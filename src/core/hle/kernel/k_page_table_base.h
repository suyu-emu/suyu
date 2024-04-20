// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/common_funcs.h"
#include "common/page_table.h"
#include "core/core.h"
#include "core/hle/kernel/k_dynamic_resource_manager.h"
#include "core/hle/kernel/k_light_lock.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/kernel/k_memory_block_manager.h"
#include "core/hle/kernel/k_memory_layout.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_typed_address.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace Kernel {

enum class DisableMergeAttribute : u8 {
    None = (0U << 0),

    DisableHead = (1U << 0),
    DisableHeadAndBody = (1U << 1),
    EnableHeadAndBody = (1U << 2),
    DisableTail = (1U << 3),
    EnableTail = (1U << 4),
    EnableAndMergeHeadBodyTail = (1U << 5),

    EnableHeadBodyTail = EnableHeadAndBody | EnableTail,
    DisableHeadBodyTail = DisableHeadAndBody | DisableTail,
};
DECLARE_ENUM_FLAG_OPERATORS(DisableMergeAttribute);

struct KPageProperties {
    KMemoryPermission perm;
    bool io;
    bool uncached;
    DisableMergeAttribute disable_merge_attributes;
};
static_assert(std::is_trivial_v<KPageProperties>);
static_assert(sizeof(KPageProperties) == sizeof(u32));

class KResourceLimit;
class KSystemResource;

class KPageTableBase {
    SUYU_NON_COPYABLE(KPageTableBase);
    SUYU_NON_MOVEABLE(KPageTableBase);

public:
    using TraversalEntry = Common::PageTable::TraversalEntry;
    using TraversalContext = Common::PageTable::TraversalContext;

    class MemoryRange {
    private:
        KernelCore& m_kernel;
        KPhysicalAddress m_address;
        size_t m_size;
        bool m_heap;

    public:
        explicit MemoryRange(KernelCore& kernel)
            : m_kernel(kernel), m_address(0), m_size(0), m_heap(false) {}

        void Set(KPhysicalAddress address, size_t size, bool heap) {
            m_address = address;
            m_size = size;
            m_heap = heap;
        }

        KPhysicalAddress GetAddress() const {
            return m_address;
        }
        size_t GetSize() const {
            return m_size;
        }
        bool IsHeap() const {
            return m_heap;
        }

        void Open();
        void Close();
    };

protected:
    enum MemoryFillValue : u8 {
        MemoryFillValue_Zero = 0,
        MemoryFillValue_Stack = 'X',
        MemoryFillValue_Ipc = 'Y',
        MemoryFillValue_Heap = 'Z',
    };

    enum class OperationType {
        Map = 0,
        MapGroup = 1,
        MapFirstGroup = 2,
        Unmap = 3,
        ChangePermissions = 4,
        ChangePermissionsAndRefresh = 5,
        ChangePermissionsAndRefreshAndFlush = 6,
        Separate = 7,

        MapFirstGroupPhysical = 65000,
        UnmapPhysical = 65001,
    };

    static constexpr size_t MaxPhysicalMapAlignment = 1_GiB;
    static constexpr size_t RegionAlignment = 2_MiB;
    static_assert(RegionAlignment == KernelAslrAlignment);

    struct PageLinkedList {
    private:
        struct Node {
            Node* m_next;
            std::array<u8, PageSize - sizeof(Node*)> m_buffer;
        };
        static_assert(std::is_trivial_v<Node>);

    private:
        Node* m_root{};

    public:
        constexpr PageLinkedList() : m_root(nullptr) {}

        void Push(Node* n) {
            ASSERT(Common::IsAligned(reinterpret_cast<uintptr_t>(n), PageSize));
            n->m_next = m_root;
            m_root = n;
        }

        Node* Peek() const {
            return m_root;
        }

        Node* Pop() {
            Node* const r = m_root;

            m_root = r->m_next;
            r->m_next = nullptr;

            return r;
        }
    };
    static_assert(std::is_trivially_destructible_v<PageLinkedList>);

    static constexpr auto DefaultMemoryIgnoreAttr =
        KMemoryAttribute::IpcLocked | KMemoryAttribute::DeviceShared;

