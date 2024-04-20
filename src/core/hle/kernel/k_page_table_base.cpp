// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/kernel/k_address_space_info.h"
#include "core/hle/kernel/k_page_table_base.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_system_resource.h"

namespace Kernel {

namespace {

class KScopedLightLockPair {
    SUYU_NON_COPYABLE(KScopedLightLockPair);
    SUYU_NON_MOVEABLE(KScopedLightLockPair);

private:
    KLightLock* m_lower;
    KLightLock* m_upper;

public:
    KScopedLightLockPair(KLightLock& lhs, KLightLock& rhs) {
        // Ensure our locks are in a consistent order.
        if (std::addressof(lhs) <= std::addressof(rhs)) {
            m_lower = std::addressof(lhs);
            m_upper = std::addressof(rhs);
        } else {
            m_lower = std::addressof(rhs);
            m_upper = std::addressof(lhs);
        }

        // Acquire both locks.
        m_lower->Lock();
        if (m_lower != m_upper) {
            m_upper->Lock();
        }
    }

    ~KScopedLightLockPair() {
        // Unlock the upper lock.
        if (m_upper != nullptr && m_upper != m_lower) {
            m_upper->Unlock();
        }

        // Unlock the lower lock.
        if (m_lower != nullptr) {
            m_lower->Unlock();
        }
    }

public:
    // Utility.
    void TryUnlockHalf(KLightLock& lock) {
        // Only allow unlocking if the lock is half the pair.
        if (m_lower != m_upper) {
            // We want to be sure the lock is one we own.
            if (m_lower == std::addressof(lock)) {
                lock.Unlock();
                m_lower = nullptr;
            } else if (m_upper == std::addressof(lock)) {
                lock.Unlock();
                m_upper = nullptr;
            }
        }
    }
};

template <typename AddressType>
void InvalidateInstructionCache(KernelCore& kernel, KPageTableBase* table, AddressType addr,
                                u64 size) {
    // TODO: lock the process list
    for (auto& process : kernel.GetProcessList()) {
        if (std::addressof(process->GetPageTable().GetBasePageTable()) != table) {
            continue;
        }

        for (size_t i = 0; i < Core::Hardware::NUM_CPU_CORES; i++) {
            auto* interface = process->GetArmInterface(i);
            if (interface) {
                interface->InvalidateCacheRange(GetInteger(addr), size);
            }
        }
    }
}

void ClearBackingRegion(Core::System& system, KPhysicalAddress addr, u64 size, u32 fill_value) {
    system.DeviceMemory().buffer.ClearBackingRegion(GetInteger(addr) - Core::DramMemoryMap::Base,
                                                    size, fill_value);
}

template <typename AddressType>
Result InvalidateDataCache(AddressType addr, u64 size) {
    R_SUCCEED();
}

template <typename AddressType>
Result StoreDataCache(AddressType addr, u64 size) {
    R_SUCCEED();
}

template <typename AddressType>
Result FlushDataCache(AddressType addr, u64 size) {
    R_SUCCEED();
}

constexpr Common::MemoryPermission ConvertToMemoryPermission(KMemoryPermission perm) {
    Common::MemoryPermission perms{};
    if (True(perm & KMemoryPermission::UserRead)) {
        perms |= Common::MemoryPermission::Read;
    }
    if (True(perm & KMemoryPermission::UserWrite)) {
        perms |= Common::MemoryPermission::Write;
    }
#ifdef HAS_NCE
    if (True(perm & KMemoryPermission::UserExecute)) {
        perms |= Common::MemoryPermission::Execute;
    }
#endif
    return perms;
}

} // namespace

void KPageTableBase::MemoryRange::Open() {
    // If the range contains heap pages, open them.
    if (this->IsHeap()) {
        m_kernel.MemoryManager().Open(this->GetAddress(), this->GetSize() / PageSize);
    }
}

void KPageTableBase::MemoryRange::Close() {
    // If the range contains heap pages, close them.
    if (this->IsHeap()) {
        m_kernel.MemoryManager().Close(this->GetAddress(), this->GetSize() / PageSize);
    }
}

KPageTableBase::KPageTableBase(KernelCore& kernel)
    : m_kernel(kernel), m_system(kernel.System()), m_general_lock(kernel),
      m_map_physical_memory_lock(kernel), m_device_map_lock(kernel) {}
KPageTableBase::~KPageTableBase() = default;

Result KPageTableBase::InitializeForKernel(bool is_64_bit, KVirtualAddress start,
                                           KVirtualAddress end, Core::Memory::Memory& memory) {
    // Initialize our members.
    m_address_space_width =
        static_cast<u32>(is_64_bit ? Common::BitSize<u64>() : Common::BitSize<u32>());
    m_address_space_start = KProcessAddress(GetInteger(start));
    m_address_space_end = KProcessAddress(GetInteger(end));
    m_is_kernel = true;
    m_enable_aslr = true;
    m_enable_device_address_space_merge = false;

    m_heap_region_start = 0;
    m_heap_region_end = 0;
    m_current_heap_end = 0;
    m_alias_region_start = 0;
    m_alias_region_end = 0;
    m_stack_region_start = 0;
    m_stack_region_end = 0;
    m_kernel_map_region_start = 0;
    m_kernel_map_region_end = 0;
    m_alias_code_region_start = 0;
    m_alias_code_region_end = 0;
    m_code_region_start = 0;
    m_code_region_end = 0;
    m_max_heap_size = 0;
    m_mapped_physical_memory_size = 0;
    m_mapped_unsafe_physical_memory = 0;
    m_mapped_insecure_memory = 0;
    m_mapped_ipc_server_memory = 0;
    m_alias_region_extra_size = 0;

    m_memory_block_slab_manager =
        m_kernel.GetSystemSystemResource().GetMemoryBlockSlabManagerPointer();
    m_block_info_manager = m_kernel.GetSystemSystemResource().GetBlockInfoManagerPointer();
    m_resource_limit = m_kernel.GetSystemResourceLimit();

    m_allocate_option = KMemoryManager::EncodeOption(KMemoryManager::Pool::System,
                                                     KMemoryManager::Direction::FromFront);
    m_heap_fill_value = MemoryFillValue_Zero;
    m_ipc_fill_value = MemoryFillValue_Zero;
    m_stack_fill_value = MemoryFillValue_Zero;

    m_cached_physical_linear_region = nullptr;
    m_cached_physical_heap_region = nullptr;

    // Initialize our implementation.
    m_impl = std::make_unique<Common::PageTable>();
    m_impl->Resize(m_address_space_width, PageBits);

    // Set the tracking memory.
    m_memory = std::addressof(memory);

    // Initialize our memory block manager.
    R_RETURN(m_memory_block_manager.Initialize(m_address_space_start, m_address_space_end,
                                               m_memory_block_slab_manager));
}

Result KPageTableBase::InitializeForProcess(Svc::CreateProcessFlag as_type, bool enable_aslr,
                                            bool enable_das_merge, bool from_back,
                                            KMemoryManager::Pool pool, KProcessAddress code_address,
                                            size_t code_size, KSystemResource* system_resource,
                                            KResourceLimit* resource_limit,
                                            Core::Memory::Memory& memory,
                                            KProcessAddress aslr_space_start) {
    // Calculate region extents.
    const size_t as_width = GetAddressSpaceWidth(as_type);
    const KProcessAddress start = 0;
    const KProcessAddress end = (1ULL << as_width);

    // Validate the region.
    ASSERT(start <= code_address);
    ASSERT(code_address < code_address + code_size);
    ASSERT(code_address + code_size - 1 <= end - 1);

    // Define helpers.
    auto GetSpaceStart = [&](KAddressSpaceInfo::Type type) {
        return KAddressSpaceInfo::GetAddressSpaceStart(m_address_space_width, type);
    };
    auto GetSpaceSize = [&](KAddressSpaceInfo::Type type) {
        return KAddressSpaceInfo::GetAddressSpaceSize(m_address_space_width, type);
    };

    // Set our bit width and heap/alias sizes.
    m_address_space_width = static_cast<u32>(GetAddressSpaceWidth(as_type));
    size_t alias_region_size = GetSpaceSize(KAddressSpaceInfo::Type::Alias);
    size_t heap_region_size = GetSpaceSize(KAddressSpaceInfo::Type::Heap);

    // Adjust heap/alias size if we don't have an alias region.
    if ((as_type & Svc::CreateProcessFlag::AddressSpaceMask) ==
        Svc::CreateProcessFlag::AddressSpace32BitWithoutAlias) {
        heap_region_size += alias_region_size;
        alias_region_size = 0;
    }

    // Set code regions and determine remaining sizes.
    KProcessAddress process_code_start;
    KProcessAddress process_code_end;
    size_t stack_region_size;
    size_t kernel_map_region_size;
    if (m_address_space_width == 39) {
        alias_region_size = GetSpaceSize(KAddressSpaceInfo::Type::Alias);
        heap_region_size = GetSpaceSize(KAddressSpaceInfo::Type::Heap);
        stack_region_size = GetSpaceSize(KAddressSpaceInfo::Type::Stack);
        kernel_map_region_size = GetSpaceSize(KAddressSpaceInfo::Type::MapSmall);
        m_code_region_start = m_address_space_start + aslr_space_start +
                              GetSpaceStart(KAddressSpaceInfo::Type::Map39Bit);
        m_code_region_end = m_code_region_start + GetSpaceSize(KAddressSpaceInfo::Type::Map39Bit);
        m_alias_code_region_start = m_code_region_start;
        m_alias_code_region_end = m_code_region_end;
        process_code_start = Common::AlignDown(GetInteger(code_address), RegionAlignment);
        process_code_end = Common::AlignUp(GetInteger(code_address) + code_size, RegionAlignment);
    } else {
        stack_region_size = 0;
        kernel_map_region_size = 0;
        m_code_region_start = GetSpaceStart(KAddressSpaceInfo::Type::MapSmall);
        m_code_region_end = m_code_region_start + GetSpaceSize(KAddressSpaceInfo::Type::MapSmall);
        m_stack_region_start = m_code_region_start;
        m_alias_code_region_start = m_code_region_start;
        m_alias_code_region_end = GetSpaceStart(KAddressSpaceInfo::Type::MapLarge) +
                                  GetSpaceSize(KAddressSpaceInfo::Type::MapLarge);
        m_stack_region_end = m_code_region_end;
        m_kernel_map_region_start = m_code_region_start;
        m_kernel_map_region_end = m_code_region_end;
        process_code_start = m_code_region_start;
        process_code_end = m_code_region_end;
    }

    m_alias_region_extra_size = 0;
    if (as_type == Svc::CreateProcessFlag::EnableReservedRegionExtraSize) {
        m_alias_region_extra_size = GetAddressSpaceSize() / 8;
        alias_region_size += m_alias_region_extra_size;
    }

    // Set other basic fields.
    m_enable_aslr = enable_aslr;
    m_enable_device_address_space_merge = enable_das_merge;
    m_address_space_start = start;
    m_address_space_end = end;
    m_is_kernel = false;
    m_memory_block_slab_manager = system_resource->GetMemoryBlockSlabManagerPointer();
    m_block_info_manager = system_resource->GetBlockInfoManagerPointer();
    m_resource_limit = resource_limit;

    // Determine the region we can place our undetermineds in.
    KProcessAddress alloc_start;
    size_t alloc_size;
    if ((GetInteger(process_code_start) - GetInteger(m_code_region_start)) >=
        (GetInteger(end) - GetInteger(process_code_end))) {
        alloc_start = m_code_region_start;
        alloc_size = GetInteger(process_code_start) - GetInteger(m_code_region_start);
    } else {
        alloc_start = process_code_end;
        alloc_size = GetInteger(end) - GetInteger(process_code_end);
    }
    const size_t needed_size =
        (alias_region_size + heap_region_size + stack_region_size + kernel_map_region_size);
    R_UNLESS(alloc_size >= needed_size, ResultOutOfMemory);

    const size_t remaining_size = alloc_size - needed_size;

    // Determine random placements for each region.
    size_t alias_rnd = 0, heap_rnd = 0, stack_rnd = 0, kmap_rnd = 0;
    if (enable_aslr) {
        alias_rnd = KSystemControl::GenerateRandomRange(0, remaining_size / RegionAlignment) *
                    RegionAlignment;
        heap_rnd = KSystemControl::GenerateRandomRange(0, remaining_size / RegionAlignment) *
                   RegionAlignment;
        stack_rnd = KSystemControl::GenerateRandomRange(0, remaining_size / RegionAlignment) *
                    RegionAlignment;
        kmap_rnd = KSystemControl::GenerateRandomRange(0, remaining_size / RegionAlignment) *
                   RegionAlignment;
    }

    // Setup heap and alias regions.
    m_alias_region_start = alloc_start + alias_rnd;
    m_alias_region_end = m_alias_region_start + alias_region_size;
    m_heap_region_start = alloc_start + heap_rnd;
    m_heap_region_end = m_heap_region_start + heap_region_size;

    if (alias_rnd <= heap_rnd) {
        m_heap_region_start += alias_region_size;
        m_heap_region_end += alias_region_size;
    } else {
        m_alias_region_start += heap_region_size;
        m_alias_region_end += heap_region_size;
    }

    // Setup stack region.
    if (stack_region_size) {
        m_stack_region_start = alloc_start + stack_rnd;
        m_stack_region_end = m_stack_region_start + stack_region_size;

        if (alias_rnd < stack_rnd) {
            m_stack_region_start += alias_region_size;
            m_stack_region_end += alias_region_size;
        } else {
            m_alias_region_start += stack_region_size;
            m_alias_region_end += stack_region_size;
        }

        if (heap_rnd < stack_rnd) {
            m_stack_region_start += heap_region_size;
            m_stack_region_end += heap_region_size;
        } else {
            m_heap_region_start += stack_region_size;
            m_heap_region_end += stack_region_size;
        }
    }

    // Setup kernel map region.
    if (kernel_map_region_size) {
        m_kernel_map_region_start = alloc_start + kmap_rnd;
        m_kernel_map_region_end = m_kernel_map_region_start + kernel_map_region_size;

        if (alias_rnd < kmap_rnd) {
            m_kernel_map_region_start += alias_region_size;
            m_kernel_map_region_end += alias_region_size;
        } else {
            m_alias_region_start += kernel_map_region_size;
            m_alias_region_end += kernel_map_region_size;
        }

        if (heap_rnd < kmap_rnd) {
            m_kernel_map_region_start += heap_region_size;
            m_kernel_map_region_end += heap_region_size;
        } else {
            m_heap_region_start += kernel_map_region_size;
            m_heap_region_end += kernel_map_region_size;
        }

        if (stack_region_size) {
            if (stack_rnd < kmap_rnd) {
                m_kernel_map_region_start += stack_region_size;
                m_kernel_map_region_end += stack_region_size;
            } else {
                m_stack_region_start += kernel_map_region_size;
                m_stack_region_end += kernel_map_region_size;
            }
        }
    }

    // Set heap and fill members.
    m_current_heap_end = m_heap_region_start;
    m_max_heap_size = 0;
    m_mapped_physical_memory_size = 0;
    m_mapped_unsafe_physical_memory = 0;
    m_mapped_insecure_memory = 0;
    m_mapped_ipc_server_memory = 0;

    // const bool fill_memory = KTargetSystem::IsDebugMemoryFillEnabled();
    const bool fill_memory = false;
    m_heap_fill_value = fill_memory ? MemoryFillValue_Heap : MemoryFillValue_Zero;
    m_ipc_fill_value = fill_memory ? MemoryFillValue_Ipc : MemoryFillValue_Zero;
    m_stack_fill_value = fill_memory ? MemoryFillValue_Stack : MemoryFillValue_Zero;

    // Set allocation option.
    m_allocate_option =
        KMemoryManager::EncodeOption(pool, from_back ? KMemoryManager::Direction::FromBack
                                                     : KMemoryManager::Direction::FromFront);

    // Ensure that we regions inside our address space.
    auto IsInAddressSpace = [&](KProcessAddress addr) {
        return m_address_space_start <= addr && addr <= m_address_space_end;
    };
    ASSERT(IsInAddressSpace(m_alias_region_start));
    ASSERT(IsInAddressSpace(m_alias_region_end));
    ASSERT(IsInAddressSpace(m_heap_region_start));
    ASSERT(IsInAddressSpace(m_heap_region_end));
    ASSERT(IsInAddressSpace(m_stack_region_start));
    ASSERT(IsInAddressSpace(m_stack_region_end));
    ASSERT(IsInAddressSpace(m_kernel_map_region_start));
    ASSERT(IsInAddressSpace(m_kernel_map_region_end));

    // Ensure that we selected regions that don't overlap.
    const KProcessAddress alias_start = m_alias_region_start;
    const KProcessAddress alias_last = m_alias_region_end - 1;
    const KProcessAddress heap_start = m_heap_region_start;
    const KProcessAddress heap_last = m_heap_region_end - 1;
    const KProcessAddress stack_start = m_stack_region_start;
    const KProcessAddress stack_last = m_stack_region_end - 1;
    const KProcessAddress kmap_start = m_kernel_map_region_start;
    const KProcessAddress kmap_last = m_kernel_map_region_end - 1;
    ASSERT(alias_last < heap_start || heap_last < alias_start);
    ASSERT(alias_last < stack_start || stack_last < alias_start);
    ASSERT(alias_last < kmap_start || kmap_last < alias_start);
    ASSERT(heap_last < stack_start || stack_last < heap_start);
    ASSERT(heap_last < kmap_start || kmap_last < heap_start);

    // Initialize our implementation.
    m_impl = std::make_unique<Common::PageTable>();
    m_impl->Resize(m_address_space_width, PageBits);

    // Set the tracking memory.
    m_memory = std::addressof(memory);

    // Initialize our memory block manager.
    R_RETURN(m_memory_block_manager.Initialize(m_address_space_start, m_address_space_end,
                                               m_memory_block_slab_manager));
}

Result KPageTableBase::FinalizeProcess() {
    // Only process tables should be finalized.
    ASSERT(!this->IsKernel());

    // NOTE: Here Nintendo calls an unknown OnFinalize function.
    // this->OnFinalize();

    // NOTE: Here Nintendo calls a second unknown OnFinalize function.
    // this->OnFinalize2();

    // NOTE: Here Nintendo does a page table walk to discover heap pages to free.
    // We will use the block manager finalization below to free them.

    R_SUCCEED();
}

void KPageTableBase::Finalize() {
    this->FinalizeProcess();

    auto BlockCallback = [&](KProcessAddress addr, u64 size) {
        if (m_impl->fastmem_arena) {
            m_system.DeviceMemory().buffer.Unmap(GetInteger(addr), size, false);
        }

        // Get physical pages.
        KPageGroup pg(m_kernel, m_block_info_manager);
        this->MakePageGroup(pg, addr, size / PageSize);

        // Free the pages.
        pg.CloseAndReset();
    };

    // Finalize memory blocks.
    {
        KScopedLightLock lk(m_general_lock);
        m_memory_block_manager.Finalize(m_memory_block_slab_manager, std::move(BlockCallback));
    }

    // Free any unsafe mapped memory.
    if (m_mapped_unsafe_physical_memory) {
        UNIMPLEMENTED();
    }

    // Release any insecure mapped memory.
    if (m_mapped_insecure_memory) {
        if (auto* const insecure_resource_limit =
                KSystemControl::GetInsecureMemoryResourceLimit(m_kernel);
            insecure_resource_limit != nullptr) {
            insecure_resource_limit->Release(Svc::LimitableResource::PhysicalMemoryMax,
                                             m_mapped_insecure_memory);
        }
    }

    // Release any ipc server memory.
    if (m_mapped_ipc_server_memory) {
        m_resource_limit->Release(Svc::LimitableResource::PhysicalMemoryMax,
                                  m_mapped_ipc_server_memory);
    }

    // Close the backing page table, as the destructor is not called for guest objects.
    m_impl.reset();
}

KProcessAddress KPageTableBase::GetRegionAddress(Svc::MemoryState state) const {
    switch (state) {
    case Svc::MemoryState::Free:
    case Svc::MemoryState::Kernel:
        return m_address_space_start;
    case Svc::MemoryState::Normal:
        return m_heap_region_start;
    case Svc::MemoryState::Ipc:
    case Svc::MemoryState::NonSecureIpc:
    case Svc::MemoryState::NonDeviceIpc:
        return m_alias_region_start;
    case Svc::MemoryState::Stack:
        return m_stack_region_start;
    case Svc::MemoryState::Static:
    case Svc::MemoryState::ThreadLocal:
        return m_kernel_map_region_start;
    case Svc::MemoryState::Io:
    case Svc::MemoryState::Shared:
    case Svc::MemoryState::AliasCode:
    case Svc::MemoryState::AliasCodeData:
    case Svc::MemoryState::Transferred:
    case Svc::MemoryState::SharedTransferred:
    case Svc::MemoryState::SharedCode:
    case Svc::MemoryState::GeneratedCode:
    case Svc::MemoryState::CodeOut:
    case Svc::MemoryState::Coverage:
    case Svc::MemoryState::Insecure:
        return m_alias_code_region_start;
    case Svc::MemoryState::Code:
    case Svc::MemoryState::CodeData:
        return m_code_region_start;
    default:
        UNREACHABLE();
    }
}

size_t KPageTableBase::GetRegionSize(Svc::MemoryState state) const {
    switch (state) {
    case Svc::MemoryState::Free:
    case Svc::MemoryState::Kernel:
        return m_address_space_end - m_address_space_start;
    case Svc::MemoryState::Normal:
        return m_heap_region_end - m_heap_region_start;
    case Svc::MemoryState::Ipc:
    case Svc::MemoryState::NonSecureIpc:
    case Svc::MemoryState::NonDeviceIpc:
        return m_alias_region_end - m_alias_region_start;
    case Svc::MemoryState::Stack:
        return m_stack_region_end - m_stack_region_start;
    case Svc::MemoryState::Static:
    case Svc::MemoryState::ThreadLocal:
        return m_kernel_map_region_end - m_kernel_map_region_start;
    case Svc::MemoryState::Io:
    case Svc::MemoryState::Shared:
    case Svc::MemoryState::AliasCode:
    case Svc::MemoryState::AliasCodeData:
    case Svc::MemoryState::Transferred:
    case Svc::MemoryState::SharedTransferred:
    case Svc::MemoryState::SharedCode:
    case Svc::MemoryState::GeneratedCode:
    case Svc::MemoryState::CodeOut:
    case Svc::MemoryState::Coverage:
    case Svc::MemoryState::Insecure:
        return m_alias_code_region_end - m_alias_code_region_start;
    case Svc::MemoryState::Code:
    case Svc::MemoryState::CodeData:
        return m_code_region_end - m_code_region_start;
    default:
        UNREACHABLE();
    }
}

bool KPageTableBase::CanContain(KProcessAddress addr, size_t size, Svc::MemoryState state) const {
    const KProcessAddress end = addr + size;
    const KProcessAddress last = end - 1;

    const KProcessAddress region_start = this->GetRegionAddress(state);
    const size_t region_size = this->GetRegionSize(state);

    const bool is_in_region =
        region_start <= addr && addr < end && last <= region_start + region_size - 1;
    const bool is_in_heap = !(end <= m_heap_region_start || m_heap_region_end <= addr ||
                              m_heap_region_start == m_heap_region_end);
    const bool is_in_alias = !(end <= m_alias_region_start || m_alias_region_end <= addr ||
                               m_alias_region_start == m_alias_region_end);
    switch (state) {
    case Svc::MemoryState::Free:
    case Svc::MemoryState::Kernel:
        return is_in_region;
    case Svc::MemoryState::Io:
    case Svc::MemoryState::Static:
    case Svc::MemoryState::Code:
    case Svc::MemoryState::CodeData:
    case Svc::MemoryState::Shared:
    case Svc::MemoryState::AliasCode:
    case Svc::MemoryState::AliasCodeData:
    case Svc::MemoryState::Stack:
    case Svc::MemoryState::ThreadLocal:
    case Svc::MemoryState::Transferred:
    case Svc::MemoryState::SharedTransferred:
    case Svc::MemoryState::SharedCode:
    case Svc::MemoryState::GeneratedCode:
    case Svc::MemoryState::CodeOut:
    case Svc::MemoryState::Coverage:
    case Svc::MemoryState::Insecure:
        return is_in_region && !is_in_heap && !is_in_alias;
    case Svc::MemoryState::Normal:
        ASSERT(is_in_heap);
        return is_in_region && !is_in_alias;
    case Svc::MemoryState::Ipc:
    case Svc::MemoryState::NonSecureIpc:
    case Svc::MemoryState::NonDeviceIpc:
        ASSERT(is_in_alias);
        return is_in_region && !is_in_heap;
    default:
        return false;
    }
}

Result KPageTableBase::CheckMemoryState(const KMemoryInfo& info, KMemoryState state_mask,
                                        KMemoryState state, KMemoryPermission perm_mask,
                                        KMemoryPermission perm, KMemoryAttribute attr_mask,
                                        KMemoryAttribute attr) const {
    // Validate the states match expectation.
    R_UNLESS((info.m_state & state_mask) == state, ResultInvalidCurrentMemory);
    R_UNLESS((info.m_permission & perm_mask) == perm, ResultInvalidCurrentMemory);
    R_UNLESS((info.m_attribute & attr_mask) == attr, ResultInvalidCurrentMemory);

    R_SUCCEED();
}

Result KPageTableBase::CheckMemoryStateContiguous(size_t* out_blocks_needed, KProcessAddress addr,
                                                  size_t size, KMemoryState state_mask,
                                                  KMemoryState state, KMemoryPermission perm_mask,
                                                  KMemoryPermission perm,
                                                  KMemoryAttribute attr_mask,
                                                  KMemoryAttribute attr) const {
    ASSERT(this->IsLockedByCurrentThread());

    // Get information about the first block.
    const KProcessAddress last_addr = addr + size - 1;
    KMemoryBlockManager::const_iterator it = m_memory_block_manager.FindIterator(addr);
    KMemoryInfo info = it->GetMemoryInfo();

    // If the start address isn't aligned, we need a block.
    const size_t blocks_for_start_align =
        (Common::AlignDown(GetInteger(addr), PageSize) != info.GetAddress()) ? 1 : 0;

    while (true) {
        // Validate against the provided masks.
        R_TRY(this->CheckMemoryState(info, state_mask, state, perm_mask, perm, attr_mask, attr));

        // Break once we're done.
        if (last_addr <= info.GetLastAddress()) {
            break;
        }

        // Advance our iterator.
        it++;
        ASSERT(it != m_memory_block_manager.cend());
        info = it->GetMemoryInfo();
    }

    // If the end address isn't aligned, we need a block.
    const size_t blocks_for_end_align =
        (Common::AlignUp(GetInteger(addr) + size, PageSize) != info.GetEndAddress()) ? 1 : 0;

    if (out_blocks_needed != nullptr) {
        *out_blocks_needed = blocks_for_start_align + blocks_for_end_align;
    }

    R_SUCCEED();
}

Result KPageTableBase::CheckMemoryState(KMemoryState* out_state, KMemoryPermission* out_perm,
                                        KMemoryAttribute* out_attr, size_t* out_blocks_needed,
                                        KMemoryBlockManager::const_iterator it,
                                        KProcessAddress last_addr, KMemoryState state_mask,
                                        KMemoryState state, KMemoryPermission perm_mask,
                                        KMemoryPermission perm, KMemoryAttribute attr_mask,
                                        KMemoryAttribute attr, KMemoryAttribute ignore_attr) const {
    ASSERT(this->IsLockedByCurrentThread());

    // Get information about the first block.
    KMemoryInfo info = it->GetMemoryInfo();

    // Validate all blocks in the range have correct state.
    const KMemoryState first_state = info.m_state;
    const KMemoryPermission first_perm = info.m_permission;
    const KMemoryAttribute first_attr = info.m_attribute;
    while (true) {
        // Validate the current block.
        R_UNLESS(info.m_state == first_state, ResultInvalidCurrentMemory);
        R_UNLESS(info.m_permission == first_perm, ResultInvalidCurrentMemory);
        R_UNLESS((info.m_attribute | ignore_attr) == (first_attr | ignore_attr),
                 ResultInvalidCurrentMemory);

        // Validate against the provided masks.
        R_TRY(this->CheckMemoryState(info, state_mask, state, perm_mask, perm, attr_mask, attr));

        // Break once we're done.
        if (last_addr <= info.GetLastAddress()) {
            break;
        }

        // Advance our iterator.
        it++;
        ASSERT(it != m_memory_block_manager.cend());
        info = it->GetMemoryInfo();
    }

    // Write output state.
    if (out_state != nullptr) {
        *out_state = first_state;
    }
    if (out_perm != nullptr) {
        *out_perm = first_perm;
    }
    if (out_attr != nullptr) {
        *out_attr = first_attr & ~ignore_attr;
    }

    // If the end address isn't aligned, we need a block.
    if (out_blocks_needed != nullptr) {
        const size_t blocks_for_end_align =
            (Common::AlignDown(GetInteger(last_addr), PageSize) + PageSize != info.GetEndAddress())
                ? 1
                : 0;
        *out_blocks_needed = blocks_for_end_align;
    }

    R_SUCCEED();
}

Result KPageTableBase::CheckMemoryState(KMemoryState* out_state, KMemoryPermission* out_perm,
                                        KMemoryAttribute* out_attr, size_t* out_blocks_needed,
                                        KProcessAddress addr, size_t size, KMemoryState state_mask,
                                        KMemoryState state, KMemoryPermission perm_mask,
                                        KMemoryPermission perm, KMemoryAttribute attr_mask,
                                        KMemoryAttribute attr, KMemoryAttribute ignore_attr) const {
    ASSERT(this->IsLockedByCurrentThread());

    // Check memory state.
    const KProcessAddress last_addr = addr + size - 1;
    KMemoryBlockManager::const_iterator it = m_memory_block_manager.FindIterator(addr);
    R_TRY(this->CheckMemoryState(out_state, out_perm, out_attr, out_blocks_needed, it, last_addr,
                                 state_mask, state, perm_mask, perm, attr_mask, attr, ignore_attr));

    // If the start address isn't aligned, we need a block.
    if (out_blocks_needed != nullptr &&
        Common::AlignDown(GetInteger(addr), PageSize) != it->GetAddress()) {
        ++(*out_blocks_needed);
    }

    R_SUCCEED();
}

Result KPageTableBase::LockMemoryAndOpen(KPageGroup* out_pg, KPhysicalAddress* out_paddr,
                                         KProcessAddress addr, size_t size, KMemoryState state_mask,
                                         KMemoryState state, KMemoryPermission perm_mask,
                                         KMemoryPermission perm, KMemoryAttribute attr_mask,
                                         KMemoryAttribute attr, KMemoryPermission new_perm,
                                         KMemoryAttribute lock_attr) {
    // Validate basic preconditions.
    ASSERT(False(lock_attr & attr));
    ASSERT(False(lock_attr & (KMemoryAttribute::IpcLocked | KMemoryAttribute::DeviceShared)));

    // Validate the lock request.
    const size_t num_pages = size / PageSize;
    R_UNLESS(this->Contains(addr, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check that the output page group is empty, if it exists.
    if (out_pg) {
        ASSERT(out_pg->GetNumPages() == 0);
    }

    // Check the state.
    KMemoryState old_state;
    KMemoryPermission old_perm;
    KMemoryAttribute old_attr;
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(old_state), std::addressof(old_perm),
                                 std::addressof(old_attr), std::addressof(num_allocator_blocks),
                                 addr, size, state_mask | KMemoryState::FlagReferenceCounted,
                                 state | KMemoryState::FlagReferenceCounted, perm_mask, perm,
                                 attr_mask, attr));

    // Get the physical address, if we're supposed to.
    if (out_paddr != nullptr) {
        ASSERT(this->GetPhysicalAddressLocked(out_paddr, addr));
    }

    // Make the page group, if we're supposed to.
    if (out_pg != nullptr) {
        R_TRY(this->MakePageGroup(*out_pg, addr, num_pages));
    }

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // Decide on new perm and attr.
    new_perm = (new_perm != KMemoryPermission::None) ? new_perm : old_perm;
    KMemoryAttribute new_attr = old_attr | static_cast<KMemoryAttribute>(lock_attr);

    // Update permission, if we need to.
    if (new_perm != old_perm) {
        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        const KPageProperties properties = {new_perm, false,
                                            True(old_attr & KMemoryAttribute::Uncached),
                                            DisableMergeAttribute::DisableHeadBodyTail};
        R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, 0, false, properties,
                            OperationType::ChangePermissions, false));
    }

    // Apply the memory block updates.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, old_state, new_perm,
                                  new_attr, KMemoryBlockDisableMergeAttribute::Locked,
                                  KMemoryBlockDisableMergeAttribute::None);

    // If we have an output group, open.
    if (out_pg) {
        out_pg->Open();
    }

    R_SUCCEED();
}

