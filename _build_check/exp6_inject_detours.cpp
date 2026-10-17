// exp6: Detours x86 -> x64 cross-bitness injection verification
#include <windows.h>
#include <stdio.h>
#include "detours.h"
#pragma comment(lib, "detours.lib")

static FILE* g_log = NULL;
static void L(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log ? g_log : stdout, fmt, ap);
    va_end(ap);
    fflush(g_log ? g_log : stdout);
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* target = (argc > 1) ? argv[1] : L"C:\\Windows\\Sysnative\\notepad.exe";
    const char* dll = "C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\probe64.dll";
    g_log = fopen("C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\exp6_run.log", "w");

    L("[exp6] DetourCreateProcessWithDllExW cross-bitness test\n");
    L("[exp6] target=%ls\n", target);
    L("[exp6] dll=%hs\n", dll ? dll : "(none)");

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    // CREATE_SUSPENDED: detours injects then leaves thread suspended
    BOOL ok = DetourCreateProcessWithDllExW(
        target,          // lpApplicationName
        nullptr,         // lpCommandLine
        nullptr,         // lpProcessAttributes
        nullptr,         // lpThreadAttributes
        FALSE,           // bInheritHandles
        CREATE_SUSPENDED,// dwCreationFlags
        nullptr,         // lpEnvironment
        nullptr,         // lpCurrentDirectory
        &si,             // lpStartupInfo
        &pi,             // lpProcessInformation
        dll,             // lpDllName (LPCSTR even in W version)
        nullptr);        // pfCreateProcessW

    if (!ok) {
        L("[exp6] FAILED err=%lu\n", GetLastError());
        if (g_log) fclose(g_log);
        return 1;
    }
    L("[exp6] injected ok pid=%lu tid=%lu (thread still suspended)\n", pi.dwProcessId, pi.dwThreadId);

    // verify target is really x64
    BOOL wow64 = FALSE;
    if (IsWow64Process(pi.hProcess, &wow64) && !wow64) {
        L("[exp6] target confirmed x64\n");
    } else {
        L("[exp6] WARNING target looks x86 (wow64=%d)\n", wow64);
    }

    // resume and poll for marker up to 6s
    L("[exp6] resuming thread...\n");
    ResumeThread(pi.hThread);
    const wchar_t* marker = L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\tl_probe_ok.txt";
    bool gotMarker = false;
    for (int i = 0; i < 20; i++) {
        Sleep(300);
        if (GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES) { gotMarker = true; break; }
        DWORD c = 0; GetExitCodeProcess(pi.hProcess, &c);
        if (c != STILL_ACTIVE) { L("[exp6] target exited early code=%lu\n", c); break; }
    }
    L("[exp6] marker: %s\n", gotMarker ? L"PASS (DllMain executed)" : L"FAIL (no marker)");

    // dump module list before killing (x64 enum helper)
    wchar_t cmd[256];
    swprintf(cmd, 256, L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\exp7.exe %lu", pi.dwProcessId);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hOut = CreateFileW(L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\exp7_out.log",
                              GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
    STARTUPINFOW si2 = { sizeof(si2) };
    PROCESS_INFORMATION pi2 = {};
    si2.dwFlags = STARTF_USESTDHANDLES;
    si2.hStdOutput = hOut;
    si2.hStdError = hOut;
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si2, &pi2)) {
        WaitForSingleObject(pi2.hProcess, 10000);
        CloseHandle(pi2.hThread); CloseHandle(pi2.hProcess);
    }
    CloseHandle(hOut);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    L("[exp6] target exit code=%lu (STILL_ACTIVE=259 means alive)\n", code);

    if (code == STILL_ACTIVE) {
        L("[exp6] terminating target...\n");
        TerminateProcess(pi.hProcess, 0);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    L("[exp6] done\n");
    if (g_log) fclose(g_log);
    return 0;
}