    static constexpr size_t GetAddressSpaceWidth(Svc::CreateProcessFlag as_type) {
        switch (static_cast<Svc::CreateProcessFlag>(as_type &
                                                    Svc::CreateProcessFlag::AddressSpaceMask)) {
        case Svc::CreateProcessFlag::AddressSpace64Bit:
            return 39;
        case Svc::CreateProcessFlag::AddressSpace64BitDeprecated:
            return 36;
        case Svc::CreateProcessFlag::AddressSpace32Bit:
        case Svc::CreateProcessFlag::AddressSpace32BitWithoutAlias:
            return 32;
        default:
            UNREACHABLE();
        }
    }

private:
    class KScopedPageTableUpdater {
    private:
        KPageTableBase* m_pt;
        PageLinkedList m_ll;

    public:
        explicit KScopedPageTableUpdater(KPageTableBase* pt) : m_pt(pt), m_ll() {}
        explicit KScopedPageTableUpdater(KPageTableBase& pt)
            : KScopedPageTableUpdater(std::addressof(pt)) {}
        ~KScopedPageTableUpdater() {
            m_pt->FinalizeUpdate(this->GetPageList());
        }

        PageLinkedList* GetPageList() {
            return std::addressof(m_ll);
        }
    };

private:
    KernelCore& m_kernel;
    Core::System& m_system;
    KProcessAddress m_address_space_start{};
    KProcessAddress m_address_space_end{};
    KProcessAddress m_heap_region_start{};
    KProcessAddress m_heap_region_end{};
    KProcessAddress m_current_heap_end{};
    KProcessAddress m_alias_region_start{};
    KProcessAddress m_alias_region_end{};
    KProcessAddress m_stack_region_start{};
    KProcessAddress m_stack_region_end{};
    KProcessAddress m_kernel_map_region_start{};
    KProcessAddress m_kernel_map_region_end{};
    KProcessAddress m_alias_code_region_start{};
    KProcessAddress m_alias_code_region_end{};
    KProcessAddress m_code_region_start{};
    KProcessAddress m_code_region_end{};
    size_t m_max_heap_size{};
    size_t m_mapped_physical_memory_size{};
    size_t m_mapped_unsafe_physical_memory{};
    size_t m_mapped_insecure_memory{};
    size_t m_mapped_ipc_server_memory{};
    size_t m_alias_region_extra_size{};
    mutable KLightLock m_general_lock;
    mutable KLightLock m_map_physical_memory_lock;
    KLightLock m_device_map_lock;
    std::unique_ptr<Common::PageTable> m_impl{};
    Core::Memory::Memory* m_memory{};
    KMemoryBlockManager m_memory_block_manager{};
    u32 m_allocate_option{};
    u32 m_address_space_width{};
    bool m_is_kernel{};
    bool m_enable_aslr{};
    bool m_enable_device_address_space_merge{};
    KMemoryBlockSlabManager* m_memory_block_slab_manager{};
    KBlockInfoManager* m_block_info_manager{};
    KResourceLimit* m_resource_limit{};
    const KMemoryRegion* m_cached_physical_linear_region{};
    const KMemoryRegion* m_cached_physical_heap_region{};
    MemoryFillValue m_heap_fill_value{};
    MemoryFillValue m_ipc_fill_value{};
    MemoryFillValue m_stack_fill_value{};

public:
    explicit KPageTableBase(KernelCore& kernel);
    ~KPageTableBase();

    Result InitializeForKernel(bool is_64_bit, KVirtualAddress start, KVirtualAddress end,
                               Core::Memory::Memory& memory);
    Result InitializeForProcess(Svc::CreateProcessFlag as_type, bool enable_aslr,
                                bool enable_device_address_space_merge, bool from_back,
                                KMemoryManager::Pool pool, KProcessAddress code_address,
                                size_t code_size, KSystemResource* system_resource,
                                KResourceLimit* resource_limit, Core::Memory::Memory& memory,
                                KProcessAddress aslr_space_start);

    Result FinalizeProcess();
    void Finalize();

    bool IsKernel() const {
        return m_is_kernel;
    }
    bool IsAslrEnabled() const {
        return m_enable_aslr;
    }