Result KPageTableBase::UnlockMemory(KProcessAddress addr, size_t size, KMemoryState state_mask,
                                    KMemoryState state, KMemoryPermission perm_mask,
                                    KMemoryPermission perm, KMemoryAttribute attr_mask,
                                    KMemoryAttribute attr, KMemoryPermission new_perm,
                                    KMemoryAttribute lock_attr, const KPageGroup* pg) {
    // Validate basic preconditions.
    ASSERT((attr_mask & lock_attr) == lock_attr);
    ASSERT((attr & lock_attr) == lock_attr);

    // Validate the unlock request.
    const size_t num_pages = size / PageSize;
    R_UNLESS(this->Contains(addr, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the state.
    KMemoryState old_state;
    KMemoryPermission old_perm;
    KMemoryAttribute old_attr;
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(old_state), std::addressof(old_perm),
                                 std::addressof(old_attr), std::addressof(num_allocator_blocks),
                                 addr, size, state_mask | KMemoryState::FlagReferenceCounted,
                                 state | KMemoryState::FlagReferenceCounted, perm_mask, perm,
                                 attr_mask, attr));

    // Check the page group.
    if (pg != nullptr) {
        R_UNLESS(this->IsValidPageGroup(*pg, addr, num_pages), ResultInvalidMemoryRegion);
    }

    // Decide on new perm and attr.
    new_perm = (new_perm != KMemoryPermission::None) ? new_perm : old_perm;
    KMemoryAttribute new_attr = old_attr & ~static_cast<KMemoryAttribute>(lock_attr);

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // Update permission, if we need to.
    if (new_perm != old_perm) {
        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        const KPageProperties properties = {new_perm, false,
                                            True(old_attr & KMemoryAttribute::Uncached),
                                            DisableMergeAttribute::EnableAndMergeHeadBodyTail};
        R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, 0, false, properties,
                            OperationType::ChangePermissions, false));
    }

    // Apply the memory block updates.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, old_state, new_perm,
                                  new_attr, KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Locked);

    R_SUCCEED();
}

Result KPageTableBase::QueryInfoImpl(KMemoryInfo* out_info, Svc::PageInfo* out_page,
                                     KProcessAddress address) const {
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(out_info != nullptr);
    ASSERT(out_page != nullptr);

    const KMemoryBlock* block = m_memory_block_manager.FindBlock(address);
    R_UNLESS(block != nullptr, ResultInvalidCurrentMemory);

    *out_info = block->GetMemoryInfo();
    out_page->flags = 0;
    R_SUCCEED();
}

Result KPageTableBase::QueryMappingImpl(KProcessAddress* out, KPhysicalAddress address, size_t size,
                                        Svc::MemoryState state) const {
    ASSERT(!this->IsLockedByCurrentThread());
    ASSERT(out != nullptr);

    const KProcessAddress region_start = this->GetRegionAddress(state);
    const size_t region_size = this->GetRegionSize(state);

    // Check that the address/size are potentially valid.
    R_UNLESS((address < address + size), ResultNotFound);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    auto& impl = this->GetImpl();

    // Begin traversal.
    TraversalContext context;
    TraversalEntry cur_entry = {.phys_addr = 0, .block_size = 0};
    bool cur_valid = false;
    TraversalEntry next_entry;
    bool next_valid;
    size_t tot_size = 0;

    next_valid =
        impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), region_start);
    next_entry.block_size =
        (next_entry.block_size - (GetInteger(region_start) & (next_entry.block_size - 1)));

    // Iterate, looking for entry.
    while (true) {
        if ((!next_valid && !cur_valid) ||
            (next_valid && cur_valid &&
             next_entry.phys_addr == cur_entry.phys_addr + cur_entry.block_size)) {
            cur_entry.block_size += next_entry.block_size;
        } else {
            if (cur_valid && cur_entry.phys_addr <= address &&
                address + size <= cur_entry.phys_addr + cur_entry.block_size) {
                // Check if this region is valid.
                const KProcessAddress mapped_address =
                    (region_start + tot_size) + GetInteger(address - cur_entry.phys_addr);
                if (R_SUCCEEDED(this->CheckMemoryState(
                        mapped_address, size, KMemoryState::Mask, static_cast<KMemoryState>(state),
                        KMemoryPermission::UserRead, KMemoryPermission::UserRead,
                        KMemoryAttribute::None, KMemoryAttribute::None))) {
                    // It is!
                    *out = mapped_address;
                    R_SUCCEED();
                }
            }

            // Update tracking variables.
            tot_size += cur_entry.block_size;
            cur_entry = next_entry;
            cur_valid = next_valid;
        }

        if (cur_entry.block_size + tot_size >= region_size) {
            break;
        }

        next_valid = impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
    }

    // Check the last entry.
    R_UNLESS(cur_valid, ResultNotFound);
    R_UNLESS(cur_entry.phys_addr <= address, ResultNotFound);
    R_UNLESS(address + size <= cur_entry.phys_addr + cur_entry.block_size, ResultNotFound);

    // Check if the last region is valid.
    const KProcessAddress mapped_address =
        (region_start + tot_size) + GetInteger(address - cur_entry.phys_addr);
    R_TRY_CATCH(this->CheckMemoryState(mapped_address, size, KMemoryState::All,
                                       static_cast<KMemoryState>(state),
                                       KMemoryPermission::UserRead, KMemoryPermission::UserRead,
                                       KMemoryAttribute::None, KMemoryAttribute::None)) {
        R_CONVERT_ALL(ResultNotFound);
    }
    R_END_TRY_CATCH;

    // We found the region.
    *out = mapped_address;
    R_SUCCEED();
}

Result KPageTableBase::MapMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                 size_t size) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Validate that the source address's state is valid.
    KMemoryState src_state;
    size_t num_src_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(src_state), nullptr, nullptr,
                                 std::addressof(num_src_allocator_blocks), src_address, size,
                                 KMemoryState::FlagCanAlias, KMemoryState::FlagCanAlias,
                                 KMemoryPermission::All, KMemoryPermission::UserReadWrite,
                                 KMemoryAttribute::All, KMemoryAttribute::None));

    // Validate that the dst address's state is valid.
    size_t num_dst_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_dst_allocator_blocks), dst_address, size,
                                 KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::None,
                                 KMemoryAttribute::None));

    // Create an update allocator for the source.
    Result src_allocator_result;
    KMemoryBlockManagerUpdateAllocator src_allocator(std::addressof(src_allocator_result),
                                                     m_memory_block_slab_manager,
                                                     num_src_allocator_blocks);
    R_TRY(src_allocator_result);

    // Create an update allocator for the destination.
    Result dst_allocator_result;
    KMemoryBlockManagerUpdateAllocator dst_allocator(std::addressof(dst_allocator_result),
                                                     m_memory_block_slab_manager,
                                                     num_dst_allocator_blocks);
    R_TRY(dst_allocator_result);

    // Map the memory.
    {
        // Determine the number of pages being operated on.
        const size_t num_pages = size / PageSize;

        // Create page groups for the memory being unmapped.
        KPageGroup pg(m_kernel, m_block_info_manager);

        // Create the page group representing the source.
        R_TRY(this->MakePageGroup(pg, src_address, num_pages));

        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        // Reprotect the source as kernel-read/not mapped.
        const KMemoryPermission new_src_perm = static_cast<KMemoryPermission>(
            KMemoryPermission::KernelRead | KMemoryPermission::NotMapped);
        const KMemoryAttribute new_src_attr = KMemoryAttribute::Locked;
        const KPageProperties src_properties = {new_src_perm, false, false,
                                                DisableMergeAttribute::DisableHeadBodyTail};
        R_TRY(this->Operate(updater.GetPageList(), src_address, num_pages, 0, false, src_properties,
                            OperationType::ChangePermissions, false));

        // Ensure that we unprotect the source pages on failure.
        ON_RESULT_FAILURE {
            const KPageProperties unprotect_properties = {
                KMemoryPermission::UserReadWrite, false, false,
                DisableMergeAttribute::EnableHeadBodyTail};
            R_ASSERT(this->Operate(updater.GetPageList(), src_address, num_pages, 0, false,
                                   unprotect_properties, OperationType::ChangePermissions, true));
        };

        // Map the alias pages.
        const KPageProperties dst_map_properties = {KMemoryPermission::UserReadWrite, false, false,
                                                    DisableMergeAttribute::DisableHead};
        R_TRY(this->MapPageGroupImpl(updater.GetPageList(), dst_address, pg, dst_map_properties,
                                     false));

        // Apply the memory block updates.
        m_memory_block_manager.Update(std::addressof(src_allocator), src_address, num_pages,
                                      src_state, new_src_perm, new_src_attr,
                                      KMemoryBlockDisableMergeAttribute::Locked,
                                      KMemoryBlockDisableMergeAttribute::None);
        m_memory_block_manager.Update(
            std::addressof(dst_allocator), dst_address, num_pages, KMemoryState::Stack,
            KMemoryPermission::UserReadWrite, KMemoryAttribute::None,
            KMemoryBlockDisableMergeAttribute::Normal, KMemoryBlockDisableMergeAttribute::None);
    }

    R_SUCCEED();
}

Result KPageTableBase::UnmapMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                   size_t size) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Validate that the source address's state is valid.
    KMemoryState src_state;
    size_t num_src_allocator_blocks;
    R_TRY(this->CheckMemoryState(
        std::addressof(src_state), nullptr, nullptr, std::addressof(num_src_allocator_blocks),
        src_address, size, KMemoryState::FlagCanAlias, KMemoryState::FlagCanAlias,
        KMemoryPermission::All, KMemoryPermission::NotMapped | KMemoryPermission::KernelRead,
        KMemoryAttribute::All, KMemoryAttribute::Locked));

    // Validate that the dst address's state is valid.
    KMemoryPermission dst_perm;
    size_t num_dst_allocator_blocks;
    R_TRY(this->CheckMemoryState(
        nullptr, std::addressof(dst_perm), nullptr, std::addressof(num_dst_allocator_blocks),
        dst_address, size, KMemoryState::All, KMemoryState::Stack, KMemoryPermission::None,
        KMemoryPermission::None, KMemoryAttribute::All, KMemoryAttribute::None));

    // Create an update allocator for the source.
    Result src_allocator_result;
    KMemoryBlockManagerUpdateAllocator src_allocator(std::addressof(src_allocator_result),
                                                     m_memory_block_slab_manager,
                                                     num_src_allocator_blocks);
    R_TRY(src_allocator_result);

    // Create an update allocator for the destination.
    Result dst_allocator_result;
    KMemoryBlockManagerUpdateAllocator dst_allocator(std::addressof(dst_allocator_result),
                                                     m_memory_block_slab_manager,
                                                     num_dst_allocator_blocks);
    R_TRY(dst_allocator_result);

    // Unmap the memory.
    {
        // Determine the number of pages being operated on.
        const size_t num_pages = size / PageSize;

        // Create page groups for the memory being unmapped.
        KPageGroup pg(m_kernel, m_block_info_manager);

        // Create the page group representing the destination.
        R_TRY(this->MakePageGroup(pg, dst_address, num_pages));

        // Ensure the page group is the valid for the source.
        R_UNLESS(this->IsValidPageGroup(pg, src_address, num_pages), ResultInvalidMemoryRegion);

        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        // Unmap the aliased copy of the pages.
        const KPageProperties dst_unmap_properties = {KMemoryPermission::None, false, false,
                                                      DisableMergeAttribute::None};
        R_TRY(this->Operate(updater.GetPageList(), dst_address, num_pages, 0, false,
                            dst_unmap_properties, OperationType::Unmap, false));

        // Ensure that we re-map the aliased pages on failure.
        ON_RESULT_FAILURE {
            this->RemapPageGroup(updater.GetPageList(), dst_address, size, pg);
        };

        // Try to set the permissions for the source pages back to what they should be.
        const KPageProperties src_properties = {KMemoryPermission::UserReadWrite, false, false,
                                                DisableMergeAttribute::EnableAndMergeHeadBodyTail};
        R_TRY(this->Operate(updater.GetPageList(), src_address, num_pages, 0, false, src_properties,
                            OperationType::ChangePermissions, false));

        // Apply the memory block updates.
        m_memory_block_manager.Update(
            std::addressof(src_allocator), src_address, num_pages, src_state,
            KMemoryPermission::UserReadWrite, KMemoryAttribute::None,
            KMemoryBlockDisableMergeAttribute::None, KMemoryBlockDisableMergeAttribute::Locked);
        m_memory_block_manager.Update(
            std::addressof(dst_allocator), dst_address, num_pages, KMemoryState::None,
            KMemoryPermission::None, KMemoryAttribute::None,
            KMemoryBlockDisableMergeAttribute::None, KMemoryBlockDisableMergeAttribute::Normal);
    }

    R_SUCCEED();
}

Result KPageTableBase::MapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                     size_t size) {
    // Validate the mapping request.
    R_UNLESS(this->CanContain(dst_address, size, KMemoryState::AliasCode),
             ResultInvalidMemoryRegion);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Verify that the source memory is normal heap.
    KMemoryState src_state;
    KMemoryPermission src_perm;
    size_t num_src_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(src_state), std::addressof(src_perm), nullptr,
                                 std::addressof(num_src_allocator_blocks), src_address, size,
                                 KMemoryState::All, KMemoryState::Normal, KMemoryPermission::All,
                                 KMemoryPermission::UserReadWrite, KMemoryAttribute::All,
                                 KMemoryAttribute::None));

    // Verify that the destination memory is unmapped.
    size_t num_dst_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_dst_allocator_blocks), dst_address, size,
                                 KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::None,
                                 KMemoryAttribute::None));

    // Create an update allocator for the source.
    Result src_allocator_result;
    KMemoryBlockManagerUpdateAllocator src_allocator(std::addressof(src_allocator_result),
                                                     m_memory_block_slab_manager,
                                                     num_src_allocator_blocks);
    R_TRY(src_allocator_result);

    // Create an update allocator for the destination.
    Result dst_allocator_result;
    KMemoryBlockManagerUpdateAllocator dst_allocator(std::addressof(dst_allocator_result),
                                                     m_memory_block_slab_manager,
                                                     num_dst_allocator_blocks);
    R_TRY(dst_allocator_result);

    // Map the code memory.
    {
        // Determine the number of pages being operated on.
        const size_t num_pages = size / PageSize;

        // Create page groups for the memory being unmapped.
        KPageGroup pg(m_kernel, m_block_info_manager);

        // Create the page group representing the source.
        R_TRY(this->MakePageGroup(pg, src_address, num_pages));

        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        // Reprotect the source as kernel-read/not mapped.
        const KMemoryPermission new_perm = static_cast<KMemoryPermission>(
            KMemoryPermission::KernelRead | KMemoryPermission::NotMapped);
        const KPageProperties src_properties = {new_perm, false, false,
                                                DisableMergeAttribute::DisableHeadBodyTail};
        R_TRY(this->Operate(updater.GetPageList(), src_address, num_pages, 0, false, src_properties,
                            OperationType::ChangePermissions, false));

        // Ensure that we unprotect the source pages on failure.
        ON_RESULT_FAILURE {
            const KPageProperties unprotect_properties = {
                src_perm, false, false, DisableMergeAttribute::EnableHeadBodyTail};
            R_ASSERT(this->Operate(updater.GetPageList(), src_address, num_pages, 0, false,
                                   unprotect_properties, OperationType::ChangePermissions, true));
        };

        // Map the alias pages.
        const KPageProperties dst_properties = {new_perm, false, false,
                                                DisableMergeAttribute::DisableHead};
        R_TRY(
            this->MapPageGroupImpl(updater.GetPageList(), dst_address, pg, dst_properties, false));

        // Apply the memory block updates.
        m_memory_block_manager.Update(std::addressof(src_allocator), src_address, num_pages,
                                      src_state, new_perm, KMemoryAttribute::Locked,
                                      KMemoryBlockDisableMergeAttribute::Locked,
                                      KMemoryBlockDisableMergeAttribute::None);
        m_memory_block_manager.Update(std::addressof(dst_allocator), dst_address, num_pages,
                                      KMemoryState::AliasCode, new_perm, KMemoryAttribute::None,
                                      KMemoryBlockDisableMergeAttribute::Normal,
                                      KMemoryBlockDisableMergeAttribute::None);
    }

    R_SUCCEED();
}

Result KPageTableBase::UnmapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                       size_t size) {
    // Validate the mapping request.
    R_UNLESS(this->CanContain(dst_address, size, KMemoryState::AliasCode),
             ResultInvalidMemoryRegion);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Verify that the source memory is locked normal heap.
    size_t num_src_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_src_allocator_blocks), src_address, size,
                                 KMemoryState::All, KMemoryState::Normal, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::All,
                                 KMemoryAttribute::Locked));

    // Verify that the destination memory is aliasable code.
    size_t num_dst_allocator_blocks;
    R_TRY(this->CheckMemoryStateContiguous(
        std::addressof(num_dst_allocator_blocks), dst_address, size, KMemoryState::FlagCanCodeAlias,
        KMemoryState::FlagCanCodeAlias, KMemoryPermission::None, KMemoryPermission::None,
        KMemoryAttribute::All & ~KMemoryAttribute::PermissionLocked, KMemoryAttribute::None));

    // Determine whether any pages being unmapped are code.
    bool any_code_pages = false;
    {
        KMemoryBlockManager::const_iterator it = m_memory_block_manager.FindIterator(dst_address);
        while (true) {
            // Get the memory info.
            const KMemoryInfo info = it->GetMemoryInfo();

            // Check if the memory has code flag.
            if (True(info.GetState() & KMemoryState::FlagCode)) {
                any_code_pages = true;
                break;
            }

            // Check if we're done.
            if (dst_address + size - 1 <= info.GetLastAddress()) {
                break;
            }

            // Advance.
            ++it;
        }
    }

    // Ensure that we maintain the instruction cache.
    bool reprotected_pages = false;
    SCOPE_EXIT {
        if (reprotected_pages && any_code_pages) {
            InvalidateInstructionCache(m_kernel, this, dst_address, size);
        }
    };

    // Unmap.
    {
        // Determine the number of pages being operated on.
        const size_t num_pages = size / PageSize;

        // Create page groups for the memory being unmapped.
        KPageGroup pg(m_kernel, m_block_info_manager);

        // Create the page group representing the destination.
        R_TRY(this->MakePageGroup(pg, dst_address, num_pages));

        // Verify that the page group contains the same pages as the source.
        R_UNLESS(this->IsValidPageGroup(pg, src_address, num_pages), ResultInvalidMemoryRegion);

        // Create an update allocator for the source.
        Result src_allocator_result;
        KMemoryBlockManagerUpdateAllocator src_allocator(std::addressof(src_allocator_result),
                                                         m_memory_block_slab_manager,
                                                         num_src_allocator_blocks);
        R_TRY(src_allocator_result);

        // Create an update allocator for the destination.
        Result dst_allocator_result;
        KMemoryBlockManagerUpdateAllocator dst_allocator(std::addressof(dst_allocator_result),
                                                         m_memory_block_slab_manager,
                                                         num_dst_allocator_blocks);
        R_TRY(dst_allocator_result);

        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        // Unmap the aliased copy of the pages.
        const KPageProperties dst_unmap_properties = {KMemoryPermission::None, false, false,
                                                      DisableMergeAttribute::None};
        R_TRY(this->Operate(updater.GetPageList(), dst_address, num_pages, 0, false,
                            dst_unmap_properties, OperationType::Unmap, false));

        // Ensure that we re-map the aliased pages on failure.
        ON_RESULT_FAILURE {
            this->RemapPageGroup(updater.GetPageList(), dst_address, size, pg);
        };

        // Try to set the permissions for the source pages back to what they should be.
        const KPageProperties src_properties = {KMemoryPermission::UserReadWrite, false, false,
                                                DisableMergeAttribute::EnableAndMergeHeadBodyTail};
        R_TRY(this->Operate(updater.GetPageList(), src_address, num_pages, 0, false, src_properties,
                            OperationType::ChangePermissions, false));

        // Apply the memory block updates.
        m_memory_block_manager.Update(
            std::addressof(dst_allocator), dst_address, num_pages, KMemoryState::None,
            KMemoryPermission::None, KMemoryAttribute::None,
            KMemoryBlockDisableMergeAttribute::None, KMemoryBlockDisableMergeAttribute::Normal);
        m_memory_block_manager.Update(
            std::addressof(src_allocator), src_address, num_pages, KMemoryState::Normal,
            KMemoryPermission::UserReadWrite, KMemoryAttribute::None,
            KMemoryBlockDisableMergeAttribute::None, KMemoryBlockDisableMergeAttribute::Locked);

        // Note that we reprotected pages.
        reprotected_pages = true;
    }

    R_SUCCEED();
}

