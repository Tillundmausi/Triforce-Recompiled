// SPDX-License-Identifier: GPL-2.0-or-later
#include "crash_handler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace TFH {
namespace {

std::filesystem::path g_dir;

void AppendLine(const std::string& line) {
    const auto p = g_dir / "crash.txt";
#ifdef _WIN32
    FILE* f = _wfopen(p.c_str(), L"ab");
#else
    FILE* f = std::fopen(p.c_str(), "ab");
#endif
    if (!f) return;
    std::fwrite(line.data(), 1, line.size(), f);
    std::fputc('\n', f);
    std::fclose(f);
}

#ifdef _WIN32
std::string ModuleAndRva(DWORD64 addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) || !mod) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "?? 0x%016llX", static_cast<unsigned long long>(addr));
        return buf;
    }
    char name[MAX_PATH] = {};
    GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = std::strrchr(name, '\\');
    char buf[MAX_PATH + 64];
    std::snprintf(buf, sizeof buf, "%s+0x%llX", base ? base + 1 : name,
                  static_cast<unsigned long long>(addr - reinterpret_cast<DWORD64>(mod)));
    return buf;
}

void WriteMinidump(EXCEPTION_POINTERS* ep) {
    const auto p = g_dir / "crash.dmp";
    HANDLE file = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                      ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

    char head[160];
    std::snprintf(head, sizeof head, "=== CRASH: exception 0x%08lX, thread %lu ===",
                  ep->ExceptionRecord->ExceptionCode, GetCurrentThreadId());
    AppendLine(head);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        char av[128];
        std::snprintf(av, sizeof av, "access violation: %s address 0x%016llX",
                      ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" :
                      ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "execute",
                      static_cast<unsigned long long>(ep->ExceptionRecord->ExceptionInformation[1]));
        AppendLine(av);
    }

    // Walk the faulting thread's stack from the exception context (x64 unwind data).
    CONTEXT ctx = *ep->ContextRecord;
    for (int frame = 0; frame < 48 && ctx.Rip; ++frame) {
        AppendLine("  #" + std::to_string(frame) + " " + ModuleAndRva(ctx.Rip));
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fn) { // leaf function: return address is at [rsp]
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
            continue;
        }
        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
    }
    WriteMinidump(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

[[noreturn]] void OnTerminate() {
    std::string what = "(no active exception)";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            what = std::string("std::exception: ") + e.what();
        } catch (...) {
            what = "unknown C++ exception";
        }
    }
    AppendLine("=== std::terminate: " + what + " ===");
    std::abort();
}

} // namespace

void InstallCrashHandler(const std::filesystem::path& dir) {
    g_dir = dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::remove(dir / "crash.txt", ec);
    std::filesystem::remove(dir / "crash.dmp", ec);
#ifdef _WIN32
    SetUnhandledExceptionFilter(OnUnhandledException);
#endif
    std::set_terminate(OnTerminate);
}

} // namespace TFH