    bool Contains(KProcessAddress addr) const {
        return m_address_space_start <= addr && addr <= m_address_space_end - 1;
    }

    bool Contains(KProcessAddress addr, size_t size) const {
        return m_address_space_start <= addr && addr < addr + size &&
               addr + size - 1 <= m_address_space_end - 1;
    }

    bool IsInAliasRegion(KProcessAddress addr, size_t size) const {
        return this->Contains(addr, size) && m_alias_region_start <= addr &&
               addr + size - 1 <= m_alias_region_end - 1;
    }

    bool IsInHeapRegion(KProcessAddress addr, size_t size) const {
        return this->Contains(addr, size) && m_heap_region_start <= addr &&
               addr + size - 1 <= m_heap_region_end - 1;
    }

    bool IsInUnsafeAliasRegion(KProcessAddress addr, size_t size) const {
        // Even though Unsafe physical memory is KMemoryState_Normal, it must be mapped inside the
        // alias code region.
        return this->CanContain(addr, size, Svc::MemoryState::AliasCode);
    }

    KScopedLightLock AcquireDeviceMapLock() {
        return KScopedLightLock(m_device_map_lock);
    }

    KProcessAddress GetRegionAddress(Svc::MemoryState state) const;
    size_t GetRegionSize(Svc::MemoryState state) const;
    bool CanContain(KProcessAddress addr, size_t size, Svc::MemoryState state) const;

    KProcessAddress GetRegionAddress(KMemoryState state) const {
        return this->GetRegionAddress(static_cast<Svc::MemoryState>(state & KMemoryState::Mask));
    }
    size_t GetRegionSize(KMemoryState state) const {
        return this->GetRegionSize(static_cast<Svc::MemoryState>(state & KMemoryState::Mask));
    }
    bool CanContain(KProcessAddress addr, size_t size, KMemoryState state) const {
        return this->CanContain(addr, size,
                                static_cast<Svc::MemoryState>(state & KMemoryState::Mask));
    }

public:
    Core::Memory::Memory& GetMemory() {
        return *m_memory;
    }

    Core::Memory::Memory& GetMemory() const {
        return *m_memory;
    }

    Common::PageTable& GetImpl() {
        return *m_impl;
    }

    Common::PageTable& GetImpl() const {
        return *m_impl;
    }

    size_t GetNumGuardPages() const {
        return this->IsKernel() ? 1 : 4;
    }

protected:
    // NOTE: These three functions (Operate, Operate, FinalizeUpdate) are virtual functions
    // in Nintendo's kernel. We devirtualize them, since KPageTable is the only derived
    // class, and this avoids unnecessary virtual function calls.
    Result Operate(PageLinkedList* page_list, KProcessAddress virt_addr, size_t num_pages,
                   KPhysicalAddress phys_addr, bool is_pa_valid, const KPageProperties properties,
                   OperationType operation, bool reuse_ll);
    Result Operate(PageLinkedList* page_list, KProcessAddress virt_addr, size_t num_pages,
                   const KPageGroup& page_group, const KPageProperties properties,
                   OperationType operation, bool reuse_ll);
    void FinalizeUpdate(PageLinkedList* page_list);

    bool IsLockedByCurrentThread() const {
        return m_general_lock.IsLockedByCurrentThread();
    }

    bool IsLinearMappedPhysicalAddress(KPhysicalAddress phys_addr) {
        ASSERT(this->IsLockedByCurrentThread());

        return m_kernel.MemoryLayout().IsLinearMappedPhysicalAddress(
            m_cached_physical_linear_region, phys_addr);
    }

    bool IsLinearMappedPhysicalAddress(KPhysicalAddress phys_addr, size_t size) {
        ASSERT(this->IsLockedByCurrentThread());

        return m_kernel.MemoryLayout().IsLinearMappedPhysicalAddress(
            m_cached_physical_linear_region, phys_addr, size);
    }

    bool IsHeapPhysicalAddress(KPhysicalAddress phys_addr) {
        ASSERT(this->IsLockedByCurrentThread());

        return m_kernel.MemoryLayout().IsHeapPhysicalAddress(m_cached_physical_heap_region,
                                                             phys_addr);
    }

