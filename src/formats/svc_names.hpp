// SPDX-License-Identifier: GPL-2.0-or-later
// Names of Horizon OS (3DS kernel) SVCs, taken from the public 3dbrew SVC table.
// Only the ranges relevant to user-mode applications are listed; anything else
// is reported as "unknown" rather than guessed.
#pragma once

#include <cstdint>

namespace tfh::ctr {

inline const char* svcName(std::uint32_t id) {
    static const char* const kLow[] = {
        nullptr,                       // 0x00
        "ControlMemory", "QueryMemory", "ExitProcess", "GetProcessAffinityMask",
        "SetProcessAffinityMask", "GetProcessIdealProcessor", "SetProcessIdealProcessor",
        "CreateThread", "ExitThread", "SleepThread", "GetThreadPriority", "SetThreadPriority",
        "GetThreadAffinityMask", "SetThreadAffinityMask", "GetThreadIdealProcessor",
        "SetThreadIdealProcessor", "GetCurrentProcessorNumber", "Run", "CreateMutex",
        "ReleaseMutex", "CreateSemaphore", "ReleaseSemaphore", "CreateEvent", "SignalEvent",
        "ClearEvent", "CreateTimer", "SetTimer", "CancelTimer", "ClearTimer",
        "CreateMemoryBlock", "MapMemoryBlock", "UnmapMemoryBlock", "CreateAddressArbiter",
        "ArbitrateAddress", "CloseHandle", "WaitSynchronization1", "WaitSynchronizationN",
        "SignalAndWait", "DuplicateHandle", "GetSystemTick", "GetHandleInfo", "GetSystemInfo",
        "GetProcessInfo", "GetThreadInfo", "ConnectToPort", "SendSyncRequest1",
        "SendSyncRequest2", "SendSyncRequest3", "SendSyncRequest4", "SendSyncRequest",
        "OpenProcess", "OpenThread", "GetProcessId", "GetProcessIdOfThread", "GetThreadId",
        "GetResourceLimit", "GetResourceLimitLimitValues", "GetResourceLimitCurrentValues",
        "GetThreadContext", "Break", "OutputDebugString", "ControlPerformanceCounter", // 0x3E
    };
    if (id < sizeof(kLow) / sizeof(kLow[0]) && kLow[id]) return kLow[id];
    switch (id) {
    case 0x47: return "CreatePort";
    case 0x48: return "CreateSessionToPort";
    case 0x49: return "CreateSession";
    case 0x4A: return "AcceptSession";
    case 0x4F: return "ReplyAndReceive";
    case 0x50: return "BindInterrupt";
    case 0x51: return "UnbindInterrupt";
    case 0x52: return "InvalidateProcessDataCache";
    case 0x53: return "StoreProcessDataCache";
    case 0x54: return "FlushProcessDataCache";
    default: return "unknown";
    }
}

} // namespace tfh::ctr