Result KPageTableBase::MapInsecureMemory(KProcessAddress address, size_t size) {
    // Get the insecure memory resource limit and pool.
    auto* const insecure_resource_limit = KSystemControl::GetInsecureMemoryResourceLimit(m_kernel);
    const auto insecure_pool =
        static_cast<KMemoryManager::Pool>(KSystemControl::GetInsecureMemoryPool());

    // Reserve the insecure memory.
    // NOTE: ResultOutOfMemory is returned here instead of the usual LimitReached.
    KScopedResourceReservation memory_reservation(insecure_resource_limit,
                                                  Svc::LimitableResource::PhysicalMemoryMax, size);
    R_UNLESS(memory_reservation.Succeeded(), ResultOutOfMemory);

    // Allocate pages for the insecure memory.
    KPageGroup pg(m_kernel, m_block_info_manager);
    R_TRY(m_kernel.MemoryManager().AllocateAndOpen(
        std::addressof(pg), size / PageSize,
        KMemoryManager::EncodeOption(insecure_pool, KMemoryManager::Direction::FromFront)));

    // Close the opened pages when we're done with them.
    // If the mapping succeeds, each page will gain an extra reference, otherwise they will be freed
    // automatically.
    SCOPE_EXIT {
        pg.Close();
    };

    // Clear all the newly allocated pages.
    for (const auto& it : pg) {
        ClearBackingRegion(m_system, it.GetAddress(), it.GetSize(), m_heap_fill_value);
    }

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Validate that the address's state is valid.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), address, size,
                                 KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::None,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Map the pages.
    const size_t num_pages = size / PageSize;
    const KPageProperties map_properties = {KMemoryPermission::UserReadWrite, false, false,
                                            DisableMergeAttribute::DisableHead};
    R_TRY(this->Operate(updater.GetPageList(), address, num_pages, pg, map_properties,
                        OperationType::MapGroup, false));

    // Apply the memory block update.
    m_memory_block_manager.Update(std::addressof(allocator), address, num_pages,
                                  KMemoryState::Insecure, KMemoryPermission::UserReadWrite,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // Update our mapped insecure size.
    m_mapped_insecure_memory += size;

    // Commit the memory reservation.
    memory_reservation.Commit();

    // We succeeded.
    R_SUCCEED();
}

Result KPageTableBase::UnmapInsecureMemory(KProcessAddress address, size_t size) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), address, size,
                                 KMemoryState::All, KMemoryState::Insecure, KMemoryPermission::All,
                                 KMemoryPermission::UserReadWrite, KMemoryAttribute::All,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Unmap the memory.
    const size_t num_pages = size / PageSize;
    const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                              DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), address, num_pages, 0, false, unmap_properties,
                        OperationType::Unmap, false));

    // Apply the memory block update.
    m_memory_block_manager.Update(std::addressof(allocator), address, num_pages, KMemoryState::Free,
                                  KMemoryPermission::None, KMemoryAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal);

    // Update our mapped insecure size.
    m_mapped_insecure_memory -= size;

    // Release the insecure memory from the insecure limit.
    if (auto* const insecure_resource_limit =
            KSystemControl::GetInsecureMemoryResourceLimit(m_kernel);
        insecure_resource_limit != nullptr) {
        insecure_resource_limit->Release(Svc::LimitableResource::PhysicalMemoryMax, size);
    }

    R_SUCCEED();
}

KProcessAddress KPageTableBase::FindFreeArea(KProcessAddress region_start, size_t region_num_pages,
                                             size_t num_pages, size_t alignment, size_t offset,
                                             size_t guard_pages) const {
    KProcessAddress address = 0;

    if (num_pages <= region_num_pages) {
        if (this->IsAslrEnabled()) {
            // Try to directly find a free area up to 8 times.
            for (size_t i = 0; i < 8; i++) {
                const size_t random_offset =
                    KSystemControl::GenerateRandomRange(
                        0, (region_num_pages - num_pages - guard_pages) * PageSize / alignment) *
                    alignment;
                const KProcessAddress candidate =
                    Common::AlignDown(GetInteger(region_start + random_offset), alignment) + offset;

                KMemoryInfo info;
                Svc::PageInfo page_info;
                R_ASSERT(this->QueryInfoImpl(std::addressof(info), std::addressof(page_info),
                                             candidate));

                if (info.m_state != KMemoryState::Free) {
                    continue;
                }
                if (!(region_start <= candidate)) {
                    continue;
                }
                if (!(info.GetAddress() + guard_pages * PageSize <= GetInteger(candidate))) {
                    continue;
                }
                if (!(candidate + (num_pages + guard_pages) * PageSize - 1 <=
                      info.GetLastAddress())) {
                    continue;
                }
                if (!(candidate + (num_pages + guard_pages) * PageSize - 1 <=
                      region_start + region_num_pages * PageSize - 1)) {
                    continue;
                }

                address = candidate;
                break;
            }
            // Fall back to finding the first free area with a random offset.
            if (address == 0) {
                // NOTE: Nintendo does not account for guard pages here.
                // This may theoretically cause an offset to be chosen that cannot be mapped.
                // We will account for guard pages.
                const size_t offset_pages = KSystemControl::GenerateRandomRange(
                    0, region_num_pages - num_pages - guard_pages);
                address = m_memory_block_manager.FindFreeArea(
                    region_start + offset_pages * PageSize, region_num_pages - offset_pages,
                    num_pages, alignment, offset, guard_pages);
            }
        }
        // Find the first free area.
        if (address == 0) {
            address = m_memory_block_manager.FindFreeArea(region_start, region_num_pages, num_pages,
                                                          alignment, offset, guard_pages);
        }
    }

    return address;
}

size_t KPageTableBase::GetSize(KMemoryState state) const {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Iterate, counting blocks with the desired state.
    size_t total_size = 0;
    for (KMemoryBlockManager::const_iterator it =
             m_memory_block_manager.FindIterator(m_address_space_start);
         it != m_memory_block_manager.end(); ++it) {
        // Get the memory info.
        const KMemoryInfo info = it->GetMemoryInfo();
        if (info.GetState() == state) {
            total_size += info.GetSize();
        }
    }

    return total_size;
}

size_t KPageTableBase::GetCodeSize() const {
    return this->GetSize(KMemoryState::Code);
}

size_t KPageTableBase::GetCodeDataSize() const {
    return this->GetSize(KMemoryState::CodeData);
}

size_t KPageTableBase::GetAliasCodeSize() const {
    return this->GetSize(KMemoryState::AliasCode);
}

size_t KPageTableBase::GetAliasCodeDataSize() const {
    return this->GetSize(KMemoryState::AliasCodeData);
}

Result KPageTableBase::AllocateAndMapPagesImpl(PageLinkedList* page_list, KProcessAddress address,
                                               size_t num_pages, KMemoryPermission perm) {
    ASSERT(this->IsLockedByCurrentThread());

    // Create a page group to hold the pages we allocate.
    KPageGroup pg(m_kernel, m_block_info_manager);

    // Allocate the pages.
    R_TRY(
        m_kernel.MemoryManager().AllocateAndOpen(std::addressof(pg), num_pages, m_allocate_option));

    // Ensure that the page group is closed when we're done working with it.
    SCOPE_EXIT {
        pg.Close();
    };

    // Clear all pages.
    for (const auto& it : pg) {
        ClearBackingRegion(m_system, it.GetAddress(), it.GetSize(), m_heap_fill_value);
    }

    // Map the pages.
    const KPageProperties properties = {perm, false, false, DisableMergeAttribute::None};
    R_RETURN(this->Operate(page_list, address, num_pages, pg, properties, OperationType::MapGroup,
                           false));
}

Result KPageTableBase::MapPageGroupImpl(PageLinkedList* page_list, KProcessAddress address,
                                        const KPageGroup& pg, const KPageProperties properties,
                                        bool reuse_ll) {
    ASSERT(this->IsLockedByCurrentThread());

    // Note the current address, so that we can iterate.
    const KProcessAddress start_address = address;
    KProcessAddress cur_address = address;

    // Ensure that we clean up on failure.
    ON_RESULT_FAILURE {
        ASSERT(!reuse_ll);
        if (cur_address != start_address) {
            const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                                      DisableMergeAttribute::None};
            R_ASSERT(this->Operate(page_list, start_address,
                                   (cur_address - start_address) / PageSize, 0, false,
                                   unmap_properties, OperationType::Unmap, true));
        }
    };

    // Iterate, mapping all pages in the group.
    for (const auto& block : pg) {
        // Map and advance.
        const KPageProperties cur_properties =
            (cur_address == start_address)
                ? properties
                : KPageProperties{properties.perm, properties.io, properties.uncached,
                                  DisableMergeAttribute::None};
        R_TRY(this->Operate(page_list, cur_address, block.GetNumPages(), block.GetAddress(), true,
                            cur_properties, OperationType::Map, reuse_ll));
        cur_address += block.GetSize();
    }

    // We succeeded!
    R_SUCCEED();
}

void KPageTableBase::RemapPageGroup(PageLinkedList* page_list, KProcessAddress address, size_t size,
                                    const KPageGroup& pg) {
    ASSERT(this->IsLockedByCurrentThread());

    // Note the current address, so that we can iterate.
    const KProcessAddress start_address = address;
    const KProcessAddress last_address = start_address + size - 1;
    const KProcessAddress end_address = last_address + 1;

    // Iterate over the memory.
    auto pg_it = pg.begin();
    ASSERT(pg_it != pg.end());

    KPhysicalAddress pg_phys_addr = pg_it->GetAddress();
    size_t pg_pages = pg_it->GetNumPages();

    auto it = m_memory_block_manager.FindIterator(start_address);
    while (true) {
        // Check that the iterator is valid.
        ASSERT(it != m_memory_block_manager.end());

        // Get the memory info.
        const KMemoryInfo info = it->GetMemoryInfo();

        // Determine the range to map.
        KProcessAddress map_address = std::max<u64>(info.GetAddress(), GetInteger(start_address));
        const KProcessAddress map_end_address =
            std::min<u64>(info.GetEndAddress(), GetInteger(end_address));
        ASSERT(map_end_address != map_address);

        // Determine if we should disable head merge.
        const bool disable_head_merge =
            info.GetAddress() >= GetInteger(start_address) &&
            True(info.GetDisableMergeAttribute() & KMemoryBlockDisableMergeAttribute::Normal);
        const KPageProperties map_properties = {
            info.GetPermission(), false, false,
            disable_head_merge ? DisableMergeAttribute::DisableHead : DisableMergeAttribute::None};

        // While we have pages to map, map them.
        size_t map_pages = (map_end_address - map_address) / PageSize;
        while (map_pages > 0) {
            // Check if we're at the end of the physical block.
            if (pg_pages == 0) {
                // Ensure there are more pages to map.
                ASSERT(pg_it != pg.end());

                // Advance our physical block.
                ++pg_it;
                pg_phys_addr = pg_it->GetAddress();
                pg_pages = pg_it->GetNumPages();
            }

            // Map whatever we can.
            const size_t cur_pages = std::min(pg_pages, map_pages);
            R_ASSERT(this->Operate(page_list, map_address, map_pages, pg_phys_addr, true,
                                   map_properties, OperationType::Map, true));

            // Advance.
            map_address += cur_pages * PageSize;
            map_pages -= cur_pages;

            pg_phys_addr += cur_pages * PageSize;
            pg_pages -= cur_pages;
        }

        // Check if we're done.
        if (last_address <= info.GetLastAddress()) {
            break;
        }

        // Advance.
        ++it;
    }

    // Check that we re-mapped precisely the page group.
    ASSERT((++pg_it) == pg.end());
}

Result KPageTableBase::MakePageGroup(KPageGroup& pg, KProcessAddress addr, size_t num_pages) {
    ASSERT(this->IsLockedByCurrentThread());

    const size_t size = num_pages * PageSize;

    // We're making a new group, not adding to an existing one.
    R_UNLESS(pg.empty(), ResultInvalidCurrentMemory);

    auto& impl = this->GetImpl();

    // Begin traversal.
    TraversalContext context;
    TraversalEntry next_entry;
    R_UNLESS(impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), addr),
             ResultInvalidCurrentMemory);

    // Prepare tracking variables.
    KPhysicalAddress cur_addr = next_entry.phys_addr;
    size_t cur_size = next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
    size_t tot_size = cur_size;

    // Iterate, adding to group as we go.
    while (tot_size < size) {
        R_UNLESS(impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context)),
                 ResultInvalidCurrentMemory);

        if (next_entry.phys_addr != (cur_addr + cur_size)) {
            const size_t cur_pages = cur_size / PageSize;

            R_UNLESS(IsHeapPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);
            R_TRY(pg.AddBlock(cur_addr, cur_pages));

            cur_addr = next_entry.phys_addr;
            cur_size = next_entry.block_size;
        } else {
            cur_size += next_entry.block_size;
        }

        tot_size += next_entry.block_size;
    }

    // Ensure we add the right amount for the last block.
    if (tot_size > size) {
        cur_size -= (tot_size - size);
    }

    // add the last block.
    const size_t cur_pages = cur_size / PageSize;
    R_UNLESS(IsHeapPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);
    R_TRY(pg.AddBlock(cur_addr, cur_pages));

    R_SUCCEED();
}

bool KPageTableBase::IsValidPageGroup(const KPageGroup& pg, KProcessAddress addr,
                                      size_t num_pages) {
    ASSERT(this->IsLockedByCurrentThread());

    const size_t size = num_pages * PageSize;

    // Empty groups are necessarily invalid.
    if (pg.empty()) {
        return false;
    }

    auto& impl = this->GetImpl();

    // We're going to validate that the group we'd expect is the group we see.
    auto cur_it = pg.begin();
    KPhysicalAddress cur_block_address = cur_it->GetAddress();
    size_t cur_block_pages = cur_it->GetNumPages();

    auto UpdateCurrentIterator = [&]() {
        if (cur_block_pages == 0) {
            if ((++cur_it) == pg.end()) {
                return false;
            }

            cur_block_address = cur_it->GetAddress();
            cur_block_pages = cur_it->GetNumPages();
        }
        return true;
    };

    // Begin traversal.
    TraversalContext context;
    TraversalEntry next_entry;
    if (!impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), addr)) {
        return false;
    }

    // Prepare tracking variables.
    KPhysicalAddress cur_addr = next_entry.phys_addr;
    size_t cur_size = next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
    size_t tot_size = cur_size;

    // Iterate, comparing expected to actual.
    while (tot_size < size) {
        if (!impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context))) {
            return false;
        }

        if (next_entry.phys_addr != (cur_addr + cur_size)) {
            const size_t cur_pages = cur_size / PageSize;

            if (!IsHeapPhysicalAddress(cur_addr)) {
                return false;
            }

            if (!UpdateCurrentIterator()) {
                return false;
            }

            if (cur_block_address != cur_addr || cur_block_pages < cur_pages) {
                return false;
            }

            cur_block_address += cur_size;
            cur_block_pages -= cur_pages;
            cur_addr = next_entry.phys_addr;
            cur_size = next_entry.block_size;
        } else {
            cur_size += next_entry.block_size;
        }

        tot_size += next_entry.block_size;
    }

    // Ensure we compare the right amount for the last block.
    if (tot_size > size) {
        cur_size -= (tot_size - size);
    }

    if (!IsHeapPhysicalAddress(cur_addr)) {
        return false;
    }

    if (!UpdateCurrentIterator()) {
        return false;
    }

    return cur_block_address == cur_addr && cur_block_pages == (cur_size / PageSize);
}

Result KPageTableBase::GetContiguousMemoryRangeWithState(
    MemoryRange* out, KProcessAddress address, size_t size, KMemoryState state_mask,
    KMemoryState state, KMemoryPermission perm_mask, KMemoryPermission perm,
    KMemoryAttribute attr_mask, KMemoryAttribute attr) {
    ASSERT(this->IsLockedByCurrentThread());

    auto& impl = this->GetImpl();

    // Begin a traversal.
    TraversalContext context;
    TraversalEntry cur_entry = {.phys_addr = 0, .block_size = 0};
    R_UNLESS(impl.BeginTraversal(std::addressof(cur_entry), std::addressof(context), address),
             ResultInvalidCurrentMemory);

    // Traverse until we have enough size or we aren't contiguous any more.
    const KPhysicalAddress phys_address = cur_entry.phys_addr;
    size_t contig_size;
    for (contig_size =
             cur_entry.block_size - (GetInteger(phys_address) & (cur_entry.block_size - 1));
         contig_size < size; contig_size += cur_entry.block_size) {
        if (!impl.ContinueTraversal(std::addressof(cur_entry), std::addressof(context))) {
            break;
        }
        if (cur_entry.phys_addr != phys_address + contig_size) {
            break;
        }
    }

    // Take the minimum size for our region.
    size = std::min(size, contig_size);

    // Check that the memory is contiguous (modulo the reference count bit).
    const KMemoryState test_state_mask = state_mask | KMemoryState::FlagReferenceCounted;
    const bool is_heap = R_SUCCEEDED(this->CheckMemoryStateContiguous(
        address, size, test_state_mask, state | KMemoryState::FlagReferenceCounted, perm_mask, perm,
        attr_mask, attr));
    if (!is_heap) {
        R_TRY(this->CheckMemoryStateContiguous(address, size, test_state_mask, state, perm_mask,
                                               perm, attr_mask, attr));
    }

    // The memory is contiguous, so set the output range.
    out->Set(phys_address, size, is_heap);
    R_SUCCEED();
}

Result KPageTableBase::SetMemoryPermission(KProcessAddress addr, size_t size,
                                           Svc::MemoryPermission svc_perm) {
    const size_t num_pages = size / PageSize;

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Verify we can change the memory permission.
    KMemoryState old_state;
    KMemoryPermission old_perm;
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(old_state), std::addressof(old_perm), nullptr,
                                 std::addressof(num_allocator_blocks), addr, size,
                                 KMemoryState::FlagCanReprotect, KMemoryState::FlagCanReprotect,
                                 KMemoryPermission::None, KMemoryPermission::None,
                                 KMemoryAttribute::All, KMemoryAttribute::None));

    // Determine new perm.
    const KMemoryPermission new_perm = ConvertToKMemoryPermission(svc_perm);
    R_SUCCEED_IF(old_perm == new_perm);

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    const KPageProperties properties = {new_perm, false, false, DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, 0, false, properties,
                        OperationType::ChangePermissions, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, old_state, new_perm,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::None);

    R_SUCCEED();
}

Result KPageTableBase::SetProcessMemoryPermission(KProcessAddress addr, size_t size,
                                                  Svc::MemoryPermission svc_perm) {
    const size_t num_pages = size / PageSize;

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Verify we can change the memory permission.
    KMemoryState old_state;
    KMemoryPermission old_perm;
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(old_state), std::addressof(old_perm), nullptr,
                                 std::addressof(num_allocator_blocks), addr, size,
                                 KMemoryState::FlagCode, KMemoryState::FlagCode,
                                 KMemoryPermission::None, KMemoryPermission::None,
                                 KMemoryAttribute::All, KMemoryAttribute::None));

    // Make a new page group for the region.
    KPageGroup pg(m_kernel, m_block_info_manager);

    // Determine new perm/state.
    const KMemoryPermission new_perm = ConvertToKMemoryPermission(svc_perm);
    KMemoryState new_state = old_state;
    const bool is_w = (new_perm & KMemoryPermission::UserWrite) == KMemoryPermission::UserWrite;
    const bool is_x = (new_perm & KMemoryPermission::UserExecute) == KMemoryPermission::UserExecute;
    const bool was_x =
        (old_perm & KMemoryPermission::UserExecute) == KMemoryPermission::UserExecute;
    ASSERT(!(is_w && is_x));

    if (is_w) {
        switch (old_state) {
        case KMemoryState::Code:
            new_state = KMemoryState::CodeData;
            break;
        case KMemoryState::AliasCode:
            new_state = KMemoryState::AliasCodeData;
            break;
        default:
            UNREACHABLE();
        }
    }

    // Create a page group, if we're setting execute permissions.
    if (is_x) {
        R_TRY(this->MakePageGroup(pg, GetInteger(addr), num_pages));
    }

    // Succeed if there's nothing to do.
    R_SUCCEED_IF(old_perm == new_perm && old_state == new_state);

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    const KPageProperties properties = {new_perm, false, false, DisableMergeAttribute::None};
    const auto operation = was_x ? OperationType::ChangePermissionsAndRefreshAndFlush
                                 : OperationType::ChangePermissions;
    R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, 0, false, properties, operation,
                        false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, new_state, new_perm,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::None);

    // Ensure cache coherency, if we're setting pages as executable.
    if (is_x) {
        for (const auto& block : pg) {
            StoreDataCache(GetHeapVirtualPointer(m_kernel, block.GetAddress()), block.GetSize());
        }
        InvalidateInstructionCache(m_kernel, this, addr, size);
    }

    R_SUCCEED();
}

Result KPageTableBase::SetMemoryAttribute(KProcessAddress addr, size_t size, KMemoryAttribute mask,
                                          KMemoryAttribute attr) {
    const size_t num_pages = size / PageSize;
    ASSERT((mask | KMemoryAttribute::SetMask) == KMemoryAttribute::SetMask);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Verify we can change the memory attribute.
    KMemoryState old_state;
    KMemoryPermission old_perm;
    KMemoryAttribute old_attr;
    size_t num_allocator_blocks;
    constexpr KMemoryAttribute AttributeTestMask =
        ~(KMemoryAttribute::SetMask | KMemoryAttribute::DeviceShared);
    const KMemoryState state_test_mask =
        (True(mask & KMemoryAttribute::Uncached) ? KMemoryState::FlagCanChangeAttribute
                                                 : KMemoryState::None) |
        (True(mask & KMemoryAttribute::PermissionLocked) ? KMemoryState::FlagCanPermissionLock
                                                         : KMemoryState::None);
    R_TRY(this->CheckMemoryState(std::addressof(old_state), std::addressof(old_perm),
                                 std::addressof(old_attr), std::addressof(num_allocator_blocks),
                                 addr, size, state_test_mask, state_test_mask,
                                 KMemoryPermission::None, KMemoryPermission::None,
                                 AttributeTestMask, KMemoryAttribute::None, ~AttributeTestMask));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // If we need to, perform a change attribute operation.
    if (True(mask & KMemoryAttribute::Uncached)) {
        // Determine the new attribute.
        const KMemoryAttribute new_attr =
            static_cast<KMemoryAttribute>(((old_attr & ~mask) | (attr & mask)));

        // Perform operation.
        const KPageProperties properties = {old_perm, false,
                                            True(new_attr & KMemoryAttribute::Uncached),
                                            DisableMergeAttribute::None};
        R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, 0, false, properties,
                            OperationType::ChangePermissionsAndRefreshAndFlush, false));
    }

    // Update the blocks.
    m_memory_block_manager.UpdateAttribute(std::addressof(allocator), addr, num_pages, mask, attr);

    R_SUCCEED();
}