    bool IsHeapPhysicalAddress(KPhysicalAddress phys_addr, size_t size) {
        ASSERT(this->IsLockedByCurrentThread());

        return m_kernel.MemoryLayout().IsHeapPhysicalAddress(m_cached_physical_heap_region,
                                                             phys_addr, size);
    }

    bool IsHeapPhysicalAddressForFinalize(KPhysicalAddress phys_addr) {
        ASSERT(!this->IsLockedByCurrentThread());

        return m_kernel.MemoryLayout().IsHeapPhysicalAddress(m_cached_physical_heap_region,
                                                             phys_addr);
    }

    bool ContainsPages(KProcessAddress addr, size_t num_pages) const {
        return (m_address_space_start <= addr) &&
               (num_pages <= (m_address_space_end - m_address_space_start) / PageSize) &&
               (addr + num_pages * PageSize - 1 <= m_address_space_end - 1);
    }

private:
    KProcessAddress FindFreeArea(KProcessAddress region_start, size_t region_num_pages,
                                 size_t num_pages, size_t alignment, size_t offset,
                                 size_t guard_pages) const;

    Result CheckMemoryStateContiguous(size_t* out_blocks_needed, KProcessAddress addr, size_t size,
                                      KMemoryState state_mask, KMemoryState state,
                                      KMemoryPermission perm_mask, KMemoryPermission perm,
                                      KMemoryAttribute attr_mask, KMemoryAttribute attr) const;
    Result CheckMemoryStateContiguous(KProcessAddress addr, size_t size, KMemoryState state_mask,
                                      KMemoryState state, KMemoryPermission perm_mask,
                                      KMemoryPermission perm, KMemoryAttribute attr_mask,
                                      KMemoryAttribute attr) const {
        R_RETURN(this->CheckMemoryStateContiguous(nullptr, addr, size, state_mask, state, perm_mask,
                                                  perm, attr_mask, attr));
    }

    Result CheckMemoryState(const KMemoryInfo& info, KMemoryState state_mask, KMemoryState state,
                            KMemoryPermission perm_mask, KMemoryPermission perm,
                            KMemoryAttribute attr_mask, KMemoryAttribute attr) const;
    Result CheckMemoryState(KMemoryState* out_state, KMemoryPermission* out_perm,
                            KMemoryAttribute* out_attr, size_t* out_blocks_needed,
                            KMemoryBlockManager::const_iterator it, KProcessAddress last_addr,
                            KMemoryState state_mask, KMemoryState state,
                            KMemoryPermission perm_mask, KMemoryPermission perm,
                            KMemoryAttribute attr_mask, KMemoryAttribute attr,
                            KMemoryAttribute ignore_attr = DefaultMemoryIgnoreAttr) const;
    Result CheckMemoryState(KMemoryState* out_state, KMemoryPermission* out_perm,
                            KMemoryAttribute* out_attr, size_t* out_blocks_needed,
                            KProcessAddress addr, size_t size, KMemoryState state_mask,
                            KMemoryState state, KMemoryPermission perm_mask, KMemoryPermission perm,
                            KMemoryAttribute attr_mask, KMemoryAttribute attr,
                            KMemoryAttribute ignore_attr = DefaultMemoryIgnoreAttr) const;
    Result CheckMemoryState(size_t* out_blocks_needed, KProcessAddress addr, size_t size,
                            KMemoryState state_mask, KMemoryState state,
                            KMemoryPermission perm_mask, KMemoryPermission perm,
                            KMemoryAttribute attr_mask, KMemoryAttribute attr,
                            KMemoryAttribute ignore_attr = DefaultMemoryIgnoreAttr) const {
        R_RETURN(this->CheckMemoryState(nullptr, nullptr, nullptr, out_blocks_needed, addr, size,
                                        state_mask, state, perm_mask, perm, attr_mask, attr,
                                        ignore_attr));
    }
    Result CheckMemoryState(KProcessAddress addr, size_t size, KMemoryState state_mask,
                            KMemoryState state, KMemoryPermission perm_mask, KMemoryPermission perm,
                            KMemoryAttribute attr_mask, KMemoryAttribute attr,
                            KMemoryAttribute ignore_attr = DefaultMemoryIgnoreAttr) const {
        R_RETURN(this->CheckMemoryState(nullptr, addr, size, state_mask, state, perm_mask, perm,
                                        attr_mask, attr, ignore_attr));
    }

