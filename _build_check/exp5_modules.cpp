// exp5：挂起的 x64 进程里，模块列表有什么？kernel32 加载了吗？
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

int wmain()
{
    const wchar_t* target = L"C:\\Windows\\System32\\notepad.exe"; // x64 程序运行，无重定向问题
    wprintf(L"=== exp5: modules in suspended x64 process ===\n");
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(target, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        wprintf(L"CreateProcessW FAILED err=%lu\n", GetLastError()); return 1;
    }
    wprintf(L"[+] suspended pid=%lu\n", pi.dwProcessId);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pi.dwProcessId);
    if (hSnap == INVALID_HANDLE_VALUE) {
        wprintf(L"snapshot FAILED err=%lu\n", GetLastError());
    }
    else {
        MODULEENTRY32W me = { sizeof(me) };
        int idx = 0;
        if (Module32FirstW(hSnap, &me)) {
            do {
                wprintf(L"  [%d] %-24s base=%p\n", idx++, me.szModule, me.modBaseAddr);
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }
    wprintf(L"[+] module count above; kernel32 %s\n",
        L"check list");

    // 再对比：resume 后再枚举
    ResumeThread(pi.hThread);
    Sleep(500);
    wprintf(L"\n[+] after resume, modules:\n");
    hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pi.dwProcessId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = { sizeof(me) };
        int idx = 0;
        if (Module32FirstW(hSnap, &me)) {
            do {
                wprintf(L"  [%d] %-24s base=%p\n", idx++, me.szModule, me.modBaseAddr);
                if (idx > 8) { wprintf(L"  ...\n"); break; }
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