Result KPageTableBase::SetHeapSize(KProcessAddress* out, size_t size) {
    // Lock the physical memory mutex.
    KScopedLightLock map_phys_mem_lk(m_map_physical_memory_lock);

    // Try to perform a reduction in heap, instead of an extension.
    KProcessAddress cur_address;
    size_t allocation_size;
    {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        // Validate that setting heap size is possible at all.
        R_UNLESS(!m_is_kernel, ResultOutOfMemory);
        R_UNLESS(size <= static_cast<size_t>(m_heap_region_end - m_heap_region_start),
                 ResultOutOfMemory);
        R_UNLESS(size <= m_max_heap_size, ResultOutOfMemory);

        if (size < static_cast<size_t>(m_current_heap_end - m_heap_region_start)) {
            // The size being requested is less than the current size, so we need to free the end of
            // the heap.

            // Validate memory state.
            size_t num_allocator_blocks;
            R_TRY(this->CheckMemoryState(
                std::addressof(num_allocator_blocks), m_heap_region_start + size,
                (m_current_heap_end - m_heap_region_start) - size, KMemoryState::All,
                KMemoryState::Normal, KMemoryPermission::All, KMemoryPermission::UserReadWrite,
                KMemoryAttribute::All, KMemoryAttribute::None));

            // Create an update allocator.
            Result allocator_result;
            KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                         m_memory_block_slab_manager,
                                                         num_allocator_blocks);
            R_TRY(allocator_result);

            // We're going to perform an update, so create a helper.
            KScopedPageTableUpdater updater(this);

            // Unmap the end of the heap.
            const size_t num_pages = ((m_current_heap_end - m_heap_region_start) - size) / PageSize;
            const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                                      DisableMergeAttribute::None};
            R_TRY(this->Operate(updater.GetPageList(), m_heap_region_start + size, num_pages, 0,
                                false, unmap_properties, OperationType::Unmap, false));

            // Release the memory from the resource limit.
            m_resource_limit->Release(Svc::LimitableResource::PhysicalMemoryMax,
                                      num_pages * PageSize);

            // Apply the memory block update.
            m_memory_block_manager.Update(std::addressof(allocator), m_heap_region_start + size,
                                          num_pages, KMemoryState::Free, KMemoryPermission::None,
                                          KMemoryAttribute::None,
                                          KMemoryBlockDisableMergeAttribute::None,
                                          size == 0 ? KMemoryBlockDisableMergeAttribute::Normal
                                                    : KMemoryBlockDisableMergeAttribute::None);

            // Update the current heap end.
            m_current_heap_end = m_heap_region_start + size;

            // Set the output.
            *out = m_heap_region_start;
            R_SUCCEED();
        } else if (size == static_cast<size_t>(m_current_heap_end - m_heap_region_start)) {
            // The size requested is exactly the current size.
            *out = m_heap_region_start;
            R_SUCCEED();
        } else {
            // We have to allocate memory. Determine how much to allocate and where while the table
            // is locked.
            cur_address = m_current_heap_end;
            allocation_size = size - (m_current_heap_end - m_heap_region_start);
        }
    }

    // Reserve memory for the heap extension.
    KScopedResourceReservation memory_reservation(
        m_resource_limit, Svc::LimitableResource::PhysicalMemoryMax, allocation_size);
    R_UNLESS(memory_reservation.Succeeded(), ResultLimitReached);

    // Allocate pages for the heap extension.
    KPageGroup pg(m_kernel, m_block_info_manager);
    R_TRY(m_kernel.MemoryManager().AllocateAndOpen(std::addressof(pg), allocation_size / PageSize,
                                                   m_allocate_option));

    // Close the opened pages when we're done with them.
    // If the mapping succeeds, each page will gain an extra reference, otherwise they will be freed
    // automatically.
    SCOPE_EXIT {
        pg.Close();
    };

    // Clear all the newly allocated pages.
    for (const auto& it : pg) {
        ClearBackingRegion(m_system, it.GetAddress(), it.GetSize(), m_heap_fill_value);
    }

    // Map the pages.
    {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        // Ensure that the heap hasn't changed since we began executing.
        ASSERT(cur_address == m_current_heap_end);

        // Check the memory state.
        size_t num_allocator_blocks;
        R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), m_current_heap_end,
                                     allocation_size, KMemoryState::All, KMemoryState::Free,
                                     KMemoryPermission::None, KMemoryPermission::None,
                                     KMemoryAttribute::None, KMemoryAttribute::None));

        // Create an update allocator.
        Result allocator_result;
        KMemoryBlockManagerUpdateAllocator allocator(
            std::addressof(allocator_result), m_memory_block_slab_manager, num_allocator_blocks);
        R_TRY(allocator_result);

        // We're going to perform an update, so create a helper.
        KScopedPageTableUpdater updater(this);

        // Map the pages.
        const size_t num_pages = allocation_size / PageSize;
        const KPageProperties map_properties = {KMemoryPermission::UserReadWrite, false, false,
                                                (m_current_heap_end == m_heap_region_start)
                                                    ? DisableMergeAttribute::DisableHead
                                                    : DisableMergeAttribute::None};
        R_TRY(this->Operate(updater.GetPageList(), m_current_heap_end, num_pages, pg,
                            map_properties, OperationType::MapGroup, false));

        // We succeeded, so commit our memory reservation.
        memory_reservation.Commit();

        // Apply the memory block update.
        m_memory_block_manager.Update(
            std::addressof(allocator), m_current_heap_end, num_pages, KMemoryState::Normal,
            KMemoryPermission::UserReadWrite, KMemoryAttribute::None,
            m_heap_region_start == m_current_heap_end ? KMemoryBlockDisableMergeAttribute::Normal
                                                      : KMemoryBlockDisableMergeAttribute::None,
            KMemoryBlockDisableMergeAttribute::None);

        // Update the current heap end.
        m_current_heap_end = m_heap_region_start + size;

        // Set the output.
        *out = m_heap_region_start;
        R_SUCCEED();
    }
}

Result KPageTableBase::SetMaxHeapSize(size_t size) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Only process page tables are allowed to set heap size.
    ASSERT(!this->IsKernel());

    m_max_heap_size = size;

    R_SUCCEED();
}

Result KPageTableBase::QueryInfo(KMemoryInfo* out_info, Svc::PageInfo* out_page_info,
                                 KProcessAddress addr) const {
    // If the address is invalid, create a fake block.
    if (!this->Contains(addr, 1)) {
        *out_info = {
            .m_address = GetInteger(m_address_space_end),
            .m_size = 0 - GetInteger(m_address_space_end),
            .m_state = static_cast<KMemoryState>(Svc::MemoryState::Inaccessible),
            .m_device_disable_merge_left_count = 0,
            .m_device_disable_merge_right_count = 0,
            .m_ipc_lock_count = 0,
            .m_device_use_count = 0,
            .m_ipc_disable_merge_count = 0,
            .m_permission = KMemoryPermission::None,
            .m_attribute = KMemoryAttribute::None,
            .m_original_permission = KMemoryPermission::None,
            .m_disable_merge_attribute = KMemoryBlockDisableMergeAttribute::None,
        };
        out_page_info->flags = 0;

        R_SUCCEED();
    }

    // Otherwise, lock the table and query.
    KScopedLightLock lk(m_general_lock);
    R_RETURN(this->QueryInfoImpl(out_info, out_page_info, addr));
}

Result KPageTableBase::QueryPhysicalAddress(Svc::lp64::PhysicalMemoryInfo* out,
                                            KProcessAddress address) const {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Align the address down to page size.
    address = Common::AlignDown(GetInteger(address), PageSize);

    // Verify that we can query the address.
    KMemoryInfo info;
    Svc::PageInfo page_info;
    R_TRY(this->QueryInfoImpl(std::addressof(info), std::addressof(page_info), address));

    // Check the memory state.
    R_TRY(this->CheckMemoryState(info, KMemoryState::FlagCanQueryPhysical,
                                 KMemoryState::FlagCanQueryPhysical,
                                 KMemoryPermission::UserReadExecute, KMemoryPermission::UserRead,
                                 KMemoryAttribute::None, KMemoryAttribute::None));

    // Prepare to traverse.
    KPhysicalAddress phys_addr;
    size_t phys_size;

    KProcessAddress virt_addr = info.GetAddress();
    KProcessAddress end_addr = info.GetEndAddress();

    // Perform traversal.
    {
        // Begin traversal.
        TraversalContext context;
        TraversalEntry next_entry;
        bool traverse_valid =
            m_impl->BeginTraversal(std::addressof(next_entry), std::addressof(context), virt_addr);
        R_UNLESS(traverse_valid, ResultInvalidCurrentMemory);

        // Set tracking variables.
        phys_addr = next_entry.phys_addr;
        phys_size = next_entry.block_size - (GetInteger(phys_addr) & (next_entry.block_size - 1));

        // Iterate.
        while (true) {
            // Continue the traversal.
            traverse_valid =
                m_impl->ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            if (!traverse_valid) {
                break;
            }

            if (next_entry.phys_addr != (phys_addr + phys_size)) {
                // Check if we're done.
                if (virt_addr <= address && address <= virt_addr + phys_size - 1) {
                    break;
                }

                // Advance.
                phys_addr = next_entry.phys_addr;
                virt_addr += next_entry.block_size;
                phys_size =
                    next_entry.block_size - (GetInteger(phys_addr) & (next_entry.block_size - 1));
            } else {
                phys_size += next_entry.block_size;
            }

            // Check if we're done.
            if (end_addr < virt_addr + phys_size) {
                break;
            }
        }
        ASSERT(virt_addr <= address && address <= virt_addr + phys_size - 1);

        // Ensure we use the right size.
        if (end_addr < virt_addr + phys_size) {
            phys_size = end_addr - virt_addr;
        }
    }

    // Set the output.
    out->physical_address = GetInteger(phys_addr);
    out->virtual_address = GetInteger(virt_addr);
    out->size = phys_size;
    R_SUCCEED();
}

Result KPageTableBase::MapIoImpl(KProcessAddress* out, PageLinkedList* page_list,
                                 KPhysicalAddress phys_addr, size_t size, KMemoryState state,
                                 KMemoryPermission perm) {
    // Check pre-conditions.
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(Common::IsAligned(GetInteger(phys_addr), PageSize));
    ASSERT(Common::IsAligned(size, PageSize));
    ASSERT(size > 0);

    R_UNLESS(phys_addr < phys_addr + size, ResultInvalidAddress);
    const size_t num_pages = size / PageSize;
    const KPhysicalAddress last = phys_addr + size - 1;

    // Get region extents.
    const KProcessAddress region_start = m_kernel_map_region_start;
    const size_t region_size = m_kernel_map_region_end - m_kernel_map_region_start;
    const size_t region_num_pages = region_size / PageSize;

    ASSERT(this->CanContain(region_start, region_size, state));

    // Locate the memory region.
    const KMemoryRegion* region = KMemoryLayout::Find(m_kernel.MemoryLayout(), phys_addr);
    R_UNLESS(region != nullptr, ResultInvalidAddress);

    ASSERT(region->Contains(GetInteger(phys_addr)));

    // Ensure that the region is mappable.
    const bool is_rw = perm == KMemoryPermission::UserReadWrite;
    while (true) {
        // Check that the region exists.
        R_UNLESS(region != nullptr, ResultInvalidAddress);

        // Check the region attributes.
        R_UNLESS(!region->IsDerivedFrom(KMemoryRegionType_Dram), ResultInvalidAddress);
        R_UNLESS(!region->HasTypeAttribute(KMemoryRegionAttr_UserReadOnly) || !is_rw,
                 ResultInvalidAddress);
        R_UNLESS(!region->HasTypeAttribute(KMemoryRegionAttr_NoUserMap), ResultInvalidAddress);

        // Check if we're done.
        if (GetInteger(last) <= region->GetLastAddress()) {
            break;
        }

        // Advance.
        region = region->GetNext();
    };

    // Select an address to map at.
    KProcessAddress addr = 0;
    {
        const size_t alignment = 4_KiB;
        const KPhysicalAddress aligned_phys =
            Common::AlignUp(GetInteger(phys_addr), alignment) + alignment - 1;
        R_UNLESS(aligned_phys > phys_addr, ResultInvalidAddress);

        const KPhysicalAddress last_aligned_paddr =
            Common::AlignDown(GetInteger(last) + 1, alignment) - 1;
        R_UNLESS((last_aligned_paddr <= last && aligned_phys <= last_aligned_paddr),
                 ResultInvalidAddress);

        addr = this->FindFreeArea(region_start, region_num_pages, num_pages, alignment, 0,
                                  this->GetNumGuardPages());
        R_UNLESS(addr != 0, ResultOutOfMemory);
    }

    // Check that we can map IO here.
    ASSERT(this->CanContain(addr, size, state));
    R_ASSERT(this->CheckMemoryState(addr, size, KMemoryState::All, KMemoryState::Free,
                                    KMemoryPermission::None, KMemoryPermission::None,
                                    KMemoryAttribute::None, KMemoryAttribute::None));

    // Perform mapping operation.
    const KPageProperties properties = {perm, state == KMemoryState::IoRegister, false,
                                        DisableMergeAttribute::DisableHead};
    R_TRY(this->Operate(page_list, addr, num_pages, phys_addr, true, properties, OperationType::Map,
                        false));

    // Set the output address.
    *out = addr;

    R_SUCCEED();
}

Result KPageTableBase::MapIo(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Map the io memory.
    KProcessAddress addr;
    R_TRY(this->MapIoImpl(std::addressof(addr), updater.GetPageList(), phys_addr, size,
                          KMemoryState::IoRegister, perm));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, size / PageSize,
                                  KMemoryState::IoRegister, perm, KMemoryAttribute::Locked,
                                  KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // We successfully mapped the pages.
    R_SUCCEED();
}

Result KPageTableBase::MapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr,
                                   size_t size, Svc::MemoryMapping mapping,
                                   Svc::MemoryPermission svc_perm) {
    const size_t num_pages = size / PageSize;

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Validate the memory state.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), dst_address, size,
                                 KMemoryState::All, KMemoryState::None, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::None,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    const KMemoryPermission perm = ConvertToKMemoryPermission(svc_perm);
    const KPageProperties properties = {perm, mapping == Svc::MemoryMapping::IoRegister,
                                        mapping == Svc::MemoryMapping::Uncached,
                                        DisableMergeAttribute::DisableHead};
    R_TRY(this->Operate(updater.GetPageList(), dst_address, num_pages, phys_addr, true, properties,
                        OperationType::Map, false));

    // Update the blocks.
    const auto state =
        mapping == Svc::MemoryMapping::Memory ? KMemoryState::IoMemory : KMemoryState::IoRegister;
    m_memory_block_manager.Update(
        std::addressof(allocator), dst_address, num_pages, state, perm, KMemoryAttribute::Locked,
        KMemoryBlockDisableMergeAttribute::Normal, KMemoryBlockDisableMergeAttribute::None);

    // We successfully mapped the pages.
    R_SUCCEED();
}

Result KPageTableBase::UnmapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr,
                                     size_t size, Svc::MemoryMapping mapping) {
    const size_t num_pages = size / PageSize;

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Validate the memory state.
    KMemoryState old_state;
    KMemoryPermission old_perm;
    KMemoryAttribute old_attr;
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(
        std::addressof(old_state), std::addressof(old_perm), std::addressof(old_attr),
        std::addressof(num_allocator_blocks), dst_address, size, KMemoryState::All,
        mapping == Svc::MemoryMapping::Memory ? KMemoryState::IoMemory : KMemoryState::IoRegister,
        KMemoryPermission::None, KMemoryPermission::None, KMemoryAttribute::All,
        KMemoryAttribute::Locked));

    // Validate that the region being unmapped corresponds to the physical range described.
    {
        // Get the impl.
        auto& impl = this->GetImpl();

        // Begin traversal.
        TraversalContext context;
        TraversalEntry next_entry;
        ASSERT(
            impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), dst_address));

        // Check that the physical region matches.
        R_UNLESS(next_entry.phys_addr == phys_addr, ResultInvalidMemoryRegion);

        // Iterate.
        for (size_t checked_size =
                 next_entry.block_size - (GetInteger(phys_addr) & (next_entry.block_size - 1));
             checked_size < size; checked_size += next_entry.block_size) {
            // Continue the traversal.
            ASSERT(impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context)));

            // Check that the physical region matches.
            R_UNLESS(next_entry.phys_addr == phys_addr + checked_size, ResultInvalidMemoryRegion);
        }
    }

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // If the region being unmapped is Memory, synchronize.
    if (mapping == Svc::MemoryMapping::Memory) {
        // Change the region to be uncached.
        const KPageProperties properties = {old_perm, false, true, DisableMergeAttribute::None};
        R_ASSERT(this->Operate(updater.GetPageList(), dst_address, num_pages, 0, false, properties,
                               OperationType::ChangePermissionsAndRefresh, false));

        // Temporarily unlock ourselves, so that other operations can occur while we flush the
        // region.
        m_general_lock.Unlock();
        SCOPE_EXIT {
            m_general_lock.Lock();
        };

        // Flush the region.
        R_ASSERT(FlushDataCache(dst_address, size));
    }

    // Perform the unmap.
    const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                              DisableMergeAttribute::None};
    R_ASSERT(this->Operate(updater.GetPageList(), dst_address, num_pages, 0, false,
                           unmap_properties, OperationType::Unmap, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), dst_address, num_pages,
                                  KMemoryState::Free, KMemoryPermission::None,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal);

    R_SUCCEED();
}

Result KPageTableBase::MapStatic(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm) {
    ASSERT(Common::IsAligned(GetInteger(phys_addr), PageSize));
    ASSERT(Common::IsAligned(size, PageSize));
    ASSERT(size > 0);
    R_UNLESS(phys_addr < phys_addr + size, ResultInvalidAddress);
    const size_t num_pages = size / PageSize;
    const KPhysicalAddress last = phys_addr + size - 1;

    // Get region extents.
    const KProcessAddress region_start = this->GetRegionAddress(KMemoryState::Static);
    const size_t region_size = this->GetRegionSize(KMemoryState::Static);
    const size_t region_num_pages = region_size / PageSize;

    // Locate the memory region.
    const KMemoryRegion* region = KMemoryLayout::Find(m_kernel.MemoryLayout(), phys_addr);
    R_UNLESS(region != nullptr, ResultInvalidAddress);

    ASSERT(region->Contains(GetInteger(phys_addr)));
    R_UNLESS(GetInteger(last) <= region->GetLastAddress(), ResultInvalidAddress);

    // Check the region attributes.
    const bool is_rw = perm == KMemoryPermission::UserReadWrite;
    R_UNLESS(region->IsDerivedFrom(KMemoryRegionType_Dram), ResultInvalidAddress);
    R_UNLESS(!region->HasTypeAttribute(KMemoryRegionAttr_NoUserMap), ResultInvalidAddress);
    R_UNLESS(!region->HasTypeAttribute(KMemoryRegionAttr_UserReadOnly) || !is_rw,
             ResultInvalidAddress);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Select an address to map at.
    KProcessAddress addr = 0;
    {
        const size_t alignment = 4_KiB;
        const KPhysicalAddress aligned_phys =
            Common::AlignUp(GetInteger(phys_addr), alignment) + alignment - 1;
        R_UNLESS(aligned_phys > phys_addr, ResultInvalidAddress);

        const KPhysicalAddress last_aligned_paddr =
            Common::AlignDown(GetInteger(last) + 1, alignment) - 1;
        R_UNLESS((last_aligned_paddr <= last && aligned_phys <= last_aligned_paddr),
                 ResultInvalidAddress);

        addr = this->FindFreeArea(region_start, region_num_pages, num_pages, alignment, 0,
                                  this->GetNumGuardPages());
        R_UNLESS(addr != 0, ResultOutOfMemory);
    }

    // Check that we can map static here.
    ASSERT(this->CanContain(addr, size, KMemoryState::Static));
    R_ASSERT(this->CheckMemoryState(addr, size, KMemoryState::All, KMemoryState::Free,
                                    KMemoryPermission::None, KMemoryPermission::None,
                                    KMemoryAttribute::None, KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    const KPageProperties properties = {perm, false, false, DisableMergeAttribute::DisableHead};
    R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, phys_addr, true, properties,
                        OperationType::Map, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, KMemoryState::Static,
                                  perm, KMemoryAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // We successfully mapped the pages.
    R_SUCCEED();
}

Result KPageTableBase::MapRegion(KMemoryRegionType region_type, KMemoryPermission perm) {
    // Get the memory region.
    const KMemoryRegion* region =
        m_kernel.MemoryLayout().GetPhysicalMemoryRegionTree().FindFirstDerived(region_type);
    R_UNLESS(region != nullptr, ResultOutOfRange);

    // Check that the region is valid.
    ASSERT(region->GetEndAddress() != 0);

    // Map the region.
    R_TRY_CATCH(this->MapStatic(region->GetAddress(), region->GetSize(), perm)){
        R_CONVERT(ResultInvalidAddress, ResultOutOfRange)} R_END_TRY_CATCH;

    R_SUCCEED();
}

Result KPageTableBase::MapPages(KProcessAddress* out_addr, size_t num_pages, size_t alignment,
                                KPhysicalAddress phys_addr, bool is_pa_valid,
                                KProcessAddress region_start, size_t region_num_pages,
                                KMemoryState state, KMemoryPermission perm) {
    ASSERT(Common::IsAligned(alignment, PageSize) && alignment >= PageSize);

    // Ensure this is a valid map request.
    R_UNLESS(this->CanContain(region_start, region_num_pages * PageSize, state),
             ResultInvalidCurrentMemory);
    R_UNLESS(num_pages < region_num_pages, ResultOutOfMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Find a random address to map at.
    KProcessAddress addr = this->FindFreeArea(region_start, region_num_pages, num_pages, alignment,
                                              0, this->GetNumGuardPages());
    R_UNLESS(addr != 0, ResultOutOfMemory);
    ASSERT(Common::IsAligned(GetInteger(addr), alignment));
    ASSERT(this->CanContain(addr, num_pages * PageSize, state));
    R_ASSERT(this->CheckMemoryState(
        addr, num_pages * PageSize, KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
        KMemoryPermission::None, KMemoryAttribute::None, KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    if (is_pa_valid) {
        const KPageProperties properties = {perm, false, false, DisableMergeAttribute::DisableHead};
        R_TRY(this->Operate(updater.GetPageList(), addr, num_pages, phys_addr, true, properties,
                            OperationType::Map, false));
    } else {
        R_TRY(this->AllocateAndMapPagesImpl(updater.GetPageList(), addr, num_pages, perm));
    }

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, state, perm,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // We successfully mapped the pages.
    *out_addr = addr;
    R_SUCCEED();
}

Result KPageTableBase::MapPages(KProcessAddress address, size_t num_pages, KMemoryState state,
                                KMemoryPermission perm) {
    // Check that the map is in range.
    const size_t size = num_pages * PageSize;
    R_UNLESS(this->CanContain(address, size, state), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), address, size,
                                 KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::None,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Map the pages.
    R_TRY(this->AllocateAndMapPagesImpl(updater.GetPageList(), address, num_pages, perm));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), address, num_pages, state, perm,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    R_SUCCEED();
}

Result KPageTableBase::UnmapPages(KProcessAddress address, size_t num_pages, KMemoryState state) {
    // Check that the unmap is in range.
    const size_t size = num_pages * PageSize;
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), address, size,
                                 KMemoryState::All, state, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::All,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform the unmap.
    const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                              DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), address, num_pages, 0, false, unmap_properties,
                        OperationType::Unmap, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), address, num_pages, KMemoryState::Free,
                                  KMemoryPermission::None, KMemoryAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal);

    R_SUCCEED();
}

