// exp7: enumerate modules of a running process (x64 host, x64 target)
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) { wprintf(L"usage: exp7 <pid> [module-keyword]\n"); return 1; }
    DWORD pid = _wtoi(argv[1]);
    const wchar_t* key = (argc > 2) ? argv[2] : L"probe64";
    wprintf(L"=== exp7: modules of pid %lu (key=%ls) ===\n", pid, key);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (hSnap == INVALID_HANDLE_VALUE) {
        wprintf(L"snapshot FAILED err=%lu\n", GetLastError());
        return 1;
    }
    MODULEENTRY32W me = { sizeof(me) };
    int idx = 0;
    bool found = false;
    if (Module32FirstW(hSnap, &me)) {
        do {
            wprintf(L"  [%d] %-24ls base=%p size=%lu\n", idx++, me.szModule, me.modBaseAddr, me.modBaseSize);
            if (wcsstr(me.szModule, key)) found = true;
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);
    wprintf(L"%ls loaded: %s\n", key, found ? L"YES" : L"NO");
    return found ? 0 : 2;
}