    Result LockMemoryAndOpen(KPageGroup* out_pg, KPhysicalAddress* out_paddr, KProcessAddress addr,
                             size_t size, KMemoryState state_mask, KMemoryState state,
                             KMemoryPermission perm_mask, KMemoryPermission perm,
                             KMemoryAttribute attr_mask, KMemoryAttribute attr,
                             KMemoryPermission new_perm, KMemoryAttribute lock_attr);
    Result UnlockMemory(KProcessAddress addr, size_t size, KMemoryState state_mask,
                        KMemoryState state, KMemoryPermission perm_mask, KMemoryPermission perm,
                        KMemoryAttribute attr_mask, KMemoryAttribute attr,
                        KMemoryPermission new_perm, KMemoryAttribute lock_attr,
                        const KPageGroup* pg);

    Result QueryInfoImpl(KMemoryInfo* out_info, Svc::PageInfo* out_page,
                         KProcessAddress address) const;

    Result QueryMappingImpl(KProcessAddress* out, KPhysicalAddress address, size_t size,
                            Svc::MemoryState state) const;

    Result AllocateAndMapPagesImpl(PageLinkedList* page_list, KProcessAddress address,
                                   size_t num_pages, KMemoryPermission perm);
    Result MapPageGroupImpl(PageLinkedList* page_list, KProcessAddress address,
                            const KPageGroup& pg, const KPageProperties properties, bool reuse_ll);

    void RemapPageGroup(PageLinkedList* page_list, KProcessAddress address, size_t size,
                        const KPageGroup& pg);

    Result MakePageGroup(KPageGroup& pg, KProcessAddress addr, size_t num_pages);
    bool IsValidPageGroup(const KPageGroup& pg, KProcessAddress addr, size_t num_pages);

    Result GetContiguousMemoryRangeWithState(MemoryRange* out, KProcessAddress address, size_t size,
                                             KMemoryState state_mask, KMemoryState state,
                                             KMemoryPermission perm_mask, KMemoryPermission perm,
                                             KMemoryAttribute attr_mask, KMemoryAttribute attr);

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, size_t alignment,
                    KPhysicalAddress phys_addr, bool is_pa_valid, KProcessAddress region_start,
                    size_t region_num_pages, KMemoryState state, KMemoryPermission perm);

    Result MapIoImpl(KProcessAddress* out, PageLinkedList* page_list, KPhysicalAddress phys_addr,
                     size_t size, KMemoryState state, KMemoryPermission perm);
    Result ReadIoMemoryImpl(KProcessAddress dst_addr, KPhysicalAddress phys_addr, size_t size,
                            KMemoryState state);
    Result WriteIoMemoryImpl(KPhysicalAddress phys_addr, KProcessAddress src_addr, size_t size,
                             KMemoryState state);

    Result SetupForIpcClient(PageLinkedList* page_list, size_t* out_blocks_needed,
                             KProcessAddress address, size_t size, KMemoryPermission test_perm,
                             KMemoryState dst_state);
    Result SetupForIpcServer(KProcessAddress* out_addr, size_t size, KProcessAddress src_addr,
                             KMemoryPermission test_perm, KMemoryState dst_state,
                             KPageTableBase& src_page_table, bool send);
    void CleanupForIpcClientOnServerSetupFailure(PageLinkedList* page_list, KProcessAddress address,
                                                 size_t size, KMemoryPermission prot_perm);

    size_t GetSize(KMemoryState state) const;

    bool GetPhysicalAddressLocked(KPhysicalAddress* out, KProcessAddress virt_addr) const {
        // Validate pre-conditions.
        ASSERT(this->IsLockedByCurrentThread());

        return this->GetImpl().GetPhysicalAddress(out, virt_addr);
    }