Result KPageTableBase::MapPageGroup(KProcessAddress* out_addr, const KPageGroup& pg,
                                    KProcessAddress region_start, size_t region_num_pages,
                                    KMemoryState state, KMemoryPermission perm) {
    ASSERT(!this->IsLockedByCurrentThread());

    // Ensure this is a valid map request.
    const size_t num_pages = pg.GetNumPages();
    R_UNLESS(this->CanContain(region_start, region_num_pages * PageSize, state),
             ResultInvalidCurrentMemory);
    R_UNLESS(num_pages < region_num_pages, ResultOutOfMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Find a random address to map at.
    KProcessAddress addr = this->FindFreeArea(region_start, region_num_pages, num_pages, PageSize,
                                              0, this->GetNumGuardPages());
    R_UNLESS(addr != 0, ResultOutOfMemory);
    ASSERT(this->CanContain(addr, num_pages * PageSize, state));
    R_ASSERT(this->CheckMemoryState(
        addr, num_pages * PageSize, KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
        KMemoryPermission::None, KMemoryAttribute::None, KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    const KPageProperties properties = {perm, false, false, DisableMergeAttribute::DisableHead};
    R_TRY(this->MapPageGroupImpl(updater.GetPageList(), addr, pg, properties, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, state, perm,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // We successfully mapped the pages.
    *out_addr = addr;
    R_SUCCEED();
}

Result KPageTableBase::MapPageGroup(KProcessAddress addr, const KPageGroup& pg, KMemoryState state,
                                    KMemoryPermission perm) {
    ASSERT(!this->IsLockedByCurrentThread());

    // Ensure this is a valid map request.
    const size_t num_pages = pg.GetNumPages();
    const size_t size = num_pages * PageSize;
    R_UNLESS(this->CanContain(addr, size, state), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check if state allows us to map.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), addr, size,
                                 KMemoryState::All, KMemoryState::Free, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::None,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform mapping operation.
    const KPageProperties properties = {perm, false, false, DisableMergeAttribute::DisableHead};
    R_TRY(this->MapPageGroupImpl(updater.GetPageList(), addr, pg, properties, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), addr, num_pages, state, perm,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // We successfully mapped the pages.
    R_SUCCEED();
}

Result KPageTableBase::UnmapPageGroup(KProcessAddress address, const KPageGroup& pg,
                                      KMemoryState state) {
    ASSERT(!this->IsLockedByCurrentThread());

    // Ensure this is a valid unmap request.
    const size_t num_pages = pg.GetNumPages();
    const size_t size = num_pages * PageSize;
    R_UNLESS(this->CanContain(address, size, state), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check if state allows us to unmap.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), address, size,
                                 KMemoryState::All, state, KMemoryPermission::None,
                                 KMemoryPermission::None, KMemoryAttribute::All,
                                 KMemoryAttribute::None));

    // Check that the page group is valid.
    R_UNLESS(this->IsValidPageGroup(pg, address, num_pages), ResultInvalidCurrentMemory);

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Perform unmapping operation.
    const KPageProperties properties = {KMemoryPermission::None, false, false,
                                        DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), address, num_pages, 0, false, properties,
                        OperationType::Unmap, false));

    // Update the blocks.
    m_memory_block_manager.Update(std::addressof(allocator), address, num_pages, KMemoryState::Free,
                                  KMemoryPermission::None, KMemoryAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal);

    R_SUCCEED();
}

Result KPageTableBase::MakeAndOpenPageGroup(KPageGroup* out, KProcessAddress address,
                                            size_t num_pages, KMemoryState state_mask,
                                            KMemoryState state, KMemoryPermission perm_mask,
                                            KMemoryPermission perm, KMemoryAttribute attr_mask,
                                            KMemoryAttribute attr) {
    // Ensure that the page group isn't null.
    ASSERT(out != nullptr);

    // Make sure that the region we're mapping is valid for the table.
    const size_t size = num_pages * PageSize;
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check if state allows us to create the group.
    R_TRY(this->CheckMemoryState(address, size, state_mask | KMemoryState::FlagReferenceCounted,
                                 state | KMemoryState::FlagReferenceCounted, perm_mask, perm,
                                 attr_mask, attr));

    // Create a new page group for the region.
    R_TRY(this->MakePageGroup(*out, address, num_pages));

    // Open a new reference to the pages in the group.
    out->Open();

    R_SUCCEED();
}

Result KPageTableBase::InvalidateProcessDataCache(KProcessAddress address, size_t size) {
    // Check that the region is in range.
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    R_TRY(this->CheckMemoryStateContiguous(
        address, size, KMemoryState::FlagReferenceCounted, KMemoryState::FlagReferenceCounted,
        KMemoryPermission::UserReadWrite, KMemoryPermission::UserReadWrite,
        KMemoryAttribute::Uncached, KMemoryAttribute::None));

    // Get the impl.
    auto& impl = this->GetImpl();

    // Begin traversal.
    TraversalContext context;
    TraversalEntry next_entry;
    bool traverse_valid =
        impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), address);
    R_UNLESS(traverse_valid, ResultInvalidCurrentMemory);

    // Prepare tracking variables.
    KPhysicalAddress cur_addr = next_entry.phys_addr;
    size_t cur_size = next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
    size_t tot_size = cur_size;

    // Iterate.
    while (tot_size < size) {
        // Continue the traversal.
        traverse_valid =
            impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
        R_UNLESS(traverse_valid, ResultInvalidCurrentMemory);

        if (next_entry.phys_addr != (cur_addr + cur_size)) {
            // Check that the pages are linearly mapped.
            R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

            // Invalidate the block.
            if (cur_size > 0) {
                // NOTE: Nintendo does not check the result of invalidation.
                InvalidateDataCache(GetLinearMappedVirtualPointer(m_kernel, cur_addr), cur_size);
            }

            // Advance.
            cur_addr = next_entry.phys_addr;
            cur_size = next_entry.block_size;
        } else {
            cur_size += next_entry.block_size;
        }

        tot_size += next_entry.block_size;
    }

    // Ensure we use the right size for the last block.
    if (tot_size > size) {
        cur_size -= (tot_size - size);
    }

    // Check that the last block is linearly mapped.
    R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

    // Invalidate the last block.
    if (cur_size > 0) {
        // NOTE: Nintendo does not check the result of invalidation.
        InvalidateDataCache(GetLinearMappedVirtualPointer(m_kernel, cur_addr), cur_size);
    }

    R_SUCCEED();
}

Result KPageTableBase::InvalidateCurrentProcessDataCache(KProcessAddress address, size_t size) {
    // Check pre-condition: this is being called on the current process.
    ASSERT(this == std::addressof(GetCurrentProcess(m_kernel).GetPageTable().GetBasePageTable()));

    // Check that the region is in range.
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    R_TRY(this->CheckMemoryStateContiguous(
        address, size, KMemoryState::FlagReferenceCounted, KMemoryState::FlagReferenceCounted,
        KMemoryPermission::UserReadWrite, KMemoryPermission::UserReadWrite,
        KMemoryAttribute::Uncached, KMemoryAttribute::None));

    // Invalidate the data cache.
    R_RETURN(InvalidateDataCache(address, size));
}

Result KPageTableBase::ReadDebugMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                       size_t size) {
    // Lightly validate the region is in range.
    R_UNLESS(this->Contains(src_address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Require that the memory either be user readable or debuggable.
    const bool can_read = R_SUCCEEDED(this->CheckMemoryStateContiguous(
        src_address, size, KMemoryState::None, KMemoryState::None, KMemoryPermission::UserRead,
        KMemoryPermission::UserRead, KMemoryAttribute::None, KMemoryAttribute::None));
    if (!can_read) {
        const bool can_debug = R_SUCCEEDED(this->CheckMemoryStateContiguous(
            src_address, size, KMemoryState::FlagCanDebug, KMemoryState::FlagCanDebug,
            KMemoryPermission::None, KMemoryPermission::None, KMemoryAttribute::None,
            KMemoryAttribute::None));
        R_UNLESS(can_debug, ResultInvalidCurrentMemory);
    }

    // Get the impl.
    auto& impl = this->GetImpl();
    auto& dst_memory = GetCurrentMemory(m_system.Kernel());

    // Begin traversal.
    TraversalContext context;
    TraversalEntry next_entry;
    bool traverse_valid =
        impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), src_address);
    R_UNLESS(traverse_valid, ResultInvalidCurrentMemory);

    // Prepare tracking variables.
    KPhysicalAddress cur_addr = next_entry.phys_addr;
    size_t cur_size = next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
    size_t tot_size = cur_size;

    auto PerformCopy = [&]() -> Result {
        // Ensure the address is linear mapped.
        R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

        // Copy as much aligned data as we can.
        if (cur_size >= sizeof(u32)) {
            const size_t copy_size = Common::AlignDown(cur_size, sizeof(u32));
            const void* copy_src = GetLinearMappedVirtualPointer(m_kernel, cur_addr);
            FlushDataCache(copy_src, copy_size);
            R_UNLESS(dst_memory.WriteBlock(dst_address, copy_src, copy_size), ResultInvalidPointer);

            dst_address += copy_size;
            cur_addr += copy_size;
            cur_size -= copy_size;
        }

        // Copy remaining data.
        if (cur_size > 0) {
            const void* copy_src = GetLinearMappedVirtualPointer(m_kernel, cur_addr);
            FlushDataCache(copy_src, cur_size);
            R_UNLESS(dst_memory.WriteBlock(dst_address, copy_src, cur_size), ResultInvalidPointer);
        }

        R_SUCCEED();
    };

    // Iterate.
    while (tot_size < size) {
        // Continue the traversal.
        traverse_valid =
            impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
        ASSERT(traverse_valid);

        if (next_entry.phys_addr != (cur_addr + cur_size)) {
            // Perform copy.
            R_TRY(PerformCopy());

            // Advance.
            dst_address += cur_size;

            cur_addr = next_entry.phys_addr;
            cur_size = next_entry.block_size;
        } else {
            cur_size += next_entry.block_size;
        }

        tot_size += next_entry.block_size;
    }

    // Ensure we use the right size for the last block.
    if (tot_size > size) {
        cur_size -= (tot_size - size);
    }

    // Perform copy for the last block.
    R_TRY(PerformCopy());

    R_SUCCEED();
}

Result KPageTableBase::WriteDebugMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                        size_t size) {
    // Lightly validate the region is in range.
    R_UNLESS(this->Contains(dst_address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Require that the memory either be user writable or debuggable.
    const bool can_read = R_SUCCEEDED(this->CheckMemoryStateContiguous(
        dst_address, size, KMemoryState::None, KMemoryState::None, KMemoryPermission::UserReadWrite,
        KMemoryPermission::UserReadWrite, KMemoryAttribute::None, KMemoryAttribute::None));
    if (!can_read) {
        const bool can_debug = R_SUCCEEDED(this->CheckMemoryStateContiguous(
            dst_address, size, KMemoryState::FlagCanDebug, KMemoryState::FlagCanDebug,
            KMemoryPermission::None, KMemoryPermission::None, KMemoryAttribute::None,
            KMemoryAttribute::None));
        R_UNLESS(can_debug, ResultInvalidCurrentMemory);
    }

    // Get the impl.
    auto& impl = this->GetImpl();
    auto& src_memory = GetCurrentMemory(m_system.Kernel());

    // Begin traversal.
    TraversalContext context;
    TraversalEntry next_entry;
    bool traverse_valid =
        impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), dst_address);
    R_UNLESS(traverse_valid, ResultInvalidCurrentMemory);

    // Prepare tracking variables.
    KPhysicalAddress cur_addr = next_entry.phys_addr;
    size_t cur_size = next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
    size_t tot_size = cur_size;

    auto PerformCopy = [&]() -> Result {
        // Ensure the address is linear mapped.
        R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

        // Copy as much aligned data as we can.
        if (cur_size >= sizeof(u32)) {
            const size_t copy_size = Common::AlignDown(cur_size, sizeof(u32));
            void* copy_dst = GetLinearMappedVirtualPointer(m_kernel, cur_addr);
            R_UNLESS(src_memory.ReadBlock(src_address, copy_dst, copy_size),
                     ResultInvalidCurrentMemory);

            StoreDataCache(GetLinearMappedVirtualPointer(m_kernel, cur_addr), copy_size);

            src_address += copy_size;
            cur_addr += copy_size;
            cur_size -= copy_size;
        }

        // Copy remaining data.
        if (cur_size > 0) {
            void* copy_dst = GetLinearMappedVirtualPointer(m_kernel, cur_addr);
            R_UNLESS(src_memory.ReadBlock(src_address, copy_dst, cur_size),
                     ResultInvalidCurrentMemory);

            StoreDataCache(GetLinearMappedVirtualPointer(m_kernel, cur_addr), cur_size);
        }

        R_SUCCEED();
    };

    // Iterate.
    while (tot_size < size) {
        // Continue the traversal.
        traverse_valid =
            impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
        ASSERT(traverse_valid);

        if (next_entry.phys_addr != (cur_addr + cur_size)) {
            // Perform copy.
            R_TRY(PerformCopy());

            // Advance.
            src_address += cur_size;

            cur_addr = next_entry.phys_addr;
            cur_size = next_entry.block_size;
        } else {
            cur_size += next_entry.block_size;
        }

        tot_size += next_entry.block_size;
    }

    // Ensure we use the right size for the last block.
    if (tot_size > size) {
        cur_size -= (tot_size - size);
    }

    // Perform copy for the last block.
    R_TRY(PerformCopy());

    // Invalidate the instruction cache, as this svc allows modifying executable pages.
    InvalidateInstructionCache(m_kernel, this, dst_address, size);

    R_SUCCEED();
}

Result KPageTableBase::ReadIoMemoryImpl(KProcessAddress dst_addr, KPhysicalAddress phys_addr,
                                        size_t size, KMemoryState state) {
    // Check pre-conditions.
    ASSERT(this->IsLockedByCurrentThread());

    // Determine the mapping extents.
    const KPhysicalAddress map_start = Common::AlignDown(GetInteger(phys_addr), PageSize);
    const KPhysicalAddress map_end = Common::AlignUp(GetInteger(phys_addr) + size, PageSize);
    const size_t map_size = map_end - map_start;

    // Get the memory reference to write into.
    auto& dst_memory = GetCurrentMemory(m_kernel);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Temporarily map the io memory.
    KProcessAddress io_addr;
    R_TRY(this->MapIoImpl(std::addressof(io_addr), updater.GetPageList(), map_start, map_size,
                          state, KMemoryPermission::UserRead));

    // Ensure we unmap the io memory when we're done with it.
    const KPageProperties unmap_properties =
        KPageProperties{KMemoryPermission::None, false, false, DisableMergeAttribute::None};
    SCOPE_EXIT {
        R_ASSERT(this->Operate(updater.GetPageList(), io_addr, map_size / PageSize, 0, false,
                               unmap_properties, OperationType::Unmap, true));
    };

    // Read the memory.
    const KProcessAddress read_addr = io_addr + (GetInteger(phys_addr) & (PageSize - 1));
    dst_memory.CopyBlock(dst_addr, read_addr, size);

    R_SUCCEED();
}

Result KPageTableBase::WriteIoMemoryImpl(KPhysicalAddress phys_addr, KProcessAddress src_addr,
                                         size_t size, KMemoryState state) {
    // Check pre-conditions.
    ASSERT(this->IsLockedByCurrentThread());

    // Determine the mapping extents.
    const KPhysicalAddress map_start = Common::AlignDown(GetInteger(phys_addr), PageSize);
    const KPhysicalAddress map_end = Common::AlignUp(GetInteger(phys_addr) + size, PageSize);
    const size_t map_size = map_end - map_start;

    // Get the memory reference to read from.
    auto& src_memory = GetCurrentMemory(m_kernel);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Temporarily map the io memory.
    KProcessAddress io_addr;
    R_TRY(this->MapIoImpl(std::addressof(io_addr), updater.GetPageList(), map_start, map_size,
                          state, KMemoryPermission::UserReadWrite));

    // Ensure we unmap the io memory when we're done with it.
    const KPageProperties unmap_properties =
        KPageProperties{KMemoryPermission::None, false, false, DisableMergeAttribute::None};
    SCOPE_EXIT {
        R_ASSERT(this->Operate(updater.GetPageList(), io_addr, map_size / PageSize, 0, false,
                               unmap_properties, OperationType::Unmap, true));
    };

    // Write the memory.
    const KProcessAddress write_addr = io_addr + (GetInteger(phys_addr) & (PageSize - 1));
    R_UNLESS(src_memory.CopyBlock(write_addr, src_addr, size), ResultInvalidPointer);

    R_SUCCEED();
}

Result KPageTableBase::ReadDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                         size_t size, KMemoryState state) {
    // Lightly validate the range before doing anything else.
    R_UNLESS(this->Contains(src_address, size), ResultInvalidCurrentMemory);

    // We need to lock both this table, and the current process's table, so set up some aliases.
    KPageTableBase& src_page_table = *this;
    KPageTableBase& dst_page_table = GetCurrentProcess(m_kernel).GetPageTable().GetBasePageTable();

    // Acquire the table locks.
    KScopedLightLockPair lk(src_page_table.m_general_lock, dst_page_table.m_general_lock);

    // Check that the desired range is readable io memory.
    R_TRY(this->CheckMemoryStateContiguous(src_address, size, KMemoryState::All, state,
                                           KMemoryPermission::UserRead, KMemoryPermission::UserRead,
                                           KMemoryAttribute::None, KMemoryAttribute::None));

    // Read the memory.
    KProcessAddress dst = dst_address;
    const KProcessAddress last_address = src_address + size - 1;
    while (src_address <= last_address) {
        // Get the current physical address.
        KPhysicalAddress phys_addr;
        ASSERT(src_page_table.GetPhysicalAddressLocked(std::addressof(phys_addr), src_address));

        // Determine the current read size.
        const size_t cur_size =
            std::min<size_t>(last_address - src_address + 1,
                             Common::AlignDown(GetInteger(src_address) + PageSize, PageSize) -
                                 GetInteger(src_address));

        // Read.
        R_TRY(dst_page_table.ReadIoMemoryImpl(dst, phys_addr, cur_size, state));

        // Advance.
        src_address += cur_size;
        dst += cur_size;
    }

    R_SUCCEED();
}

Result KPageTableBase::WriteDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address,
                                          size_t size, KMemoryState state) {
    // Lightly validate the range before doing anything else.
    R_UNLESS(this->Contains(dst_address, size), ResultInvalidCurrentMemory);

    // We need to lock both this table, and the current process's table, so set up some aliases.
    KPageTableBase& src_page_table = *this;
    KPageTableBase& dst_page_table = GetCurrentProcess(m_kernel).GetPageTable().GetBasePageTable();

    // Acquire the table locks.
    KScopedLightLockPair lk(src_page_table.m_general_lock, dst_page_table.m_general_lock);

    // Check that the desired range is writable io memory.
    R_TRY(this->CheckMemoryStateContiguous(
        dst_address, size, KMemoryState::All, state, KMemoryPermission::UserReadWrite,
        KMemoryPermission::UserReadWrite, KMemoryAttribute::None, KMemoryAttribute::None));

    // Read the memory.
    KProcessAddress src = src_address;
    const KProcessAddress last_address = dst_address + size - 1;
    while (dst_address <= last_address) {
        // Get the current physical address.
        KPhysicalAddress phys_addr;
        ASSERT(src_page_table.GetPhysicalAddressLocked(std::addressof(phys_addr), dst_address));

        // Determine the current read size.
        const size_t cur_size =
            std::min<size_t>(last_address - dst_address + 1,
                             Common::AlignDown(GetInteger(dst_address) + PageSize, PageSize) -
                                 GetInteger(dst_address));

        // Read.
        R_TRY(dst_page_table.WriteIoMemoryImpl(phys_addr, src, cur_size, state));

        // Advance.
        dst_address += cur_size;
        src += cur_size;
    }

    R_SUCCEED();
}

Result KPageTableBase::LockForMapDeviceAddressSpace(bool* out_is_io, KProcessAddress address,
                                                    size_t size, KMemoryPermission perm,
                                                    bool is_aligned, bool check_heap) {
    // Lightly validate the range before doing anything else.
    const size_t num_pages = size / PageSize;
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    const KMemoryState test_state =
        (is_aligned ? KMemoryState::FlagCanAlignedDeviceMap : KMemoryState::FlagCanDeviceMap) |
        (check_heap ? KMemoryState::FlagReferenceCounted : KMemoryState::None);
    size_t num_allocator_blocks;
    KMemoryState old_state;
    R_TRY(this->CheckMemoryState(std::addressof(old_state), nullptr, nullptr,
                                 std::addressof(num_allocator_blocks), address, size, test_state,
                                 test_state, perm, perm,
                                 KMemoryAttribute::IpcLocked | KMemoryAttribute::Locked,
                                 KMemoryAttribute::None, KMemoryAttribute::DeviceShared));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // Update the memory blocks.
    m_memory_block_manager.UpdateLock(std::addressof(allocator), address, num_pages,
                                      &KMemoryBlock::ShareToDevice, KMemoryPermission::None);

    // Set whether the locked memory was io.
    *out_is_io =
        static_cast<Svc::MemoryState>(old_state & KMemoryState::Mask) == Svc::MemoryState::Io;

    R_SUCCEED();
}

Result KPageTableBase::LockForUnmapDeviceAddressSpace(KProcessAddress address, size_t size,
                                                      bool check_heap) {
    // Lightly validate the range before doing anything else.
    const size_t num_pages = size / PageSize;
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    const KMemoryState test_state =
        KMemoryState::FlagCanDeviceMap |
        (check_heap ? KMemoryState::FlagReferenceCounted : KMemoryState::None);
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryStateContiguous(
        std::addressof(num_allocator_blocks), address, size, test_state, test_state,
        KMemoryPermission::None, KMemoryPermission::None,
        KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked, KMemoryAttribute::DeviceShared));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // Update the memory blocks.
    const KMemoryBlockManager::MemoryBlockLockFunction lock_func =
        m_enable_device_address_space_merge
            ? &KMemoryBlock::UpdateDeviceDisableMergeStateForShare
            : &KMemoryBlock::UpdateDeviceDisableMergeStateForShareRight;
    m_memory_block_manager.UpdateLock(std::addressof(allocator), address, num_pages, lock_func,
                                      KMemoryPermission::None);

    R_SUCCEED();
}

Result KPageTableBase::UnlockForDeviceAddressSpace(KProcessAddress address, size_t size) {
    // Lightly validate the range before doing anything else.
    const size_t num_pages = size / PageSize;
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check the memory state.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryStateContiguous(
        std::addressof(num_allocator_blocks), address, size, KMemoryState::FlagCanDeviceMap,
        KMemoryState::FlagCanDeviceMap, KMemoryPermission::None, KMemoryPermission::None,
        KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked, KMemoryAttribute::DeviceShared));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // Update the memory blocks.
    m_memory_block_manager.UpdateLock(std::addressof(allocator), address, num_pages,
                                      &KMemoryBlock::UnshareToDevice, KMemoryPermission::None);

    R_SUCCEED();
}

Result KPageTableBase::UnlockForDeviceAddressSpacePartialMap(KProcessAddress address, size_t size) {
    // Lightly validate the range before doing anything else.
    const size_t num_pages = size / PageSize;
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Check memory state.
    size_t allocator_num_blocks = 0;
    R_TRY(this->CheckMemoryStateContiguous(
        std::addressof(allocator_num_blocks), address, size, KMemoryState::FlagCanDeviceMap,
        KMemoryState::FlagCanDeviceMap, KMemoryPermission::None, KMemoryPermission::None,
        KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked, KMemoryAttribute::DeviceShared));

    // Create an update allocator for the region.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, allocator_num_blocks);
    R_TRY(allocator_result);

    // Update the memory blocks.
    m_memory_block_manager.UpdateLock(
        std::addressof(allocator), address, num_pages,
        m_enable_device_address_space_merge
            ? &KMemoryBlock::UpdateDeviceDisableMergeStateForUnshare
            : &KMemoryBlock::UpdateDeviceDisableMergeStateForUnshareRight,
        KMemoryPermission::None);

    R_SUCCEED();
}

Result KPageTableBase::OpenMemoryRangeForMapDeviceAddressSpace(KPageTableBase::MemoryRange* out,
                                                               KProcessAddress address, size_t size,
                                                               KMemoryPermission perm,
                                                               bool is_aligned) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Get the range.
    const KMemoryState test_state =
        (is_aligned ? KMemoryState::FlagCanAlignedDeviceMap : KMemoryState::FlagCanDeviceMap);
    R_TRY(this->GetContiguousMemoryRangeWithState(
        out, address, size, test_state, test_state, perm, perm,
        KMemoryAttribute::IpcLocked | KMemoryAttribute::Locked, KMemoryAttribute::None));

    // We got the range, so open it.
    out->Open();

    R_SUCCEED();
}

Result KPageTableBase::OpenMemoryRangeForUnmapDeviceAddressSpace(MemoryRange* out,
                                                                 KProcessAddress address,
                                                                 size_t size) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Get the range.
    R_TRY(this->GetContiguousMemoryRangeWithState(
        out, address, size, KMemoryState::FlagCanDeviceMap, KMemoryState::FlagCanDeviceMap,
        KMemoryPermission::None, KMemoryPermission::None,
        KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked, KMemoryAttribute::DeviceShared));

    // We got the range, so open it.
    out->Open();

    R_SUCCEED();
}

Result KPageTableBase::LockForIpcUserBuffer(KPhysicalAddress* out, KProcessAddress address,
                                            size_t size) {
    R_RETURN(this->LockMemoryAndOpen(
        nullptr, out, address, size, KMemoryState::FlagCanIpcUserBuffer,
        KMemoryState::FlagCanIpcUserBuffer, KMemoryPermission::All,
        KMemoryPermission::UserReadWrite, KMemoryAttribute::All, KMemoryAttribute::None,
        static_cast<KMemoryPermission>(KMemoryPermission::NotMapped |
                                       KMemoryPermission::KernelReadWrite),
        KMemoryAttribute::Locked));
}

Result KPageTableBase::UnlockForIpcUserBuffer(KProcessAddress address, size_t size) {
    R_RETURN(this->UnlockMemory(address, size, KMemoryState::FlagCanIpcUserBuffer,
                                KMemoryState::FlagCanIpcUserBuffer, KMemoryPermission::None,
                                KMemoryPermission::None, KMemoryAttribute::All,
                                KMemoryAttribute::Locked, KMemoryPermission::UserReadWrite,
                                KMemoryAttribute::Locked, nullptr));
}

Result KPageTableBase::LockForTransferMemory(KPageGroup* out, KProcessAddress address, size_t size,
                                             KMemoryPermission perm) {
    R_RETURN(this->LockMemoryAndOpen(out, nullptr, address, size, KMemoryState::FlagCanTransfer,
                                     KMemoryState::FlagCanTransfer, KMemoryPermission::All,
                                     KMemoryPermission::UserReadWrite, KMemoryAttribute::All,
                                     KMemoryAttribute::None, perm, KMemoryAttribute::Locked));
}

Result KPageTableBase::UnlockForTransferMemory(KProcessAddress address, size_t size,
                                               const KPageGroup& pg) {
    R_RETURN(this->UnlockMemory(address, size, KMemoryState::FlagCanTransfer,
                                KMemoryState::FlagCanTransfer, KMemoryPermission::None,
                                KMemoryPermission::None, KMemoryAttribute::All,
                                KMemoryAttribute::Locked, KMemoryPermission::UserReadWrite,
                                KMemoryAttribute::Locked, std::addressof(pg)));
}

Result KPageTableBase::LockForCodeMemory(KPageGroup* out, KProcessAddress address, size_t size) {
    R_RETURN(this->LockMemoryAndOpen(
        out, nullptr, address, size, KMemoryState::FlagCanCodeMemory,
        KMemoryState::FlagCanCodeMemory, KMemoryPermission::All, KMemoryPermission::UserReadWrite,
        KMemoryAttribute::All, KMemoryAttribute::None,
        static_cast<KMemoryPermission>(KMemoryPermission::NotMapped |
                                       KMemoryPermission::KernelReadWrite),
        KMemoryAttribute::Locked));
}

Result KPageTableBase::UnlockForCodeMemory(KProcessAddress address, size_t size,
                                           const KPageGroup& pg) {
    R_RETURN(this->UnlockMemory(address, size, KMemoryState::FlagCanCodeMemory,
                                KMemoryState::FlagCanCodeMemory, KMemoryPermission::None,
                                KMemoryPermission::None, KMemoryAttribute::All,
                                KMemoryAttribute::Locked, KMemoryPermission::UserReadWrite,
                                KMemoryAttribute::Locked, std::addressof(pg)));
}

