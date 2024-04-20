// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bitset>

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Kernel::Svc {

using Handle = u32;

enum class MemoryState : u32 {
    Free = 0x00,
    Io = 0x01,
    Static = 0x02,
    Code = 0x03,
    CodeData = 0x04,
    Normal = 0x05,
    Shared = 0x06,
    Alias = 0x07,
    AliasCode = 0x08,
    AliasCodeData = 0x09,
    Ipc = 0x0A,
    Stack = 0x0B,
    ThreadLocal = 0x0C,
    Transferred = 0x0D,
    SharedTransferred = 0x0E,
    SharedCode = 0x0F,
    Inaccessible = 0x10,
    NonSecureIpc = 0x11,
    NonDeviceIpc = 0x12,
    Kernel = 0x13,
    GeneratedCode = 0x14,
    CodeOut = 0x15,
    Coverage = 0x16,
    Insecure = 0x17,
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryState);

enum class MemoryAttribute : u32 {
    Locked = (1 << 0),
    IpcLocked = (1 << 1),
    DeviceShared = (1 << 2),
    Uncached = (1 << 3),
    PermissionLocked = (1 << 4),
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryAttribute);

enum class MemoryPermission : u32 {
    None = (0 << 0),
    Read = (1 << 0),
    Write = (1 << 1),
    Execute = (1 << 2),
    ReadWrite = Read | Write,
    ReadExecute = Read | Execute,
    DontCare = (1 << 28),
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryPermission);

enum class SignalType : u32 {
    Signal = 0,
    SignalAndIncrementIfEqual = 1,
    SignalAndModifyByWaitingCountIfEqual = 2,
};

enum class ArbitrationType : u32 {
    WaitIfLessThan = 0,
    DecrementAndWaitIfLessThan = 1,
    WaitIfEqual = 2,
};

enum class YieldType : s64 {
    WithoutCoreMigration = 0,
    WithCoreMigration = -1,
    ToAnyThread = -2,
};

enum class ThreadExitReason : u32 {
    ExitThread = 0,
    TerminateThread = 1,
    ExitProcess = 2,
    TerminateProcess = 3,
};

enum class ThreadActivity : u32 {
    Runnable = 0,
    Paused = 1,
};

constexpr inline s32 IdealCoreDontCare = -1;
constexpr inline s32 IdealCoreUseProcessValue = -2;
constexpr inline s32 IdealCoreNoUpdate = -3;

constexpr inline s32 LowestThreadPriority = 63;
constexpr inline s32 HighestThreadPriority = 0;

constexpr inline s32 SystemThreadPriorityHighest = 16;

enum class ProcessState : u32 {
    Created = 0,
    CreatedAttached = 1,
    Running = 2,
    Crashed = 3,
    RunningAttached = 4,
    Terminating = 5,
    Terminated = 6,
    DebugBreak = 7,
};

enum class ProcessExitReason : u32 {
    ExitProcess = 0,
    TerminateProcess = 1,
    Exception = 2,
};

constexpr inline size_t ThreadLocalRegionSize = 0x200;

struct PageInfo {
    u32 flags;
};

// Info Types.
enum class InfoType : u32 {
    CoreMask = 0,
    PriorityMask = 1,
    AliasRegionAddress = 2,
    AliasRegionSize = 3,
    HeapRegionAddress = 4,
    HeapRegionSize = 5,
    TotalMemorySize = 6,
    UsedMemorySize = 7,
    DebuggerAttached = 8,
    ResourceLimit = 9,
    IdleTickCount = 10,
    RandomEntropy = 11,
    AslrRegionAddress = 12,
    AslrRegionSize = 13,
    StackRegionAddress = 14,
    StackRegionSize = 15,
    SystemResourceSizeTotal = 16,
    SystemResourceSizeUsed = 17,
    ProgramId = 18,
    InitialProcessIdRange = 19,
    UserExceptionContextAddress = 20,
    TotalNonSystemMemorySize = 21,
    UsedNonSystemMemorySize = 22,
    IsApplication = 23,
    FreeThreadCount = 24,
    ThreadTickCount = 25,
    IsSvcPermitted = 26,
    IoRegionHint = 27,
    ReservedRegionExtraSize = 28,

    MesosphereMeta = 65000,
    MesosphereCurrentProcess = 65001,
};

enum class BreakReason : u32 {
    Panic = 0,
    Assert = 1,
    User = 2,
    PreLoadDll = 3,
    PostLoadDll = 4,
    PreUnloadDll = 5,
    PostUnloadDll = 6,
    CppException = 7,

    NotificationOnlyFlag = 0x80000000,
};
DECLARE_ENUM_FLAG_OPERATORS(BreakReason);

enum class DebugEvent : u32 {
    CreateProcess = 0,
    CreateThread = 1,
    ExitProcess = 2,
    ExitThread = 3,
    Exception = 4,
};

enum class DebugThreadParam : u32 {
    Priority = 0,
    State = 1,
    IdealCore = 2,
    CurrentCore = 3,
    AffinityMask = 4,
};

enum class DebugException : u32 {
    UndefinedInstruction = 0,
    InstructionAbort = 1,
    DataAbort = 2,
    AlignmentFault = 3,
    DebuggerAttached = 4,
    BreakPoint = 5,
    UserBreak = 6,
    DebuggerBreak = 7,
    UndefinedSystemCall = 8,
    MemorySystemError = 9,
};

enum class DebugEventFlag : u32 {
    Stopped = (1u << 0),
};

enum class BreakPointType : u32 {
    HardwareInstruction = 0,
    HardwareData = 1,
};

enum class HardwareBreakPointRegisterName : u32 {
    I0 = 0,
    I1 = 1,
    I2 = 2,
    I3 = 3,
    I4 = 4,
    I5 = 5,
    I6 = 6,
    I7 = 7,
    I8 = 8,
    I9 = 9,
    I10 = 10,
    I11 = 11,
    I12 = 12,
    I13 = 13,
    I14 = 14,
    I15 = 15,
    D0 = 16,
    D1 = 17,
    D2 = 18,
    D3 = 19,
    D4 = 20,
    D5 = 21,
    D6 = 22,
    D7 = 23,
    D8 = 24,
    D9 = 25,
    D10 = 26,
    D11 = 27,
    D12 = 28,
    D13 = 29,
    D14 = 30,
    D15 = 31,
};

namespace lp64 {
struct LastThreadContext {
    u64 fp;
    u64 sp;
    u64 lr;
    u64 pc;
};

struct PhysicalMemoryInfo {
    u64 physical_address;
    u64 virtual_address;
    u64 size;
};

struct DebugInfoCreateProcess {
    u64 program_id;
    u64 process_id;
    std::array<char, 0xC> name;
    u32 flags;
    u64 user_exception_context_address; // 5.0.0+
};

struct DebugInfoCreateThread {
    u64 thread_id;
    u64 tls_address;
    // Removed in 11.0.0 u64 entrypoint;
};

struct DebugInfoExitProcess {
    ProcessExitReason reason;
};

struct DebugInfoExitThread {
    ThreadExitReason reason;
};

struct DebugInfoUndefinedInstructionException {
    u32 insn;
};

struct DebugInfoDataAbortException {
    u64 address;
};

struct DebugInfoAlignmentFaultException {
    u64 address;
};

struct DebugInfoBreakPointException {
    BreakPointType type;
    u64 address;
};

struct DebugInfoUserBreakException {
    BreakReason break_reason;
    u64 address;
    u64 size;
};

struct DebugInfoDebuggerBreakException {
    std::array<u64, 4> active_thread_ids;
};

struct DebugInfoUndefinedSystemCallException {
    u32 id;
};

union DebugInfoSpecificException {
    DebugInfoUndefinedInstructionException undefined_instruction;
    DebugInfoDataAbortException data_abort;
    DebugInfoAlignmentFaultException alignment_fault;
    DebugInfoBreakPointException break_point;
    DebugInfoUserBreakException user_break;
    DebugInfoDebuggerBreakException debugger_break;
    DebugInfoUndefinedSystemCallException undefined_system_call;
    u64 raw;
};

struct DebugInfoException {
    DebugException type;
    u64 address;
    DebugInfoSpecificException specific;
};

union DebugInfo {
    DebugInfoCreateProcess create_process;
    DebugInfoCreateThread create_thread;
    DebugInfoExitProcess exit_process;
    DebugInfoExitThread exit_thread;
    DebugInfoException exception;
};

struct DebugEventInfo {
    DebugEvent type;
    u32 flags;
    u64 thread_id;
    DebugInfo info;
};
static_assert(sizeof(DebugEventInfo) >= 0x40);

struct SecureMonitorArguments {
    std::array<u64, 8> r;
};
static_assert(sizeof(SecureMonitorArguments) == 0x40);
} // namespace lp64

namespace ilp32 {
struct LastThreadContext {
    u32 fp;
    u32 sp;
    u32 lr;
    u32 pc;
};

struct PhysicalMemoryInfo {
    u64 physical_address;
    u32 virtual_address;
    u32 size;
};

struct DebugInfoCreateProcess {
    u64 program_id;
    u64 process_id;
    std::array<char, 0xC> name;
    u32 flags;
    u32 user_exception_context_address; // 5.0.0+
};

struct DebugInfoCreateThread {
    u64 thread_id;
    u32 tls_address;
    // Removed in 11.0.0 u32 entrypoint;
};

struct DebugInfoExitProcess {
    ProcessExitReason reason;
};

struct DebugInfoExitThread {
    ThreadExitReason reason;
};

struct DebugInfoUndefinedInstructionException {
    u32 insn;
};

struct DebugInfoDataAbortException {
    u32 address;
};

struct DebugInfoAlignmentFaultException {
    u32 address;
};

struct DebugInfoBreakPointException {
    BreakPointType type;
    u32 address;
};

struct DebugInfoUserBreakException {
    BreakReason break_reason;
    u32 address;
    u32 size;
};

struct DebugInfoDebuggerBreakException {
    std::array<u64, 4> active_thread_ids;
};

struct DebugInfoUndefinedSystemCallException {
    u32 id;
};

union DebugInfoSpecificException {
    DebugInfoUndefinedInstructionException undefined_instruction;
    DebugInfoDataAbortException data_abort;
    DebugInfoAlignmentFaultException alignment_fault;
    DebugInfoBreakPointException break_point;
    DebugInfoUserBreakException user_break;
    DebugInfoDebuggerBreakException debugger_break;
    DebugInfoUndefinedSystemCallException undefined_system_call;
    u64 raw;
};

struct DebugInfoException {
    DebugException type;
    u32 address;
    DebugInfoSpecificException specific;
};

union DebugInfo {
    DebugInfoCreateProcess create_process;
    DebugInfoCreateThread create_thread;
    DebugInfoExitProcess exit_process;
    DebugInfoExitThread exit_thread;
    DebugInfoException exception;
};

struct DebugEventInfo {
    DebugEvent type;
    u32 flags;
    u64 thread_id;
    DebugInfo info;
};

struct SecureMonitorArguments {
    std::array<u32, 8> r;
};
static_assert(sizeof(SecureMonitorArguments) == 0x20);
} // namespace ilp32

struct ThreadContext {
    std::array<u64, 29> r;
    u64 fp;
    u64 lr;
    u64 sp;
    u64 pc;
    u32 pstate;
    u32 padding;
    std::array<u128, 32> v;
    u32 fpcr;
    u32 fpsr;
    u64 tpidr;
};
static_assert(sizeof(ThreadContext) == 0x320);

struct MemoryInfo {
    u64 base_address;
    u64 size;
    MemoryState state;
    MemoryAttribute attribute;
    MemoryPermission permission;
    u32 ipc_count;
    u32 device_count;
    u32 padding;
};

enum class LimitableResource : u32 {
    PhysicalMemoryMax = 0,
    ThreadCountMax = 1,
    EventCountMax = 2,
    TransferMemoryCountMax = 3,
    SessionCountMax = 4,
    Count,
};

enum class IoPoolType : u32 {
    // Not supported.
    Count = 0,
};

enum class MemoryMapping : u32 {
    IoRegister = 0,
    Uncached = 1,
    Memory = 2,
};

enum class MapDeviceAddressSpaceFlag : u32 {
    None = (0U << 0),
    NotIoRegister = (1U << 0),
};
DECLARE_ENUM_FLAG_OPERATORS(MapDeviceAddressSpaceFlag);

union MapDeviceAddressSpaceOption {
    u32 raw;
    BitField<0, 16, MemoryPermission> permission;
    BitField<16, 1, MapDeviceAddressSpaceFlag> flags;
    BitField<17, 15, u32> reserved;
};

enum class KernelDebugType : u32 {
    Thread = 0,
    ThreadCallStack = 1,
    KernelObject = 2,
    Handle_ = 3,
    Memory = 4,
    PageTable = 5,
    CpuUtilization = 6,
    Process = 7,
    SuspendProcess = 8,
    ResumeProcess = 9,
    Port = 10,
};

enum class KernelTraceState : u32 {
    Disabled = 0,
    Enabled = 1,
};

enum class CodeMemoryOperation : u32 {
    Map = 0,
    MapToOwner = 1,
    Unmap = 2,
    UnmapFromOwner = 3,
};

enum class InterruptType : u32 {
    Edge = 0,
    Level = 1,
};

enum class DeviceName {
    Afi = 0,
    Avpc = 1,
    Dc = 2,
    Dcb = 3,
    Hc = 4,
    Hda = 5,
    Isp2 = 6,
    MsencNvenc = 7,
    Nv = 8,
    Nv2 = 9,
    Ppcs = 10,
    Sata = 11,
    Vi = 12,
    Vic = 13,
    XusbHost = 14,
    XusbDev = 15,
    Tsec = 16,
    Ppcs1 = 17,
    Dc1 = 18,
    Sdmmc1a = 19,
    Sdmmc2a = 20,
    Sdmmc3a = 21,
    Sdmmc4a = 22,
    Isp2b = 23,
    Gpu = 24,
    Gpub = 25,
    Ppcs2 = 26,
    Nvdec = 27,
    Ape = 28,
    Se = 29,
    Nvjpg = 30,
    Hc1 = 31,
    Se1 = 32,
    Axiap = 33,
    Etr = 34,
    Tsecb = 35,
    Tsec1 = 36,
    Tsecb1 = 37,
    Nvdec1 = 38,
    Count,
};

enum class SystemInfoType : u32 {
    TotalPhysicalMemorySize = 0,
    UsedPhysicalMemorySize = 1,
    InitialProcessIdRange = 2,
};

enum class ProcessInfoType : u32 {
    ProcessState = 0,
};

enum class ProcessActivity : u32 {
    Runnable,
    Paused,
};

enum class CreateProcessFlag : u32 {
    // Is 64 bit?
    Is64Bit = (1 << 0),

    // What kind of address space?
    AddressSpaceShift = 1,
    AddressSpaceMask = (7 << AddressSpaceShift),
    AddressSpace32Bit = (0 << AddressSpaceShift),
    AddressSpace64BitDeprecated = (1 << AddressSpaceShift),
    AddressSpace32BitWithoutAlias = (2 << AddressSpaceShift),
    AddressSpace64Bit = (3 << AddressSpaceShift),

    // Should JIT debug be done on crash?
    EnableDebug = (1 << 4),

    // Should ASLR be enabled for the process?
    EnableAslr = (1 << 5),

    // Is the process an application?
    IsApplication = (1 << 6),

    // 4.x deprecated: Should use secure memory?
    DeprecatedUseSecureMemory = (1 << 7),

    // 5.x+ Pool partition type.
    PoolPartitionShift = 7,
    PoolPartitionMask = (0xF << PoolPartitionShift),
    PoolPartitionApplication = (0 << PoolPartitionShift),
    PoolPartitionApplet = (1 << PoolPartitionShift),
    PoolPartitionSystem = (2 << PoolPartitionShift),
    PoolPartitionSystemNonSecure = (3 << PoolPartitionShift),

    // 7.x+ Should memory allocation be optimized? This requires IsApplication.
    OptimizeMemoryAllocation = (1 << 11),

    // 11.x+ DisableDeviceAddressSpaceMerge.
    DisableDeviceAddressSpaceMerge = (1 << 12),

    EnableReservedRegionExtraSize = (1 << 13),

    // Mask of all flags.
    All = Is64Bit | AddressSpaceMask | EnableDebug | EnableAslr | IsApplication |
          PoolPartitionMask | OptimizeMemoryAllocation | DisableDeviceAddressSpaceMerge |
          EnableReservedRegionExtraSize,
};
DECLARE_ENUM_FLAG_OPERATORS(CreateProcessFlag);

struct CreateProcessParameter {
    std::array<char, 12> name;
    u32 version;
    u64 program_id;
    u64 code_address;
    s32 code_num_pages;
    CreateProcessFlag flags;
    Handle reslimit;
    s32 system_resource_num_pages;
};
static_assert(sizeof(CreateProcessParameter) == 0x30);

constexpr size_t NumSupervisorCalls = 0xC0;
using SvcAccessFlagSet = std::bitset<NumSupervisorCalls>;

enum class InitialProcessIdRangeInfo : u64 {
    Minimum = 0,
    Maximum = 1,
};

} // namespace Kernel::Svc