public:
    bool GetPhysicalAddress(KPhysicalAddress* out, KProcessAddress virt_addr) const {
        // Validate pre-conditions.
        ASSERT(!this->IsLockedByCurrentThread());

        // Acquire exclusive access to the table while doing address translation.
        KScopedLightLock lk(m_general_lock);

        return this->GetPhysicalAddressLocked(out, virt_addr);
    }

    KBlockInfoManager* GetBlockInfoManager() const {
        return m_block_info_manager;
    }

    Result SetMemoryPermission(KProcessAddress addr, size_t size, Svc::MemoryPermission perm);
    Result SetProcessMemoryPermission(KProcessAddress addr, size_t size,
                                      Svc::MemoryPermission perm);
    Result SetMemoryAttribute(KProcessAddress addr, size_t size, KMemoryAttribute mask,
                              KMemoryAttribute attr);
    Result SetHeapSize(KProcessAddress* out, size_t size);
    Result SetMaxHeapSize(size_t size);
    Result QueryInfo(KMemoryInfo* out_info, Svc::PageInfo* out_page_info,
                     KProcessAddress addr) const;
    Result QueryPhysicalAddress(Svc::lp64::PhysicalMemoryInfo* out, KProcessAddress address) const;
    Result QueryStaticMapping(KProcessAddress* out, KPhysicalAddress address, size_t size) const {
        R_RETURN(this->QueryMappingImpl(out, address, size, Svc::MemoryState::Static));
    }
    Result QueryIoMapping(KProcessAddress* out, KPhysicalAddress address, size_t size) const {
        R_RETURN(this->QueryMappingImpl(out, address, size, Svc::MemoryState::Io));
    }
    Result MapMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size);
    Result UnmapMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size);
    Result MapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size);
    Result UnmapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size);
    Result MapIo(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm);
    Result MapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr, size_t size,
                       Svc::MemoryMapping mapping, Svc::MemoryPermission perm);
    Result UnmapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr, size_t size,
                         Svc::MemoryMapping mapping);
    Result MapStatic(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm);
    Result MapRegion(KMemoryRegionType region_type, KMemoryPermission perm);
    Result MapInsecureMemory(KProcessAddress address, size_t size);
    Result UnmapInsecureMemory(KProcessAddress address, size_t size);

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, size_t alignment,
                    KPhysicalAddress phys_addr, KProcessAddress region_start,
                    size_t region_num_pages, KMemoryState state, KMemoryPermission perm) {
        R_RETURN(this->MapPages(out_addr, num_pages, alignment, phys_addr, true, region_start,
                                region_num_pages, state, perm));
    }

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, size_t alignment,
                    KPhysicalAddress phys_addr, KMemoryState state, KMemoryPermission perm) {
        R_RETURN(this->MapPages(out_addr, num_pages, alignment, phys_addr, true,
                                this->GetRegionAddress(state),
                                this->GetRegionSize(state) / PageSize, state, perm));
    }

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, KMemoryState state,
                    KMemoryPermission perm) {
        R_RETURN(this->MapPages(out_addr, num_pages, PageSize, 0, false,
                                this->GetRegionAddress(state),
                                this->GetRegionSize(state) / PageSize, state, perm));
    }

    Result MapPages(KProcessAddress address, size_t num_pages, KMemoryState state,
                    KMemoryPermission perm);
    Result UnmapPages(KProcessAddress address, size_t num_pages, KMemoryState state);

    Result MapPageGroup(KProcessAddress* out_addr, const KPageGroup& pg,
                        KProcessAddress region_start, size_t region_num_pages, KMemoryState state,
                        KMemoryPermission perm);
    Result MapPageGroup(KProcessAddress address, const KPageGroup& pg, KMemoryState state,
                        KMemoryPermission perm);
    Result UnmapPageGroup(KProcessAddress address, const KPageGroup& pg, KMemoryState state);

    Result MakeAndOpenPageGroup(KPageGroup* out, KProcessAddress address, size_t num_pages,
                                KMemoryState state_mask, KMemoryState state,
                                KMemoryPermission perm_mask, KMemoryPermission perm,
                                KMemoryAttribute attr_mask, KMemoryAttribute attr);

    Result InvalidateProcessDataCache(KProcessAddress address, size_t size);
    Result InvalidateCurrentProcessDataCache(KProcessAddress address, size_t size);

    Result ReadDebugMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size);
    Result ReadDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size,
                             KMemoryState state);

    Result WriteDebugMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size);
    Result WriteDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size,
                              KMemoryState state);

    Result LockForMapDeviceAddressSpace(bool* out_is_io, KProcessAddress address, size_t size,
                                        KMemoryPermission perm, bool is_aligned, bool check_heap);
    Result LockForUnmapDeviceAddressSpace(KProcessAddress address, size_t size, bool check_heap);

    Result UnlockForDeviceAddressSpace(KProcessAddress address, size_t size);
    Result UnlockForDeviceAddressSpacePartialMap(KProcessAddress address, size_t size);

    Result OpenMemoryRangeForMapDeviceAddressSpace(KPageTableBase::MemoryRange* out,
                                                   KProcessAddress address, size_t size,
                                                   KMemoryPermission perm, bool is_aligned);
    Result OpenMemoryRangeForUnmapDeviceAddressSpace(MemoryRange* out, KProcessAddress address,
                                                     size_t size);

    Result LockForIpcUserBuffer(KPhysicalAddress* out, KProcessAddress address, size_t size);
    Result UnlockForIpcUserBuffer(KProcessAddress address, size_t size);

    Result LockForTransferMemory(KPageGroup* out, KProcessAddress address, size_t size,
                                 KMemoryPermission perm);
    Result UnlockForTransferMemory(KProcessAddress address, size_t size, const KPageGroup& pg);
    Result LockForCodeMemory(KPageGroup* out, KProcessAddress address, size_t size);
    Result UnlockForCodeMemory(KProcessAddress address, size_t size, const KPageGroup& pg);

    Result OpenMemoryRangeForProcessCacheOperation(MemoryRange* out, KProcessAddress address,
                                                   size_t size);

    Result CopyMemoryFromLinearToUser(KProcessAddress dst_addr, size_t size,
                                      KProcessAddress src_addr, KMemoryState src_state_mask,
                                      KMemoryState src_state, KMemoryPermission src_test_perm,
                                      KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr);
    Result CopyMemoryFromLinearToKernel(void* buffer, size_t size, KProcessAddress src_addr,
                                        KMemoryState src_state_mask, KMemoryState src_state,
                                        KMemoryPermission src_test_perm,
                                        KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr);
    Result CopyMemoryFromUserToLinear(KProcessAddress dst_addr, size_t size,
                                      KMemoryState dst_state_mask, KMemoryState dst_state,
                                      KMemoryPermission dst_test_perm,
                                      KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                      KProcessAddress src_addr);
    Result CopyMemoryFromKernelToLinear(KProcessAddress dst_addr, size_t size,
                                        KMemoryState dst_state_mask, KMemoryState dst_state,
                                        KMemoryPermission dst_test_perm,
                                        KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                        void* buffer);
    Result CopyMemoryFromHeapToHeap(KPageTableBase& dst_page_table, KProcessAddress dst_addr,
                                    size_t size, KMemoryState dst_state_mask,
                                    KMemoryState dst_state, KMemoryPermission dst_test_perm,
                                    KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                    KProcessAddress src_addr, KMemoryState src_state_mask,
                                    KMemoryState src_state, KMemoryPermission src_test_perm,
                                    KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr);
    Result CopyMemoryFromHeapToHeapWithoutCheckDestination(
        KPageTableBase& dst_page_table, KProcessAddress dst_addr, size_t size,
        KMemoryState dst_state_mask, KMemoryState dst_state, KMemoryPermission dst_test_perm,
        KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr, KProcessAddress src_addr,
        KMemoryState src_state_mask, KMemoryState src_state, KMemoryPermission src_test_perm,
        KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr);

    Result SetupForIpc(KProcessAddress* out_dst_addr, size_t size, KProcessAddress src_addr,
                       KPageTableBase& src_page_table, KMemoryPermission test_perm,
                       KMemoryState dst_state, bool send);
    Result CleanupForIpcServer(KProcessAddress address, size_t size, KMemoryState dst_state);
    Result CleanupForIpcClient(KProcessAddress address, size_t size, KMemoryState dst_state);

    Result MapPhysicalMemory(KProcessAddress address, size_t size);
    Result UnmapPhysicalMemory(KProcessAddress address, size_t size);

    Result MapPhysicalMemoryUnsafe(KProcessAddress address, size_t size);
    Result UnmapPhysicalMemoryUnsafe(KProcessAddress address, size_t size);

    Result UnmapProcessMemory(KProcessAddress dst_address, size_t size, KPageTableBase& src_pt,
                              KProcessAddress src_address);