Result KPageTableBase::OpenMemoryRangeForProcessCacheOperation(MemoryRange* out,
                                                               KProcessAddress address,
                                                               size_t size) {
    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Get the range.
    R_TRY(this->GetContiguousMemoryRangeWithState(
        out, address, size, KMemoryState::FlagReferenceCounted, KMemoryState::FlagReferenceCounted,
        KMemoryPermission::UserRead, KMemoryPermission::UserRead, KMemoryAttribute::Uncached,
        KMemoryAttribute::None));

    // We got the range, so open it.
    out->Open();

    R_SUCCEED();
}

Result KPageTableBase::CopyMemoryFromLinearToUser(
    KProcessAddress dst_addr, size_t size, KProcessAddress src_addr, KMemoryState src_state_mask,
    KMemoryState src_state, KMemoryPermission src_test_perm, KMemoryAttribute src_attr_mask,
    KMemoryAttribute src_attr) {
    // Lightly validate the range before doing anything else.
    R_UNLESS(this->Contains(src_addr, size), ResultInvalidCurrentMemory);

    // Get the destination memory reference.
    auto& dst_memory = GetCurrentMemory(m_kernel);

    // Copy the memory.
    {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        // Check memory state.
        R_TRY(this->CheckMemoryStateContiguous(
            src_addr, size, src_state_mask, src_state, src_test_perm, src_test_perm,
            src_attr_mask | KMemoryAttribute::Uncached, src_attr));

        auto& impl = this->GetImpl();

        // Begin traversal.
        TraversalContext context;
        TraversalEntry next_entry;
        bool traverse_valid =
            impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), src_addr);
        ASSERT(traverse_valid);

        // Prepare tracking variables.
        KPhysicalAddress cur_addr = next_entry.phys_addr;
        size_t cur_size =
            next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
        size_t tot_size = cur_size;

        auto PerformCopy = [&]() -> Result {
            // Ensure the address is linear mapped.
            R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

            // Copy as much aligned data as we can.
            if (cur_size >= sizeof(u32)) {
                const size_t copy_size = Common::AlignDown(cur_size, sizeof(u32));
                R_UNLESS(dst_memory.WriteBlock(dst_addr,
                                               GetLinearMappedVirtualPointer(m_kernel, cur_addr),
                                               copy_size),
                         ResultInvalidCurrentMemory);

                dst_addr += copy_size;
                cur_addr += copy_size;
                cur_size -= copy_size;
            }

            // Copy remaining data.
            if (cur_size > 0) {
                R_UNLESS(dst_memory.WriteBlock(
                             dst_addr, GetLinearMappedVirtualPointer(m_kernel, cur_addr), cur_size),
                         ResultInvalidCurrentMemory);
            }

            R_SUCCEED();
        };

        // Iterate.
        while (tot_size < size) {
            // Continue the traversal.
            traverse_valid =
                impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            ASSERT(traverse_valid);

            if (next_entry.phys_addr != (cur_addr + cur_size)) {
                // Perform copy.
                R_TRY(PerformCopy());

                // Advance.
                dst_addr += cur_size;

                cur_addr = next_entry.phys_addr;
                cur_size = next_entry.block_size;
            } else {
                cur_size += next_entry.block_size;
            }

            tot_size += next_entry.block_size;
        }

        // Ensure we use the right size for the last block.
        if (tot_size > size) {
            cur_size -= (tot_size - size);
        }

        // Perform copy for the last block.
        R_TRY(PerformCopy());
    }

    R_SUCCEED();
}

Result KPageTableBase::CopyMemoryFromLinearToKernel(
    void* buffer, size_t size, KProcessAddress src_addr, KMemoryState src_state_mask,
    KMemoryState src_state, KMemoryPermission src_test_perm, KMemoryAttribute src_attr_mask,
    KMemoryAttribute src_attr) {
    // Lightly validate the range before doing anything else.
    R_UNLESS(this->Contains(src_addr, size), ResultInvalidCurrentMemory);

    // Copy the memory.
    {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        // Check memory state.
        R_TRY(this->CheckMemoryStateContiguous(
            src_addr, size, src_state_mask, src_state, src_test_perm, src_test_perm,
            src_attr_mask | KMemoryAttribute::Uncached, src_attr));

        auto& impl = this->GetImpl();

        // Begin traversal.
        TraversalContext context;
        TraversalEntry next_entry;
        bool traverse_valid =
            impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), src_addr);
        ASSERT(traverse_valid);

        // Prepare tracking variables.
        KPhysicalAddress cur_addr = next_entry.phys_addr;
        size_t cur_size =
            next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
        size_t tot_size = cur_size;

        auto PerformCopy = [&]() -> Result {
            // Ensure the address is linear mapped.
            R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

            // Copy the data.
            std::memcpy(buffer, GetLinearMappedVirtualPointer(m_kernel, cur_addr), cur_size);

            R_SUCCEED();
        };

        // Iterate.
        while (tot_size < size) {
            // Continue the traversal.
            traverse_valid =
                impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            ASSERT(traverse_valid);

            if (next_entry.phys_addr != (cur_addr + cur_size)) {
                // Perform copy.
                R_TRY(PerformCopy());

                // Advance.
                buffer = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(buffer) + cur_size);

                cur_addr = next_entry.phys_addr;
                cur_size = next_entry.block_size;
            } else {
                cur_size += next_entry.block_size;
            }

            tot_size += next_entry.block_size;
        }

        // Ensure we use the right size for the last block.
        if (tot_size > size) {
            cur_size -= (tot_size - size);
        }

        // Perform copy for the last block.
        R_TRY(PerformCopy());
    }

    R_SUCCEED();
}

Result KPageTableBase::CopyMemoryFromUserToLinear(
    KProcessAddress dst_addr, size_t size, KMemoryState dst_state_mask, KMemoryState dst_state,
    KMemoryPermission dst_test_perm, KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
    KProcessAddress src_addr) {
    // Lightly validate the range before doing anything else.
    R_UNLESS(this->Contains(dst_addr, size), ResultInvalidCurrentMemory);

    // Get the source memory reference.
    auto& src_memory = GetCurrentMemory(m_kernel);

    // Copy the memory.
    {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        // Check memory state.
        R_TRY(this->CheckMemoryStateContiguous(
            dst_addr, size, dst_state_mask, dst_state, dst_test_perm, dst_test_perm,
            dst_attr_mask | KMemoryAttribute::Uncached, dst_attr));

        auto& impl = this->GetImpl();

        // Begin traversal.
        TraversalContext context;
        TraversalEntry next_entry;
        bool traverse_valid =
            impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), dst_addr);
        ASSERT(traverse_valid);

        // Prepare tracking variables.
        KPhysicalAddress cur_addr = next_entry.phys_addr;
        size_t cur_size =
            next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
        size_t tot_size = cur_size;

        auto PerformCopy = [&]() -> Result {
            // Ensure the address is linear mapped.
            R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

            // Copy as much aligned data as we can.
            if (cur_size >= sizeof(u32)) {
                const size_t copy_size = Common::AlignDown(cur_size, sizeof(u32));
                R_UNLESS(src_memory.ReadBlock(src_addr,
                                              GetLinearMappedVirtualPointer(m_kernel, cur_addr),
                                              copy_size),
                         ResultInvalidCurrentMemory);
                src_addr += copy_size;
                cur_addr += copy_size;
                cur_size -= copy_size;
            }

            // Copy remaining data.
            if (cur_size > 0) {
                R_UNLESS(src_memory.ReadBlock(
                             src_addr, GetLinearMappedVirtualPointer(m_kernel, cur_addr), cur_size),
                         ResultInvalidCurrentMemory);
            }

            R_SUCCEED();
        };

        // Iterate.
        while (tot_size < size) {
            // Continue the traversal.
            traverse_valid =
                impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            ASSERT(traverse_valid);

            if (next_entry.phys_addr != (cur_addr + cur_size)) {
                // Perform copy.
                R_TRY(PerformCopy());

                // Advance.
                src_addr += cur_size;

                cur_addr = next_entry.phys_addr;
                cur_size = next_entry.block_size;
            } else {
                cur_size += next_entry.block_size;
            }

            tot_size += next_entry.block_size;
        }

        // Ensure we use the right size for the last block.
        if (tot_size > size) {
            cur_size -= (tot_size - size);
        }

        // Perform copy for the last block.
        R_TRY(PerformCopy());
    }

    R_SUCCEED();
}

Result KPageTableBase::CopyMemoryFromKernelToLinear(KProcessAddress dst_addr, size_t size,
                                                    KMemoryState dst_state_mask,
                                                    KMemoryState dst_state,
                                                    KMemoryPermission dst_test_perm,
                                                    KMemoryAttribute dst_attr_mask,
                                                    KMemoryAttribute dst_attr, void* buffer) {
    // Lightly validate the range before doing anything else.
    R_UNLESS(this->Contains(dst_addr, size), ResultInvalidCurrentMemory);

    // Copy the memory.
    {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        // Check memory state.
        R_TRY(this->CheckMemoryStateContiguous(
            dst_addr, size, dst_state_mask, dst_state, dst_test_perm, dst_test_perm,
            dst_attr_mask | KMemoryAttribute::Uncached, dst_attr));

        auto& impl = this->GetImpl();

        // Begin traversal.
        TraversalContext context;
        TraversalEntry next_entry;
        bool traverse_valid =
            impl.BeginTraversal(std::addressof(next_entry), std::addressof(context), dst_addr);
        ASSERT(traverse_valid);

        // Prepare tracking variables.
        KPhysicalAddress cur_addr = next_entry.phys_addr;
        size_t cur_size =
            next_entry.block_size - (GetInteger(cur_addr) & (next_entry.block_size - 1));
        size_t tot_size = cur_size;

        auto PerformCopy = [&]() -> Result {
            // Ensure the address is linear mapped.
            R_UNLESS(IsLinearMappedPhysicalAddress(cur_addr), ResultInvalidCurrentMemory);

            // Copy the data.
            std::memcpy(GetLinearMappedVirtualPointer(m_kernel, cur_addr), buffer, cur_size);

            R_SUCCEED();
        };

        // Iterate.
        while (tot_size < size) {
            // Continue the traversal.
            traverse_valid =
                impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            ASSERT(traverse_valid);

            if (next_entry.phys_addr != (cur_addr + cur_size)) {
                // Perform copy.
                R_TRY(PerformCopy());

                // Advance.
                buffer = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(buffer) + cur_size);

                cur_addr = next_entry.phys_addr;
                cur_size = next_entry.block_size;
            } else {
                cur_size += next_entry.block_size;
            }

            tot_size += next_entry.block_size;
        }

        // Ensure we use the right size for the last block.
        if (tot_size > size) {
            cur_size -= (tot_size - size);
        }

        // Perform copy for the last block.
        R_TRY(PerformCopy());
    }

    R_SUCCEED();
}

Result KPageTableBase::CopyMemoryFromHeapToHeap(
    KPageTableBase& dst_page_table, KProcessAddress dst_addr, size_t size,
    KMemoryState dst_state_mask, KMemoryState dst_state, KMemoryPermission dst_test_perm,
    KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr, KProcessAddress src_addr,
    KMemoryState src_state_mask, KMemoryState src_state, KMemoryPermission src_test_perm,
    KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
    // For convenience, alias this.
    KPageTableBase& src_page_table = *this;

    // Lightly validate the ranges before doing anything else.
    R_UNLESS(src_page_table.Contains(src_addr, size), ResultInvalidCurrentMemory);
    R_UNLESS(dst_page_table.Contains(dst_addr, size), ResultInvalidCurrentMemory);

    // Copy the memory.
    {
        // Acquire the table locks.
        KScopedLightLockPair lk(src_page_table.m_general_lock, dst_page_table.m_general_lock);

        // Check memory state.
        R_TRY(src_page_table.CheckMemoryStateContiguous(
            src_addr, size, src_state_mask, src_state, src_test_perm, src_test_perm,
            src_attr_mask | KMemoryAttribute::Uncached, src_attr));
        R_TRY(dst_page_table.CheckMemoryStateContiguous(
            dst_addr, size, dst_state_mask, dst_state, dst_test_perm, dst_test_perm,
            dst_attr_mask | KMemoryAttribute::Uncached, dst_attr));

        // Get implementations.
        auto& src_impl = src_page_table.GetImpl();
        auto& dst_impl = dst_page_table.GetImpl();

        // Prepare for traversal.
        TraversalContext src_context;
        TraversalContext dst_context;
        TraversalEntry src_next_entry;
        TraversalEntry dst_next_entry;
        bool traverse_valid;

        // Begin traversal.
        traverse_valid = src_impl.BeginTraversal(std::addressof(src_next_entry),
                                                 std::addressof(src_context), src_addr);
        ASSERT(traverse_valid);
        traverse_valid = dst_impl.BeginTraversal(std::addressof(dst_next_entry),
                                                 std::addressof(dst_context), dst_addr);
        ASSERT(traverse_valid);

        // Prepare tracking variables.
        KPhysicalAddress cur_src_block_addr = src_next_entry.phys_addr;
        KPhysicalAddress cur_dst_block_addr = dst_next_entry.phys_addr;
        size_t cur_src_size = src_next_entry.block_size -
                              (GetInteger(cur_src_block_addr) & (src_next_entry.block_size - 1));
        size_t cur_dst_size = dst_next_entry.block_size -
                              (GetInteger(cur_dst_block_addr) & (dst_next_entry.block_size - 1));

        // Adjust the initial block sizes.
        src_next_entry.block_size = cur_src_size;
        dst_next_entry.block_size = cur_dst_size;

        // Before we get any crazier, succeed if there's nothing to do.
        R_SUCCEED_IF(size == 0);

        // We're going to manage dual traversal via an offset against the total size.
        KPhysicalAddress cur_src_addr = cur_src_block_addr;
        KPhysicalAddress cur_dst_addr = cur_dst_block_addr;
        size_t cur_min_size = std::min<size_t>(cur_src_size, cur_dst_size);

        // Iterate.
        size_t ofs = 0;
        while (ofs < size) {
            // Determine how much we can copy this iteration.
            const size_t cur_copy_size = std::min<size_t>(cur_min_size, size - ofs);

            // If we need to advance the traversals, do so.
            bool updated_src = false, updated_dst = false, skip_copy = false;
            if (ofs + cur_copy_size != size) {
                if (cur_src_addr + cur_min_size == cur_src_block_addr + cur_src_size) {
                    // Continue the src traversal.
                    traverse_valid = src_impl.ContinueTraversal(std::addressof(src_next_entry),
                                                                std::addressof(src_context));
                    ASSERT(traverse_valid);

                    // Update source.
                    updated_src = cur_src_addr + cur_min_size != src_next_entry.phys_addr;
                }

                if (cur_dst_addr + cur_min_size ==
                    dst_next_entry.phys_addr + dst_next_entry.block_size) {
                    // Continue the dst traversal.
                    traverse_valid = dst_impl.ContinueTraversal(std::addressof(dst_next_entry),
                                                                std::addressof(dst_context));
                    ASSERT(traverse_valid);

                    // Update destination.
                    updated_dst = cur_dst_addr + cur_min_size != dst_next_entry.phys_addr;
                }

                // If we didn't update either of source/destination, skip the copy this iteration.
                if (!updated_src && !updated_dst) {
                    skip_copy = true;

                    // Update the source block address.
                    cur_src_block_addr = src_next_entry.phys_addr;
                }
            }

            // Do the copy, unless we're skipping it.
            if (!skip_copy) {
                // We need both ends of the copy to be heap blocks.
                R_UNLESS(IsHeapPhysicalAddress(cur_src_addr), ResultInvalidCurrentMemory);
                R_UNLESS(IsHeapPhysicalAddress(cur_dst_addr), ResultInvalidCurrentMemory);

                // Copy the data.
                std::memcpy(GetHeapVirtualPointer(m_kernel, cur_dst_addr),
                            GetHeapVirtualPointer(m_kernel, cur_src_addr), cur_copy_size);

                // Update.
                cur_src_block_addr = src_next_entry.phys_addr;
                cur_src_addr = updated_src ? cur_src_block_addr : cur_src_addr + cur_copy_size;
                cur_dst_block_addr = dst_next_entry.phys_addr;
                cur_dst_addr = updated_dst ? cur_dst_block_addr : cur_dst_addr + cur_copy_size;

                // Advance offset.
                ofs += cur_copy_size;
            }

            // Update min size.
            cur_src_size = src_next_entry.block_size;
            cur_dst_size = dst_next_entry.block_size;
            cur_min_size = std::min<size_t>(cur_src_block_addr - cur_src_addr + cur_src_size,
                                            cur_dst_block_addr - cur_dst_addr + cur_dst_size);
        }
    }

    R_SUCCEED();
}

Result KPageTableBase::CopyMemoryFromHeapToHeapWithoutCheckDestination(
    KPageTableBase& dst_page_table, KProcessAddress dst_addr, size_t size,
    KMemoryState dst_state_mask, KMemoryState dst_state, KMemoryPermission dst_test_perm,
    KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr, KProcessAddress src_addr,
    KMemoryState src_state_mask, KMemoryState src_state, KMemoryPermission src_test_perm,
    KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
    // For convenience, alias this.
    KPageTableBase& src_page_table = *this;

    // Lightly validate the ranges before doing anything else.
    R_UNLESS(src_page_table.Contains(src_addr, size), ResultInvalidCurrentMemory);
    R_UNLESS(dst_page_table.Contains(dst_addr, size), ResultInvalidCurrentMemory);

    // Copy the memory.
    {
        // Acquire the table locks.
        KScopedLightLockPair lk(src_page_table.m_general_lock, dst_page_table.m_general_lock);

        // Check memory state for source.
        R_TRY(src_page_table.CheckMemoryStateContiguous(
            src_addr, size, src_state_mask, src_state, src_test_perm, src_test_perm,
            src_attr_mask | KMemoryAttribute::Uncached, src_attr));

        // Destination state is intentionally unchecked.

        // Get implementations.
        auto& src_impl = src_page_table.GetImpl();
        auto& dst_impl = dst_page_table.GetImpl();

        // Prepare for traversal.
        TraversalContext src_context;
        TraversalContext dst_context;
        TraversalEntry src_next_entry;
        TraversalEntry dst_next_entry;
        bool traverse_valid;

        // Begin traversal.
        traverse_valid = src_impl.BeginTraversal(std::addressof(src_next_entry),
                                                 std::addressof(src_context), src_addr);
        ASSERT(traverse_valid);
        traverse_valid = dst_impl.BeginTraversal(std::addressof(dst_next_entry),
                                                 std::addressof(dst_context), dst_addr);
        ASSERT(traverse_valid);

        // Prepare tracking variables.
        KPhysicalAddress cur_src_block_addr = src_next_entry.phys_addr;
        KPhysicalAddress cur_dst_block_addr = dst_next_entry.phys_addr;
        size_t cur_src_size = src_next_entry.block_size -
                              (GetInteger(cur_src_block_addr) & (src_next_entry.block_size - 1));
        size_t cur_dst_size = dst_next_entry.block_size -
                              (GetInteger(cur_dst_block_addr) & (dst_next_entry.block_size - 1));

        // Adjust the initial block sizes.
        src_next_entry.block_size = cur_src_size;
        dst_next_entry.block_size = cur_dst_size;

        // Before we get any crazier, succeed if there's nothing to do.
        R_SUCCEED_IF(size == 0);

        // We're going to manage dual traversal via an offset against the total size.
        KPhysicalAddress cur_src_addr = cur_src_block_addr;
        KPhysicalAddress cur_dst_addr = cur_dst_block_addr;
        size_t cur_min_size = std::min<size_t>(cur_src_size, cur_dst_size);

        // Iterate.
        size_t ofs = 0;
        while (ofs < size) {
            // Determine how much we can copy this iteration.
            const size_t cur_copy_size = std::min<size_t>(cur_min_size, size - ofs);

            // If we need to advance the traversals, do so.
            bool updated_src = false, updated_dst = false, skip_copy = false;
            if (ofs + cur_copy_size != size) {
                if (cur_src_addr + cur_min_size == cur_src_block_addr + cur_src_size) {
                    // Continue the src traversal.
                    traverse_valid = src_impl.ContinueTraversal(std::addressof(src_next_entry),
                                                                std::addressof(src_context));
                    ASSERT(traverse_valid);

                    // Update source.
                    updated_src = cur_src_addr + cur_min_size != src_next_entry.phys_addr;
                }

                if (cur_dst_addr + cur_min_size ==
                    dst_next_entry.phys_addr + dst_next_entry.block_size) {
                    // Continue the dst traversal.
                    traverse_valid = dst_impl.ContinueTraversal(std::addressof(dst_next_entry),
                                                                std::addressof(dst_context));
                    ASSERT(traverse_valid);

                    // Update destination.
                    updated_dst = cur_dst_addr + cur_min_size != dst_next_entry.phys_addr;
                }

                // If we didn't update either of source/destination, skip the copy this iteration.
                if (!updated_src && !updated_dst) {
                    skip_copy = true;

                    // Update the source block address.
                    cur_src_block_addr = src_next_entry.phys_addr;
                }
            }

            // Do the copy, unless we're skipping it.
            if (!skip_copy) {
                // We need both ends of the copy to be heap blocks.
                R_UNLESS(IsHeapPhysicalAddress(cur_src_addr), ResultInvalidCurrentMemory);
                R_UNLESS(IsHeapPhysicalAddress(cur_dst_addr), ResultInvalidCurrentMemory);

                // Copy the data.
                std::memcpy(GetHeapVirtualPointer(m_kernel, cur_dst_addr),
                            GetHeapVirtualPointer(m_kernel, cur_src_addr), cur_copy_size);

                // Update.
                cur_src_block_addr = src_next_entry.phys_addr;
                cur_src_addr = updated_src ? cur_src_block_addr : cur_src_addr + cur_copy_size;
                cur_dst_block_addr = dst_next_entry.phys_addr;
                cur_dst_addr = updated_dst ? cur_dst_block_addr : cur_dst_addr + cur_copy_size;

                // Advance offset.
                ofs += cur_copy_size;
            }

            // Update min size.
            cur_src_size = src_next_entry.block_size;
            cur_dst_size = dst_next_entry.block_size;
            cur_min_size = std::min<size_t>(cur_src_block_addr - cur_src_addr + cur_src_size,
                                            cur_dst_block_addr - cur_dst_addr + cur_dst_size);
        }
    }

    R_SUCCEED();
}

Result KPageTableBase::SetupForIpcClient(PageLinkedList* page_list, size_t* out_blocks_needed,
                                         KProcessAddress address, size_t size,
                                         KMemoryPermission test_perm, KMemoryState dst_state) {
    // Validate pre-conditions.
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(test_perm == KMemoryPermission::UserReadWrite ||
           test_perm == KMemoryPermission::UserRead);

    // Check that the address is in range.
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Get the source permission.
    const auto src_perm = static_cast<KMemoryPermission>(
        (test_perm == KMemoryPermission::UserReadWrite)
            ? KMemoryPermission::KernelReadWrite | KMemoryPermission::NotMapped
            : KMemoryPermission::UserRead);

    // Get aligned extents.
    const KProcessAddress aligned_src_start = Common::AlignDown(GetInteger(address), PageSize);
    const KProcessAddress aligned_src_end = Common::AlignUp(GetInteger(address) + size, PageSize);
    const KProcessAddress mapping_src_start = Common::AlignUp(GetInteger(address), PageSize);
    const KProcessAddress mapping_src_end = Common::AlignDown(GetInteger(address) + size, PageSize);

    const auto aligned_src_last = GetInteger(aligned_src_end) - 1;
    const auto mapping_src_last = GetInteger(mapping_src_end) - 1;

    // Get the test state and attribute mask.
    KMemoryState test_state;
    KMemoryAttribute test_attr_mask;
    switch (dst_state) {
    case KMemoryState::Ipc:
        test_state = KMemoryState::FlagCanUseIpc;
        test_attr_mask =
            KMemoryAttribute::Uncached | KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked;
        break;
    case KMemoryState::NonSecureIpc:
        test_state = KMemoryState::FlagCanUseNonSecureIpc;
        test_attr_mask = KMemoryAttribute::Uncached | KMemoryAttribute::Locked;
        break;
    case KMemoryState::NonDeviceIpc:
        test_state = KMemoryState::FlagCanUseNonDeviceIpc;
        test_attr_mask = KMemoryAttribute::Uncached | KMemoryAttribute::Locked;
        break;
    default:
        R_THROW(ResultInvalidCombination);
    }

    // Ensure that on failure, we roll back appropriately.
    size_t mapped_size = 0;
    ON_RESULT_FAILURE {
        if (mapped_size > 0) {
            this->CleanupForIpcClientOnServerSetupFailure(page_list, mapping_src_start, mapped_size,
                                                          src_perm);
        }
    };

    size_t blocks_needed = 0;

    // Iterate, mapping as needed.
    KMemoryBlockManager::const_iterator it = m_memory_block_manager.FindIterator(aligned_src_start);
    while (true) {
        const KMemoryInfo info = it->GetMemoryInfo();

        // Validate the current block.
        R_TRY(this->CheckMemoryState(info, test_state, test_state, test_perm, test_perm,
                                     test_attr_mask, KMemoryAttribute::None));

        if (mapping_src_start < mapping_src_end &&
            GetInteger(mapping_src_start) < info.GetEndAddress() &&
            info.GetAddress() < GetInteger(mapping_src_end)) {
            const auto cur_start = info.GetAddress() >= GetInteger(mapping_src_start)
                                       ? info.GetAddress()
                                       : GetInteger(mapping_src_start);
            const auto cur_end = mapping_src_last >= info.GetLastAddress()
                                     ? info.GetEndAddress()
                                     : GetInteger(mapping_src_end);
            const size_t cur_size = cur_end - cur_start;

            if (info.GetAddress() < GetInteger(mapping_src_start)) {
                ++blocks_needed;
            }
            if (mapping_src_last < info.GetLastAddress()) {
                ++blocks_needed;
            }

            // Set the permissions on the block, if we need to.
            if ((info.GetPermission() & KMemoryPermission::IpcLockChangeMask) != src_perm) {
                const DisableMergeAttribute head_body_attr =
                    (GetInteger(mapping_src_start) >= info.GetAddress())
                        ? DisableMergeAttribute::DisableHeadAndBody
                        : DisableMergeAttribute::None;
                const DisableMergeAttribute tail_attr = (cur_end == GetInteger(mapping_src_end))
                                                            ? DisableMergeAttribute::DisableTail
                                                            : DisableMergeAttribute::None;
                const KPageProperties properties = {
                    src_perm, false, false,
                    static_cast<DisableMergeAttribute>(head_body_attr | tail_attr)};
                R_TRY(this->Operate(page_list, cur_start, cur_size / PageSize, 0, false, properties,
                                    OperationType::ChangePermissions, false));
            }

            // Note that we mapped this part.
            mapped_size += cur_size;
        }

        // If the block is at the end, we're done.
        if (aligned_src_last <= info.GetLastAddress()) {
            break;
        }

        // Advance.
        ++it;
        ASSERT(it != m_memory_block_manager.end());
    }

    if (out_blocks_needed != nullptr) {
        ASSERT(blocks_needed <= KMemoryBlockManagerUpdateAllocator::MaxBlocks);
        *out_blocks_needed = blocks_needed;
    }

    R_SUCCEED();
}

Result KPageTableBase::SetupForIpcServer(KProcessAddress* out_addr, size_t size,
                                         KProcessAddress src_addr, KMemoryPermission test_perm,
                                         KMemoryState dst_state, KPageTableBase& src_page_table,
                                         bool send) {
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(src_page_table.IsLockedByCurrentThread());

    // Check that we can theoretically map.
    const KProcessAddress region_start = m_alias_region_start;
    const size_t region_size = m_alias_region_end - m_alias_region_start;
    R_UNLESS(size < region_size, ResultOutOfAddressSpace);

    // Get aligned source extents.
    const KProcessAddress src_start = src_addr;
    const KProcessAddress src_end = src_addr + size;
    const KProcessAddress aligned_src_start = Common::AlignDown(GetInteger(src_start), PageSize);
    const KProcessAddress aligned_src_end = Common::AlignUp(GetInteger(src_start) + size, PageSize);
    const KProcessAddress mapping_src_start = Common::AlignUp(GetInteger(src_start), PageSize);
    const KProcessAddress mapping_src_end =
        Common::AlignDown(GetInteger(src_start) + size, PageSize);
    const size_t aligned_src_size = aligned_src_end - aligned_src_start;
    const size_t mapping_src_size =
        (mapping_src_start < mapping_src_end) ? (mapping_src_end - mapping_src_start) : 0;

    // Select a random address to map at.
    KProcessAddress dst_addr = 0;
    {
        const size_t alignment = 4_KiB;
        const size_t offset = GetInteger(aligned_src_start) & (alignment - 1);

        dst_addr =
            this->FindFreeArea(region_start, region_size / PageSize, aligned_src_size / PageSize,
                               alignment, offset, this->GetNumGuardPages());
        R_UNLESS(dst_addr != 0, ResultOutOfAddressSpace);
    }

    // Check that we can perform the operation we're about to perform.
    ASSERT(this->CanContain(dst_addr, aligned_src_size, dst_state));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Reserve space for any partial pages we allocate.
    const size_t unmapped_size = aligned_src_size - mapping_src_size;
    KScopedResourceReservation memory_reservation(
        m_resource_limit, Svc::LimitableResource::PhysicalMemoryMax, unmapped_size);
    R_UNLESS(memory_reservation.Succeeded(), ResultLimitReached);

    // Ensure that we manage page references correctly.
    KPhysicalAddress start_partial_page = 0;
    KPhysicalAddress end_partial_page = 0;
    KProcessAddress cur_mapped_addr = dst_addr;

    // If the partial pages are mapped, an extra reference will have been opened. Otherwise, they'll
    // free on scope exit.
    SCOPE_EXIT {
        if (start_partial_page != 0) {
            m_kernel.MemoryManager().Close(start_partial_page, 1);
        }
        if (end_partial_page != 0) {
            m_kernel.MemoryManager().Close(end_partial_page, 1);
        }
    };

    ON_RESULT_FAILURE {
        if (cur_mapped_addr != dst_addr) {
            const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                                      DisableMergeAttribute::None};
            R_ASSERT(this->Operate(updater.GetPageList(), dst_addr,
                                   (cur_mapped_addr - dst_addr) / PageSize, 0, false,
                                   unmap_properties, OperationType::Unmap, true));
        }
    };

    // Allocate the start page as needed.
    if (aligned_src_start < mapping_src_start) {
        start_partial_page =
            m_kernel.MemoryManager().AllocateAndOpenContinuous(1, 1, m_allocate_option);
        R_UNLESS(start_partial_page != 0, ResultOutOfMemory);
    }

    // Allocate the end page as needed.
    if (mapping_src_end < aligned_src_end &&
        (aligned_src_start < mapping_src_end || aligned_src_start == mapping_src_start)) {
        end_partial_page =
            m_kernel.MemoryManager().AllocateAndOpenContinuous(1, 1, m_allocate_option);
        R_UNLESS(end_partial_page != 0, ResultOutOfMemory);
    }

    // Get the implementation.
    auto& src_impl = src_page_table.GetImpl();

    // Get the fill value for partial pages.
    const auto fill_val = m_ipc_fill_value;

    // Begin traversal.
    TraversalContext context;
    TraversalEntry next_entry;
    bool traverse_valid = src_impl.BeginTraversal(std::addressof(next_entry),
                                                  std::addressof(context), aligned_src_start);
    ASSERT(traverse_valid);

    // Prepare tracking variables.
    KPhysicalAddress cur_block_addr = next_entry.phys_addr;
    size_t cur_block_size =
        next_entry.block_size - (GetInteger(cur_block_addr) & (next_entry.block_size - 1));
    size_t tot_block_size = cur_block_size;

    // Map the start page, if we have one.
    if (start_partial_page != 0) {
        // Ensure the page holds correct data.
        u8* const start_partial_virt = GetHeapVirtualPointer(m_kernel, start_partial_page);
        if (send) {
            const size_t partial_offset = src_start - aligned_src_start;
            size_t copy_size, clear_size;
            if (src_end < mapping_src_start) {
                copy_size = size;
                clear_size = mapping_src_start - src_end;
            } else {
                copy_size = mapping_src_start - src_start;
                clear_size = 0;
            }

            std::memset(start_partial_virt, fill_val, partial_offset);
            std::memcpy(start_partial_virt + partial_offset,
                        GetHeapVirtualPointer(m_kernel, cur_block_addr) + partial_offset,
                        copy_size);
            if (clear_size > 0) {
                std::memset(start_partial_virt + partial_offset + copy_size, fill_val, clear_size);
            }
        } else {
            std::memset(start_partial_virt, fill_val, PageSize);
        }

        // Map the page.
        const KPageProperties start_map_properties = {test_perm, false, false,
                                                      DisableMergeAttribute::DisableHead};
        R_TRY(this->Operate(updater.GetPageList(), cur_mapped_addr, 1, start_partial_page, true,
                            start_map_properties, OperationType::Map, false));

        // Update tracking extents.
        cur_mapped_addr += PageSize;
        cur_block_addr += PageSize;
        cur_block_size -= PageSize;

        // If the block's size was one page, we may need to continue traversal.
        if (cur_block_size == 0 && aligned_src_size > PageSize) {
            traverse_valid =
                src_impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            ASSERT(traverse_valid);

            cur_block_addr = next_entry.phys_addr;
            cur_block_size = next_entry.block_size;
            tot_block_size += next_entry.block_size;
        }
    }

    // Map the remaining pages.
    while (aligned_src_start + tot_block_size < mapping_src_end) {
        // Continue the traversal.
        traverse_valid =
            src_impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
        ASSERT(traverse_valid);

        // Process the block.
        if (next_entry.phys_addr != cur_block_addr + cur_block_size) {
            // Map the block we've been processing so far.
            const KPageProperties map_properties = {test_perm, false, false,
                                                    (cur_mapped_addr == dst_addr)
                                                        ? DisableMergeAttribute::DisableHead
                                                        : DisableMergeAttribute::None};
            R_TRY(this->Operate(updater.GetPageList(), cur_mapped_addr, cur_block_size / PageSize,
                                cur_block_addr, true, map_properties, OperationType::Map, false));

            // Update tracking extents.
            cur_mapped_addr += cur_block_size;
            cur_block_addr = next_entry.phys_addr;
            cur_block_size = next_entry.block_size;
        } else {
            cur_block_size += next_entry.block_size;
        }
        tot_block_size += next_entry.block_size;
    }

    // Handle the last direct-mapped page.
    if (const KProcessAddress mapped_block_end =
            aligned_src_start + tot_block_size - cur_block_size;
        mapped_block_end < mapping_src_end) {
        const size_t last_block_size = mapping_src_end - mapped_block_end;

        // Map the last block.
        const KPageProperties map_properties = {test_perm, false, false,
                                                (cur_mapped_addr == dst_addr)
                                                    ? DisableMergeAttribute::DisableHead
                                                    : DisableMergeAttribute::None};
        R_TRY(this->Operate(updater.GetPageList(), cur_mapped_addr, last_block_size / PageSize,
                            cur_block_addr, true, map_properties, OperationType::Map, false));

        // Update tracking extents.
        cur_mapped_addr += last_block_size;
        cur_block_addr += last_block_size;
        if (mapped_block_end + cur_block_size < aligned_src_end &&
            cur_block_size == last_block_size) {
            traverse_valid =
                src_impl.ContinueTraversal(std::addressof(next_entry), std::addressof(context));
            ASSERT(traverse_valid);

            cur_block_addr = next_entry.phys_addr;
        }
    }

    // Map the end page, if we have one.
    if (end_partial_page != 0) {
        // Ensure the page holds correct data.
        u8* const end_partial_virt = GetHeapVirtualPointer(m_kernel, end_partial_page);
        if (send) {
            const size_t copy_size = src_end - mapping_src_end;
            std::memcpy(end_partial_virt, GetHeapVirtualPointer(m_kernel, cur_block_addr),
                        copy_size);
            std::memset(end_partial_virt + copy_size, fill_val, PageSize - copy_size);
        } else {
            std::memset(end_partial_virt, fill_val, PageSize);
        }

        // Map the page.
        const KPageProperties map_properties = {test_perm, false, false,
                                                (cur_mapped_addr == dst_addr)
                                                    ? DisableMergeAttribute::DisableHead
                                                    : DisableMergeAttribute::None};
        R_TRY(this->Operate(updater.GetPageList(), cur_mapped_addr, 1, end_partial_page, true,
                            map_properties, OperationType::Map, false));
    }

    // Update memory blocks to reflect our changes
    m_memory_block_manager.Update(std::addressof(allocator), dst_addr, aligned_src_size / PageSize,
                                  dst_state, test_perm, KMemoryAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal,
                                  KMemoryBlockDisableMergeAttribute::None);

    // Set the output address.
    *out_addr = dst_addr + (src_start - aligned_src_start);

    // We succeeded.
    memory_reservation.Commit();
    R_SUCCEED();
}

Result KPageTableBase::SetupForIpc(KProcessAddress* out_dst_addr, size_t size,
                                   KProcessAddress src_addr, KPageTableBase& src_page_table,
                                   KMemoryPermission test_perm, KMemoryState dst_state, bool send) {
    // For convenience, alias this.
    KPageTableBase& dst_page_table = *this;

    // Acquire the table locks.
    KScopedLightLockPair lk(src_page_table.m_general_lock, dst_page_table.m_general_lock);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(std::addressof(src_page_table));

    // Perform client setup.
    size_t num_allocator_blocks;
    R_TRY(src_page_table.SetupForIpcClient(updater.GetPageList(),
                                           std::addressof(num_allocator_blocks), src_addr, size,
                                           test_perm, dst_state));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 src_page_table.m_memory_block_slab_manager,
                                                 num_allocator_blocks);
    R_TRY(allocator_result);

    // Get the mapped extents.
    const KProcessAddress src_map_start = Common::AlignUp(GetInteger(src_addr), PageSize);
    const KProcessAddress src_map_end = Common::AlignDown(GetInteger(src_addr) + size, PageSize);
    const size_t src_map_size = src_map_end - src_map_start;

    // Ensure that we clean up appropriately if we fail after this.
    const auto src_perm = static_cast<KMemoryPermission>(
        (test_perm == KMemoryPermission::UserReadWrite)
            ? KMemoryPermission::KernelReadWrite | KMemoryPermission::NotMapped
            : KMemoryPermission::UserRead);
    ON_RESULT_FAILURE {
        if (src_map_end > src_map_start) {
            src_page_table.CleanupForIpcClientOnServerSetupFailure(
                updater.GetPageList(), src_map_start, src_map_size, src_perm);
        }
    };

    // Perform server setup.
    R_TRY(dst_page_table.SetupForIpcServer(out_dst_addr, size, src_addr, test_perm, dst_state,
                                           src_page_table, send));

    // If anything was mapped, ipc-lock the pages.
    if (src_map_start < src_map_end) {
        // Get the source permission.
        src_page_table.m_memory_block_manager.UpdateLock(std::addressof(allocator), src_map_start,
                                                         (src_map_end - src_map_start) / PageSize,
                                                         &KMemoryBlock::LockForIpc, src_perm);
    }

    R_SUCCEED();
}

Result KPageTableBase::CleanupForIpcServer(KProcessAddress address, size_t size,
                                           KMemoryState dst_state) {
    // Validate the address.
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Validate the memory state.
    size_t num_allocator_blocks;
    R_TRY(this->CheckMemoryState(std::addressof(num_allocator_blocks), address, size,
                                 KMemoryState::All, dst_state, KMemoryPermission::UserRead,
                                 KMemoryPermission::UserRead, KMemoryAttribute::All,
                                 KMemoryAttribute::None));

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Get aligned extents.
    const KProcessAddress aligned_start = Common::AlignDown(GetInteger(address), PageSize);
    const KProcessAddress aligned_end = Common::AlignUp(GetInteger(address) + size, PageSize);
    const size_t aligned_size = aligned_end - aligned_start;
    const size_t aligned_num_pages = aligned_size / PageSize;

    // Unmap the pages.
    const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                              DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), aligned_start, aligned_num_pages, 0, false,
                        unmap_properties, OperationType::Unmap, false));

    // Update memory blocks.
    m_memory_block_manager.Update(std::addressof(allocator), aligned_start, aligned_num_pages,
                                  KMemoryState::None, KMemoryPermission::None,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal);

    // Release from the resource limit as relevant.
    const KProcessAddress mapping_start = Common::AlignUp(GetInteger(address), PageSize);
    const KProcessAddress mapping_end = Common::AlignDown(GetInteger(address) + size, PageSize);
    const size_t mapping_size = (mapping_start < mapping_end) ? mapping_end - mapping_start : 0;
    m_resource_limit->Release(Svc::LimitableResource::PhysicalMemoryMax,
                              aligned_size - mapping_size);

    R_SUCCEED();
}

Result KPageTableBase::CleanupForIpcClient(KProcessAddress address, size_t size,
                                           KMemoryState dst_state) {
    // Validate the address.
    R_UNLESS(this->Contains(address, size), ResultInvalidCurrentMemory);

    // Get aligned source extents.
    const KProcessAddress mapping_start = Common::AlignUp(GetInteger(address), PageSize);
    const KProcessAddress mapping_end = Common::AlignDown(GetInteger(address) + size, PageSize);
    const KProcessAddress mapping_last = mapping_end - 1;
    const size_t mapping_size = (mapping_start < mapping_end) ? (mapping_end - mapping_start) : 0;

    // If nothing was mapped, we're actually done immediately.
    R_SUCCEED_IF(mapping_size == 0);

    // Get the test state and attribute mask.
    KMemoryState test_state;
    KMemoryAttribute test_attr_mask;
    switch (dst_state) {
    case KMemoryState::Ipc:
        test_state = KMemoryState::FlagCanUseIpc;
        test_attr_mask =
            KMemoryAttribute::Uncached | KMemoryAttribute::DeviceShared | KMemoryAttribute::Locked;
        break;
    case KMemoryState::NonSecureIpc:
        test_state = KMemoryState::FlagCanUseNonSecureIpc;
        test_attr_mask = KMemoryAttribute::Uncached | KMemoryAttribute::Locked;
        break;
    case KMemoryState::NonDeviceIpc:
        test_state = KMemoryState::FlagCanUseNonDeviceIpc;
        test_attr_mask = KMemoryAttribute::Uncached | KMemoryAttribute::Locked;
        break;
    default:
        R_THROW(ResultInvalidCombination);
    }

    // Lock the table.
    // NOTE: Nintendo does this *after* creating the updater below, but this does not follow
    // convention elsewhere in KPageTableBase.
    KScopedLightLock lk(m_general_lock);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Ensure that on failure, we roll back appropriately.
    size_t mapped_size = 0;
    ON_RESULT_FAILURE {
        if (mapped_size > 0) {
            // Determine where the mapping ends.
            const auto mapped_end = GetInteger(mapping_start) + mapped_size;
            const auto mapped_last = mapped_end - 1;

            // Get current and next iterators.
            KMemoryBlockManager::const_iterator start_it =
                m_memory_block_manager.FindIterator(mapping_start);
            KMemoryBlockManager::const_iterator next_it = start_it;
            ++next_it;

            // Get the current block info.
            KMemoryInfo cur_info = start_it->GetMemoryInfo();

            // Create tracking variables.
            KProcessAddress cur_address = cur_info.GetAddress();
            size_t cur_size = cur_info.GetSize();
            bool cur_perm_eq = cur_info.GetPermission() == cur_info.GetOriginalPermission();
            bool cur_needs_set_perm = !cur_perm_eq && cur_info.GetIpcLockCount() == 1;
            bool first = cur_info.GetIpcDisableMergeCount() == 1 &&
                         False(cur_info.GetDisableMergeAttribute() &
                               KMemoryBlockDisableMergeAttribute::Locked);

            while ((GetInteger(cur_address) + cur_size - 1) < mapped_last) {
                // Check that we have a next block.
                ASSERT(next_it != m_memory_block_manager.end());

                // Get the next info.
                const KMemoryInfo next_info = next_it->GetMemoryInfo();

                // Check if we can consolidate the next block's permission set with the current one.
                const bool next_perm_eq =
                    next_info.GetPermission() == next_info.GetOriginalPermission();
                const bool next_needs_set_perm = !next_perm_eq && next_info.GetIpcLockCount() == 1;
                if (cur_perm_eq == next_perm_eq && cur_needs_set_perm == next_needs_set_perm &&
                    cur_info.GetOriginalPermission() == next_info.GetOriginalPermission()) {
                    // We can consolidate the reprotection for the current and next block into a
                    // single call.
                    cur_size += next_info.GetSize();
                } else {
                    // We have to operate on the current block.
                    if ((cur_needs_set_perm || first) && !cur_perm_eq) {
                        const KPageProperties properties = {
                            cur_info.GetPermission(), false, false,
                            first ? DisableMergeAttribute::EnableAndMergeHeadBodyTail
                                  : DisableMergeAttribute::None};
                        R_ASSERT(this->Operate(updater.GetPageList(), cur_address,
                                               cur_size / PageSize, 0, false, properties,
                                               OperationType::ChangePermissions, true));
                    }

                    // Advance.
                    cur_address = next_info.GetAddress();
                    cur_size = next_info.GetSize();
                    first = false;
                }

                // Advance.
                cur_info = next_info;
                cur_perm_eq = next_perm_eq;
                cur_needs_set_perm = next_needs_set_perm;
                ++next_it;
            }

            // Process the last block.
            if ((first || cur_needs_set_perm) && !cur_perm_eq) {
                const KPageProperties properties = {
                    cur_info.GetPermission(), false, false,
                    first ? DisableMergeAttribute::EnableAndMergeHeadBodyTail
                          : DisableMergeAttribute::None};
                R_ASSERT(this->Operate(updater.GetPageList(), cur_address, cur_size / PageSize, 0,
                                       false, properties, OperationType::ChangePermissions, true));
            }
        }
    };

    // Iterate, reprotecting as needed.
    {
        // Get current and next iterators.
        KMemoryBlockManager::const_iterator start_it =
            m_memory_block_manager.FindIterator(mapping_start);
        KMemoryBlockManager::const_iterator next_it = start_it;
        ++next_it;

        // Validate the current block.
        KMemoryInfo cur_info = start_it->GetMemoryInfo();
        R_ASSERT(this->CheckMemoryState(
            cur_info, test_state, test_state, KMemoryPermission::None, KMemoryPermission::None,
            test_attr_mask | KMemoryAttribute::IpcLocked, KMemoryAttribute::IpcLocked));

        // Create tracking variables.
        KProcessAddress cur_address = cur_info.GetAddress();
        size_t cur_size = cur_info.GetSize();
        bool cur_perm_eq = cur_info.GetPermission() == cur_info.GetOriginalPermission();
        bool cur_needs_set_perm = !cur_perm_eq && cur_info.GetIpcLockCount() == 1;
        bool first =
            cur_info.GetIpcDisableMergeCount() == 1 &&
            False(cur_info.GetDisableMergeAttribute() & KMemoryBlockDisableMergeAttribute::Locked);

        while ((cur_address + cur_size - 1) < mapping_last) {
            // Check that we have a next block.
            ASSERT(next_it != m_memory_block_manager.end());

            // Get the next info.
            const KMemoryInfo next_info = next_it->GetMemoryInfo();

            // Validate the next block.
            R_ASSERT(this->CheckMemoryState(
                next_info, test_state, test_state, KMemoryPermission::None, KMemoryPermission::None,
                test_attr_mask | KMemoryAttribute::IpcLocked, KMemoryAttribute::IpcLocked));

            // Check if we can consolidate the next block's permission set with the current one.
            const bool next_perm_eq =
                next_info.GetPermission() == next_info.GetOriginalPermission();
            const bool next_needs_set_perm = !next_perm_eq && next_info.GetIpcLockCount() == 1;
            if (cur_perm_eq == next_perm_eq && cur_needs_set_perm == next_needs_set_perm &&
                cur_info.GetOriginalPermission() == next_info.GetOriginalPermission()) {
                // We can consolidate the reprotection for the current and next block into a single
                // call.
                cur_size += next_info.GetSize();
            } else {
                // We have to operate on the current block.
                if ((cur_needs_set_perm || first) && !cur_perm_eq) {
                    const KPageProperties properties = {
                        cur_needs_set_perm ? cur_info.GetOriginalPermission()
                                           : cur_info.GetPermission(),
                        false, false,
                        first ? DisableMergeAttribute::EnableHeadAndBody
                              : DisableMergeAttribute::None};
                    R_TRY(this->Operate(updater.GetPageList(), cur_address, cur_size / PageSize, 0,
                                        false, properties, OperationType::ChangePermissions,
                                        false));
                }

                // Mark that we mapped the block.
                mapped_size += cur_size;

                // Advance.
                cur_address = next_info.GetAddress();
                cur_size = next_info.GetSize();
                first = false;
            }

            // Advance.
            cur_info = next_info;
            cur_perm_eq = next_perm_eq;
            cur_needs_set_perm = next_needs_set_perm;
            ++next_it;
        }

        // Process the last block.
        const auto lock_count =
            cur_info.GetIpcLockCount() +
            (next_it != m_memory_block_manager.end()
                 ? (next_it->GetIpcDisableMergeCount() - next_it->GetIpcLockCount())
                 : 0);
        if ((first || cur_needs_set_perm || (lock_count == 1)) && !cur_perm_eq) {
            const DisableMergeAttribute head_body_attr =
                first ? DisableMergeAttribute::EnableHeadAndBody : DisableMergeAttribute::None;
            const DisableMergeAttribute tail_attr =
                lock_count == 1 ? DisableMergeAttribute::EnableTail : DisableMergeAttribute::None;
            const KPageProperties properties = {
                cur_needs_set_perm ? cur_info.GetOriginalPermission() : cur_info.GetPermission(),
                false, false, static_cast<DisableMergeAttribute>(head_body_attr | tail_attr)};
            R_TRY(this->Operate(updater.GetPageList(), cur_address, cur_size / PageSize, 0, false,
                                properties, OperationType::ChangePermissions, false));
        }
    }

    // Create an update allocator.
    // NOTE: Guaranteed zero blocks needed here.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, 0);
    R_TRY(allocator_result);

    // Unlock the pages.
    m_memory_block_manager.UpdateLock(std::addressof(allocator), mapping_start,
                                      mapping_size / PageSize, &KMemoryBlock::UnlockForIpc,
                                      KMemoryPermission::None);

    R_SUCCEED();
}