public:
    KProcessAddress GetAddressSpaceStart() const {
        return m_address_space_start;
    }
    KProcessAddress GetHeapRegionStart() const {
        return m_heap_region_start;
    }
    KProcessAddress GetAliasRegionStart() const {
        return m_alias_region_start;
    }
    KProcessAddress GetStackRegionStart() const {
        return m_stack_region_start;
    }
    KProcessAddress GetKernelMapRegionStart() const {
        return m_kernel_map_region_start;
    }
    KProcessAddress GetCodeRegionStart() const {
        return m_code_region_start;
    }
    KProcessAddress GetAliasCodeRegionStart() const {
        return m_alias_code_region_start;
    }

    size_t GetAddressSpaceSize() const {
        return m_address_space_end - m_address_space_start;
    }
    size_t GetHeapRegionSize() const {
        return m_heap_region_end - m_heap_region_start;
    }
    size_t GetAliasRegionSize() const {
        return m_alias_region_end - m_alias_region_start;
    }
    size_t GetReservedRegionExtraSize() const {
        return m_alias_region_extra_size;
    }
    size_t GetStackRegionSize() const {
        return m_stack_region_end - m_stack_region_start;
    }
    size_t GetKernelMapRegionSize() const {
        return m_kernel_map_region_end - m_kernel_map_region_start;
    }
    size_t GetCodeRegionSize() const {
        return m_code_region_end - m_code_region_start;
    }
    size_t GetAliasCodeRegionSize() const {
        return m_alias_code_region_end - m_alias_code_region_start;
    }

    size_t GetNormalMemorySize() const {
        // Lock the table.
        KScopedLightLock lk(m_general_lock);

        return (m_current_heap_end - m_heap_region_start) + m_mapped_physical_memory_size;
    }

    size_t GetCodeSize() const;
    size_t GetCodeDataSize() const;
    size_t GetAliasCodeSize() const;
    size_t GetAliasCodeDataSize() const;

    u32 GetAllocateOption() const {
        return m_allocate_option;
    }

    u32 GetAddressSpaceWidth() const {
        return m_address_space_width;
    }