void KPageTableBase::CleanupForIpcClientOnServerSetupFailure(PageLinkedList* page_list,
                                                             KProcessAddress address, size_t size,
                                                             KMemoryPermission prot_perm) {
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(Common::IsAligned(GetInteger(address), PageSize));
    ASSERT(Common::IsAligned(size, PageSize));

    // Get the mapped extents.
    const KProcessAddress src_map_start = address;
    const KProcessAddress src_map_end = address + size;
    const KProcessAddress src_map_last = src_map_end - 1;

    // This function is only invoked when there's something to do.
    ASSERT(src_map_end > src_map_start);

    // Iterate over blocks, fixing permissions.
    KMemoryBlockManager::const_iterator it = m_memory_block_manager.FindIterator(address);
    while (true) {
        const KMemoryInfo info = it->GetMemoryInfo();

        const auto cur_start = info.GetAddress() >= GetInteger(src_map_start)
                                   ? info.GetAddress()
                                   : GetInteger(src_map_start);
        const auto cur_end =
            src_map_last <= info.GetLastAddress() ? src_map_end : info.GetEndAddress();

        // If we can, fix the protections on the block.
        if ((info.GetIpcLockCount() == 0 &&
             (info.GetPermission() & KMemoryPermission::IpcLockChangeMask) != prot_perm) ||
            (info.GetIpcLockCount() != 0 &&
             (info.GetOriginalPermission() & KMemoryPermission::IpcLockChangeMask) != prot_perm)) {
            // Check if we actually need to fix the protections on the block.
            if (cur_end == src_map_end || info.GetAddress() <= GetInteger(src_map_start) ||
                (info.GetPermission() & KMemoryPermission::IpcLockChangeMask) != prot_perm) {
                const bool start_nc = (info.GetAddress() == GetInteger(src_map_start))
                                          ? (False(info.GetDisableMergeAttribute() &
                                                   (KMemoryBlockDisableMergeAttribute::Locked |
                                                    KMemoryBlockDisableMergeAttribute::IpcLeft)))
                                          : info.GetAddress() <= GetInteger(src_map_start);

                const DisableMergeAttribute head_body_attr =
                    start_nc ? DisableMergeAttribute::EnableHeadAndBody
                             : DisableMergeAttribute::None;
                DisableMergeAttribute tail_attr;
                if (cur_end == src_map_end && info.GetEndAddress() == src_map_end) {
                    auto next_it = it;
                    ++next_it;

                    const auto lock_count =
                        info.GetIpcLockCount() +
                        (next_it != m_memory_block_manager.end()
                             ? (next_it->GetIpcDisableMergeCount() - next_it->GetIpcLockCount())
                             : 0);
                    tail_attr = lock_count == 0 ? DisableMergeAttribute::EnableTail
                                                : DisableMergeAttribute::None;
                } else {
                    tail_attr = DisableMergeAttribute::None;
                }

                const KPageProperties properties = {
                    info.GetPermission(), false, false,
                    static_cast<DisableMergeAttribute>(head_body_attr | tail_attr)};
                R_ASSERT(this->Operate(page_list, cur_start, (cur_end - cur_start) / PageSize, 0,
                                       false, properties, OperationType::ChangePermissions, true));
            }
        }

        // If we're past the end of the region, we're done.
        if (src_map_last <= info.GetLastAddress()) {
            break;
        }

        // Advance.
        ++it;
        ASSERT(it != m_memory_block_manager.end());
    }
}

Result KPageTableBase::MapPhysicalMemory(KProcessAddress address, size_t size) {
    // Lock the physical memory lock.
    KScopedLightLock phys_lk(m_map_physical_memory_lock);

    // Calculate the last address for convenience.
    const KProcessAddress last_address = address + size - 1;

    // Define iteration variables.
    KProcessAddress cur_address;
    size_t mapped_size;

    // The entire mapping process can be retried.
    while (true) {
        // Check if the memory is already mapped.
        {
            // Lock the table.
            KScopedLightLock lk(m_general_lock);

            // Iterate over the memory.
            cur_address = address;
            mapped_size = 0;

            auto it = m_memory_block_manager.FindIterator(cur_address);
            while (true) {
                // Check that the iterator is valid.
                ASSERT(it != m_memory_block_manager.end());

                // Get the memory info.
                const KMemoryInfo info = it->GetMemoryInfo();

                // Check if we're done.
                if (last_address <= info.GetLastAddress()) {
                    if (info.GetState() != KMemoryState::Free) {
                        mapped_size += (last_address + 1 - cur_address);
                    }
                    break;
                }

                // Track the memory if it's mapped.
                if (info.GetState() != KMemoryState::Free) {
                    mapped_size += KProcessAddress(info.GetEndAddress()) - cur_address;
                }

                // Advance.
                cur_address = info.GetEndAddress();
                ++it;
            }

            // If the size mapped is the size requested, we've nothing to do.
            R_SUCCEED_IF(size == mapped_size);
        }

        // Allocate and map the memory.
        {
            // Reserve the memory from the process resource limit.
            KScopedResourceReservation memory_reservation(
                m_resource_limit, Svc::LimitableResource::PhysicalMemoryMax, size - mapped_size);
            R_UNLESS(memory_reservation.Succeeded(), ResultLimitReached);

            // Allocate pages for the new memory.
            KPageGroup pg(m_kernel, m_block_info_manager);
            R_TRY(m_kernel.MemoryManager().AllocateForProcess(
                std::addressof(pg), (size - mapped_size) / PageSize, m_allocate_option,
                GetCurrentProcess(m_kernel).GetId(), m_heap_fill_value));

            // If we fail in the next bit (or retry), we need to cleanup the pages.
            auto pg_guard = SCOPE_GUARD {
                pg.OpenFirst();
                pg.Close();
            };

            // Map the memory.
            {
                // Lock the table.
                KScopedLightLock lk(m_general_lock);

                size_t num_allocator_blocks = 0;

                // Verify that nobody has mapped memory since we first checked.
                {
                    // Iterate over the memory.
                    size_t checked_mapped_size = 0;
                    cur_address = address;

                    auto it = m_memory_block_manager.FindIterator(cur_address);
                    while (true) {
                        // Check that the iterator is valid.
                        ASSERT(it != m_memory_block_manager.end());

                        // Get the memory info.
                        const KMemoryInfo info = it->GetMemoryInfo();

                        const bool is_free = info.GetState() == KMemoryState::Free;
                        if (is_free) {
                            if (info.GetAddress() < GetInteger(address)) {
                                ++num_allocator_blocks;
                            }
                            if (last_address < info.GetLastAddress()) {
                                ++num_allocator_blocks;
                            }
                        }

                        // Check if we're done.
                        if (last_address <= info.GetLastAddress()) {
                            if (!is_free) {
                                checked_mapped_size += (last_address + 1 - cur_address);
                            }
                            break;
                        }

                        // Track the memory if it's mapped.
                        if (!is_free) {
                            checked_mapped_size +=
                                KProcessAddress(info.GetEndAddress()) - cur_address;
                        }

                        // Advance.
                        cur_address = info.GetEndAddress();
                        ++it;
                    }

                    // If the size now isn't what it was before, somebody mapped or unmapped
                    // concurrently. If this happened, retry.
                    if (mapped_size != checked_mapped_size) {
                        continue;
                    }
                }

                // Create an update allocator.
                ASSERT(num_allocator_blocks <= KMemoryBlockManagerUpdateAllocator::MaxBlocks);
                Result allocator_result;
                KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                             m_memory_block_slab_manager,
                                                             num_allocator_blocks);
                R_TRY(allocator_result);

                // We're going to perform an update, so create a helper.
                KScopedPageTableUpdater updater(this);

                // Prepare to iterate over the memory.
                auto pg_it = pg.begin();
                KPhysicalAddress pg_phys_addr = pg_it->GetAddress();
                size_t pg_pages = pg_it->GetNumPages();

                // Reset the current tracking address, and make sure we clean up on failure.
                pg_guard.Cancel();
                cur_address = address;
                ON_RESULT_FAILURE {
                    if (cur_address > address) {
                        const KProcessAddress last_unmap_address = cur_address - 1;

                        // Iterate, unmapping the pages.
                        cur_address = address;

                        auto it = m_memory_block_manager.FindIterator(cur_address);
                        while (true) {
                            // Check that the iterator is valid.
                            ASSERT(it != m_memory_block_manager.end());

                            // Get the memory info.
                            const KMemoryInfo info = it->GetMemoryInfo();

                            // If the memory state is free, we mapped it and need to unmap it.
                            if (info.GetState() == KMemoryState::Free) {
                                // Determine the range to unmap.
                                const KPageProperties unmap_properties = {
                                    KMemoryPermission::None, false, false,
                                    DisableMergeAttribute::None};
                                const size_t cur_pages =
                                    std::min(KProcessAddress(info.GetEndAddress()) - cur_address,
                                             last_unmap_address + 1 - cur_address) /
                                    PageSize;

                                // Unmap.
                                R_ASSERT(this->Operate(updater.GetPageList(), cur_address,
                                                       cur_pages, 0, false, unmap_properties,
                                                       OperationType::UnmapPhysical, true));
                            }

                            // Check if we're done.
                            if (last_unmap_address <= info.GetLastAddress()) {
                                break;
                            }

                            // Advance.
                            cur_address = info.GetEndAddress();
                            ++it;
                        }
                    }

                    // Release any remaining unmapped memory.
                    m_kernel.MemoryManager().OpenFirst(pg_phys_addr, pg_pages);
                    m_kernel.MemoryManager().Close(pg_phys_addr, pg_pages);
                    for (++pg_it; pg_it != pg.end(); ++pg_it) {
                        m_kernel.MemoryManager().OpenFirst(pg_it->GetAddress(),
                                                           pg_it->GetNumPages());
                        m_kernel.MemoryManager().Close(pg_it->GetAddress(), pg_it->GetNumPages());
                    }
                };

                auto it = m_memory_block_manager.FindIterator(cur_address);
                while (true) {
                    // Check that the iterator is valid.
                    ASSERT(it != m_memory_block_manager.end());

                    // Get the memory info.
                    const KMemoryInfo info = it->GetMemoryInfo();

                    // If it's unmapped, we need to map it.
                    if (info.GetState() == KMemoryState::Free) {
                        // Determine the range to map.
                        const KPageProperties map_properties = {
                            KMemoryPermission::UserReadWrite, false, false,
                            cur_address == this->GetAliasRegionStart()
                                ? DisableMergeAttribute::DisableHead
                                : DisableMergeAttribute::None};
                        size_t map_pages =
                            std::min(KProcessAddress(info.GetEndAddress()) - cur_address,
                                     last_address + 1 - cur_address) /
                            PageSize;

                        // While we have pages to map, map them.
                        {
                            // Create a page group for the current mapping range.
                            KPageGroup cur_pg(m_kernel, m_block_info_manager);
                            {
                                ON_RESULT_FAILURE_2 {
                                    cur_pg.OpenFirst();
                                    cur_pg.Close();
                                };

                                size_t remain_pages = map_pages;
                                while (remain_pages > 0) {
                                    // Check if we're at the end of the physical block.
                                    if (pg_pages == 0) {
                                        // Ensure there are more pages to map.
                                        ASSERT(pg_it != pg.end());

                                        // Advance our physical block.
                                        ++pg_it;
                                        pg_phys_addr = pg_it->GetAddress();
                                        pg_pages = pg_it->GetNumPages();
                                    }

                                    // Add whatever we can to the current block.
                                    const size_t cur_pages = std::min(pg_pages, remain_pages);
                                    R_TRY(cur_pg.AddBlock(pg_phys_addr +
                                                              ((pg_pages - cur_pages) * PageSize),
                                                          cur_pages));

                                    // Advance.
                                    remain_pages -= cur_pages;
                                    pg_pages -= cur_pages;
                                }
                            }

                            // Map the papges.
                            R_TRY(this->Operate(updater.GetPageList(), cur_address, map_pages,
                                                cur_pg, map_properties,
                                                OperationType::MapFirstGroupPhysical, false));
                        }
                    }

                    // Check if we're done.
                    if (last_address <= info.GetLastAddress()) {
                        break;
                    }

                    // Advance.
                    cur_address = info.GetEndAddress();
                    ++it;
                }

                // We succeeded, so commit the memory reservation.
                memory_reservation.Commit();

                // Increase our tracked mapped size.
                m_mapped_physical_memory_size += (size - mapped_size);

                // Update the relevant memory blocks.
                m_memory_block_manager.UpdateIfMatch(
                    std::addressof(allocator), address, size / PageSize, KMemoryState::Free,
                    KMemoryPermission::None, KMemoryAttribute::None, KMemoryState::Normal,
                    KMemoryPermission::UserReadWrite, KMemoryAttribute::None,
                    address == this->GetAliasRegionStart()
                        ? KMemoryBlockDisableMergeAttribute::Normal
                        : KMemoryBlockDisableMergeAttribute::None,
                    KMemoryBlockDisableMergeAttribute::None);

                R_SUCCEED();
            }
        }
    }
}

Result KPageTableBase::UnmapPhysicalMemory(KProcessAddress address, size_t size) {
    // Lock the physical memory lock.
    KScopedLightLock phys_lk(m_map_physical_memory_lock);

    // Lock the table.
    KScopedLightLock lk(m_general_lock);

    // Calculate the last address for convenience.
    const KProcessAddress last_address = address + size - 1;

    // Define iteration variables.
    KProcessAddress map_start_address = 0;
    KProcessAddress map_last_address = 0;

    KProcessAddress cur_address;
    size_t mapped_size;
    size_t num_allocator_blocks = 0;

    // Check if the memory is mapped.
    {
        // Iterate over the memory.
        cur_address = address;
        mapped_size = 0;

        auto it = m_memory_block_manager.FindIterator(cur_address);
        while (true) {
            // Check that the iterator is valid.
            ASSERT(it != m_memory_block_manager.end());

            // Get the memory info.
            const KMemoryInfo info = it->GetMemoryInfo();

            // Verify the memory's state.
            const bool is_normal = info.GetState() == KMemoryState::Normal &&
                                   info.GetAttribute() == KMemoryAttribute::None;
            const bool is_free = info.GetState() == KMemoryState::Free;
            R_UNLESS(is_normal || is_free, ResultInvalidCurrentMemory);

            if (is_normal) {
                R_UNLESS(info.GetAttribute() == KMemoryAttribute::None, ResultInvalidCurrentMemory);

                if (map_start_address == 0) {
                    map_start_address = cur_address;
                }
                map_last_address =
                    (last_address >= info.GetLastAddress()) ? info.GetLastAddress() : last_address;

                if (info.GetAddress() < GetInteger(address)) {
                    ++num_allocator_blocks;
                }
                if (last_address < info.GetLastAddress()) {
                    ++num_allocator_blocks;
                }

                mapped_size += (map_last_address + 1 - cur_address);
            }

            // Check if we're done.
            if (last_address <= info.GetLastAddress()) {
                break;
            }

            // Advance.
            cur_address = info.GetEndAddress();
            ++it;
        }

        // If there's nothing mapped, we've nothing to do.
        R_SUCCEED_IF(mapped_size == 0);
    }

    // Create an update allocator.
    ASSERT(num_allocator_blocks <= KMemoryBlockManagerUpdateAllocator::MaxBlocks);
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Separate the mapping.
    const KPageProperties sep_properties = {KMemoryPermission::None, false, false,
                                            DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), map_start_address,
                        (map_last_address + 1 - map_start_address) / PageSize, 0, false,
                        sep_properties, OperationType::Separate, false));

    // Reset the current tracking address, and make sure we clean up on failure.
    cur_address = address;

    // Iterate over the memory, unmapping as we go.
    auto it = m_memory_block_manager.FindIterator(cur_address);

    const auto clear_merge_attr =
        (it->GetState() == KMemoryState::Normal &&
         it->GetAddress() == this->GetAliasRegionStart() && it->GetAddress() == address)
            ? KMemoryBlockDisableMergeAttribute::Normal
            : KMemoryBlockDisableMergeAttribute::None;

    while (true) {
        // Check that the iterator is valid.
        ASSERT(it != m_memory_block_manager.end());

        // Get the memory info.
        const KMemoryInfo info = it->GetMemoryInfo();

        // If the memory state is normal, we need to unmap it.
        if (info.GetState() == KMemoryState::Normal) {
            // Determine the range to unmap.
            const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                                      DisableMergeAttribute::None};
            const size_t cur_pages = std::min(KProcessAddress(info.GetEndAddress()) - cur_address,
                                              last_address + 1 - cur_address) /
                                     PageSize;

            // Unmap.
            R_ASSERT(this->Operate(updater.GetPageList(), cur_address, cur_pages, 0, false,
                                   unmap_properties, OperationType::UnmapPhysical, false));
        }

        // Check if we're done.
        if (last_address <= info.GetLastAddress()) {
            break;
        }

        // Advance.
        cur_address = info.GetEndAddress();
        ++it;
    }

    // Release the memory resource.
    m_mapped_physical_memory_size -= mapped_size;
    m_resource_limit->Release(Svc::LimitableResource::PhysicalMemoryMax, mapped_size);

    // Update memory blocks.
    m_memory_block_manager.Update(std::addressof(allocator), address, size / PageSize,
                                  KMemoryState::Free, KMemoryPermission::None,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::None,
                                  clear_merge_attr);

    // We succeeded.
    R_SUCCEED();
}

Result KPageTableBase::MapPhysicalMemoryUnsafe(KProcessAddress address, size_t size) {
    UNIMPLEMENTED();
    R_THROW(ResultNotImplemented);
}

Result KPageTableBase::UnmapPhysicalMemoryUnsafe(KProcessAddress address, size_t size) {
    UNIMPLEMENTED();
    R_THROW(ResultNotImplemented);
}

Result KPageTableBase::UnmapProcessMemory(KProcessAddress dst_address, size_t size,
                                          KPageTableBase& src_page_table,
                                          KProcessAddress src_address) {
    // We need to lock both this table, and the current process's table, so set up an alias.
    KPageTableBase& dst_page_table = *this;

    // Acquire the table locks.
    KScopedLightLockPair lk(src_page_table.m_general_lock, dst_page_table.m_general_lock);

    // Check that the memory is mapped in the destination process.
    size_t num_allocator_blocks;
    R_TRY(dst_page_table.CheckMemoryState(
        std::addressof(num_allocator_blocks), dst_address, size, KMemoryState::All,
        KMemoryState::SharedCode, KMemoryPermission::UserReadWrite,
        KMemoryPermission::UserReadWrite, KMemoryAttribute::All, KMemoryAttribute::None));

    // Check that the memory is mapped in the source process.
    R_TRY(src_page_table.CheckMemoryState(src_address, size, KMemoryState::FlagCanMapProcess,
                                          KMemoryState::FlagCanMapProcess, KMemoryPermission::None,
                                          KMemoryPermission::None, KMemoryAttribute::All,
                                          KMemoryAttribute::None));

    // Validate that the memory ranges are compatible.
    {
        // Define a helper type.
        struct ContiguousRangeInfo {
        public:
            KPageTableBase& m_pt;
            TraversalContext m_context;
            TraversalEntry m_entry;
            KPhysicalAddress m_phys_addr;
            size_t m_cur_size;
            size_t m_remaining_size;

        public:
            ContiguousRangeInfo(KPageTableBase& pt, KProcessAddress address, size_t size)
                : m_pt(pt), m_remaining_size(size) {
                // Begin a traversal.
                ASSERT(m_pt.GetImpl().BeginTraversal(std::addressof(m_entry),
                                                     std::addressof(m_context), address));

                // Setup tracking fields.
                m_phys_addr = m_entry.phys_addr;
                m_cur_size = std::min<size_t>(
                    m_remaining_size,
                    m_entry.block_size - (GetInteger(m_phys_addr) & (m_entry.block_size - 1)));

                // Consume the whole contiguous block.
                this->DetermineContiguousBlockExtents();
            }

            void ContinueTraversal() {
                // Update our remaining size.
                m_remaining_size = m_remaining_size - m_cur_size;

                // Update our tracking fields.
                if (m_remaining_size > 0) {
                    m_phys_addr = m_entry.phys_addr;
                    m_cur_size = std::min<size_t>(m_remaining_size, m_entry.block_size);

                    // Consume the whole contiguous block.
                    this->DetermineContiguousBlockExtents();
                }
            }

        private:
            void DetermineContiguousBlockExtents() {
                // Continue traversing until we're not contiguous, or we have enough.
                while (m_cur_size < m_remaining_size) {
                    ASSERT(m_pt.GetImpl().ContinueTraversal(std::addressof(m_entry),
                                                            std::addressof(m_context)));

                    // If we're not contiguous, we're done.
                    if (m_entry.phys_addr != m_phys_addr + m_cur_size) {
                        break;
                    }

                    // Update our current size.
                    m_cur_size = std::min(m_remaining_size, m_cur_size + m_entry.block_size);
                }
            }
        };

        // Create ranges for both tables.
        ContiguousRangeInfo src_range(src_page_table, src_address, size);
        ContiguousRangeInfo dst_range(dst_page_table, dst_address, size);

        // Validate the ranges.
        while (src_range.m_remaining_size > 0 && dst_range.m_remaining_size > 0) {
            R_UNLESS(src_range.m_phys_addr == dst_range.m_phys_addr, ResultInvalidMemoryRegion);
            R_UNLESS(src_range.m_cur_size == dst_range.m_cur_size, ResultInvalidMemoryRegion);

            src_range.ContinueTraversal();
            dst_range.ContinueTraversal();
        }
    }

    // We no longer need to hold our lock on the source page table.
    lk.TryUnlockHalf(src_page_table.m_general_lock);

    // Create an update allocator.
    Result allocator_result;
    KMemoryBlockManagerUpdateAllocator allocator(std::addressof(allocator_result),
                                                 m_memory_block_slab_manager, num_allocator_blocks);
    R_TRY(allocator_result);

    // We're going to perform an update, so create a helper.
    KScopedPageTableUpdater updater(this);

    // Unmap the memory.
    const size_t num_pages = size / PageSize;
    const KPageProperties unmap_properties = {KMemoryPermission::None, false, false,
                                              DisableMergeAttribute::None};
    R_TRY(this->Operate(updater.GetPageList(), dst_address, num_pages, 0, false, unmap_properties,
                        OperationType::Unmap, false));

    // Apply the memory block update.
    m_memory_block_manager.Update(std::addressof(allocator), dst_address, num_pages,
                                  KMemoryState::Free, KMemoryPermission::None,
                                  KMemoryAttribute::None, KMemoryBlockDisableMergeAttribute::None,
                                  KMemoryBlockDisableMergeAttribute::Normal);

    R_SUCCEED();
}

Result KPageTableBase::Operate(PageLinkedList* page_list, KProcessAddress virt_addr,
                               size_t num_pages, KPhysicalAddress phys_addr, bool is_pa_valid,
                               const KPageProperties properties, OperationType operation,
                               bool reuse_ll) {
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(num_pages > 0);
    ASSERT(Common::IsAligned(GetInteger(virt_addr), PageSize));
    ASSERT(this->ContainsPages(virt_addr, num_pages));

    // As we don't allocate page entries in guest memory, we don't need to allocate them from
    // or free them to the page list, and so it goes unused (along with page properties).

    switch (operation) {
    case OperationType::Unmap:
    case OperationType::UnmapPhysical: {
        const bool separate_heap = operation == OperationType::UnmapPhysical;

        // Ensure that any pages we track are closed on exit.
        KPageGroup pages_to_close(m_kernel, this->GetBlockInfoManager());
        SCOPE_EXIT {
            pages_to_close.CloseAndReset();
        };

        // Make a page group representing the region to unmap.
        this->MakePageGroup(pages_to_close, virt_addr, num_pages);

        // Unmap.
        m_memory->UnmapRegion(*m_impl, virt_addr, num_pages * PageSize, separate_heap);

        R_SUCCEED();
    }
    case OperationType::Map: {
        ASSERT(virt_addr != 0);
        ASSERT(Common::IsAligned(GetInteger(virt_addr), PageSize));
        m_memory->MapMemoryRegion(*m_impl, virt_addr, num_pages * PageSize, phys_addr,
                                  ConvertToMemoryPermission(properties.perm), false);

        // Open references to pages, if we should.
        if (this->IsHeapPhysicalAddress(phys_addr)) {
            m_kernel.MemoryManager().Open(phys_addr, num_pages);
        }

        R_SUCCEED();
    }
    case OperationType::Separate: {
        // TODO: Unimplemented.
        R_SUCCEED();
    }
    case OperationType::ChangePermissions:
    case OperationType::ChangePermissionsAndRefresh:
    case OperationType::ChangePermissionsAndRefreshAndFlush: {
        m_memory->ProtectRegion(*m_impl, virt_addr, num_pages * PageSize,
                                ConvertToMemoryPermission(properties.perm));
        R_SUCCEED();
    }
    default:
        UNREACHABLE();
    }
}

Result KPageTableBase::Operate(PageLinkedList* page_list, KProcessAddress virt_addr,
                               size_t num_pages, const KPageGroup& page_group,
                               const KPageProperties properties, OperationType operation,
                               bool reuse_ll) {
    ASSERT(this->IsLockedByCurrentThread());
    ASSERT(Common::IsAligned(GetInteger(virt_addr), PageSize));
    ASSERT(num_pages > 0);
    ASSERT(num_pages == page_group.GetNumPages());

    // As we don't allocate page entries in guest memory, we don't need to allocate them from
    // the page list, and so it goes unused (along with page properties).

    switch (operation) {
    case OperationType::MapGroup:
    case OperationType::MapFirstGroup:
    case OperationType::MapFirstGroupPhysical: {
        const bool separate_heap = operation == OperationType::MapFirstGroupPhysical;

        // We want to maintain a new reference to every page in the group.
        KScopedPageGroup spg(page_group, operation == OperationType::MapGroup);

        for (const auto& node : page_group) {
            const size_t size{node.GetNumPages() * PageSize};

            // Map the pages.
            m_memory->MapMemoryRegion(*m_impl, virt_addr, size, node.GetAddress(),
                                      ConvertToMemoryPermission(properties.perm), separate_heap);

            virt_addr += size;
        }

        // We succeeded! We want to persist the reference to the pages.
        spg.CancelClose();

        R_SUCCEED();
    }
    default:
        UNREACHABLE();
    }
}

void KPageTableBase::FinalizeUpdate(PageLinkedList* page_list) {
    while (page_list->Peek()) {
        [[maybe_unused]] auto page = page_list->Pop();

        // TODO: Free page entries once they are allocated in guest memory.
        // ASSERT(this->GetPageTableManager().IsInPageTableHeap(page));
        // ASSERT(this->GetPageTableManager().GetRefCount(page) == 0);
        // this->GetPageTableManager().Free(page);
    }
}

} // namespace Kernel