public:
    // Linear mapped
    static u8* GetLinearMappedVirtualPointer(KernelCore& kernel, KPhysicalAddress addr) {
        return kernel.System().DeviceMemory().GetPointer<u8>(addr);
    }

    static KPhysicalAddress GetLinearMappedPhysicalAddress(KernelCore& kernel,
                                                           KVirtualAddress addr) {
        return kernel.MemoryLayout().GetLinearPhysicalAddress(addr);
    }

    static KVirtualAddress GetLinearMappedVirtualAddress(KernelCore& kernel,
                                                         KPhysicalAddress addr) {
        return kernel.MemoryLayout().GetLinearVirtualAddress(addr);
    }

    // Heap
    static u8* GetHeapVirtualPointer(KernelCore& kernel, KPhysicalAddress addr) {
        return kernel.System().DeviceMemory().GetPointer<u8>(addr);
    }

    static KPhysicalAddress GetHeapPhysicalAddress(KernelCore& kernel, KVirtualAddress addr) {
        return GetLinearMappedPhysicalAddress(kernel, addr);
    }

    static KVirtualAddress GetHeapVirtualAddress(KernelCore& kernel, KPhysicalAddress addr) {
        return GetLinearMappedVirtualAddress(kernel, addr);
    }

    // Member heap
    u8* GetHeapVirtualPointer(KPhysicalAddress addr) {
        return GetHeapVirtualPointer(m_kernel, addr);
    }

    KPhysicalAddress GetHeapPhysicalAddress(KVirtualAddress addr) {
        return GetHeapPhysicalAddress(m_kernel, addr);
    }

    KVirtualAddress GetHeapVirtualAddress(KPhysicalAddress addr) {
        return GetHeapVirtualAddress(m_kernel, addr);
    }

    // TODO: GetPageTableVirtualAddress
    // TODO: GetPageTablePhysicalAddress
};

} // namespace Kernel
