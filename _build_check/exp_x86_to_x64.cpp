// x86 -> x64 injection channel experiment (decisive)
// checks:
//  [1] CreateToolhelp32Snapshot(TH32CS_SNAPMODULE) 从 x86 进程枚举"挂起的 x64 进程"能否拿到 exe 基址
//  [2] NtQueryInformationProcess(ProcessBasicInformation) 从 x86 拿 x64 目标 PEB 指针是否可用
//  [3] 跨位宽 ReadProcessMemory / VirtualAllocEx / WriteProcessMemory
//  [4] 挂起的 x64 新进程 exe 镜像基址 == 文件 PE 头 preferred base？
//  [5] 挂起新进程的模块列表里有没有 kernel32（决定远程线程 LoadLibrary 方案是否可行）
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

typedef NTSTATUS(WINAPI* NtQueryInformationProcess_t)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

struct PBI32 { // 32 位视角的 PROCESS_BASIC_INFORMATION
    NTSTATUS ExitStatus;
    ULONG     Reserved;
    ULONG_PTR PebBaseAddress;   // 32 位编译下是 4 字节
    ULONG_PTR AffinityMask;
    LONG      BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
};
struct PBI64 {
    NTSTATUS ExitStatus;
    ULONG     Reserved;
    uint64_t  PebBaseAddress;
    uint64_t  AffinityMask;
    LONG      BasePriority;
    ULONG     Reserved2[3];
    uint64_t  UniqueProcessId;
    uint64_t  InheritedFromUniqueProcessId;
};

static DWORD ReadFileU32At(const wchar_t* path, DWORD rvaToFile, DWORD* out)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD n = 0;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    BYTE hdr[4096] = {};
    if (!ReadFile(h, hdr, sizeof(hdr), &n, NULL) || n < 0x200) { DWORD e = GetLastError(); CloseHandle(h); return e ? e : 1; }
    CloseHandle(h);
    if (hdr[0] != 'M' || hdr[1] != 'Z') return ERROR_INVALID_DATA;
    DWORD e_lfanew = *(DWORD*)(hdr + 0x3C);
    if (e_lfanew + 0x100 > n) return ERROR_INVALID_DATA;
    BYTE* nh = hdr + e_lfanew;
    if (*(DWORD*)nh != 0x00004550) return ERROR_INVALID_DATA;
    WORD machine = *(WORD*)(nh + 4);
    WORD nsects = *(WORD*)(nh + 6);
    WORD optSize = *(WORD*)(nh + 20);
    BYTE* opt = nh + 24;
    DWORD magic = *(DWORD*)opt;
    DWORD imageBaseOff = (magic == 0x20b) ? 24 : 28; // PE32+ vs PE32 (OptionalHeader.ImageBase)
    DWORD preferredBase = *(DWORD*)(opt + imageBaseOff);
    DWORD aoe = *(DWORD*)(opt + 16); // AddressOfEntryPoint
    DWORD rvaDD = (magic == 0x20b) ? 112 : 96;  // DataDirectory offset
    DWORD impRVA = *(DWORD*)(opt + rvaDD + 1 * 8);
    DWORD impSize = *(DWORD*)(opt + rvaDD + 1 * 8 + 4);
    wprintf(L"    [PE] machine=%04x magic=%03x nsects=%d preferredBase=%08X AOE=%08X ImpDirRVA=%08X ImpDirSize=%08X\n",
        machine, magic, nsects, preferredBase, aoe, impRVA, impSize);
    *out = preferredBase;
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* target = (argc > 1) ? argv[1] : L"C:\\Windows\\System32\\notepad.exe";
    wprintf(L"=== x86 -> x64 injection experiment ===\ntarget: %ls\nself bitness: %s\n\n", target,
        (sizeof(void*) == 8) ? L"x64" : L"x86");

    // 文件 PE 头
    DWORD filePreferred = 0;
    wprintf(L"[4] target exe file PE header://n");
    ReadFileU32At(target, 0, &filePreferred);

    // 挂起创建
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(target, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        wprintf(L"CreateProcessW FAILED err=%lu\n", GetLastError());
        return 1;
    }
    wprintf(L"\n[+] CreateProcessW(CREATE_SUSPENDED) ok pid=%lu tid=%lu\n", pi.dwProcessId, pi.dwThreadId);

    // [1] toolhelp 模块枚举（挂起态）
    wprintf(L"\n[1] toolhelp TH32CS_SNAPMODULE on suspended process://n");
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pi.dwProcessId);
    if (hSnap == INVALID_HANDLE_VALUE) {
        wprintf(L"    snapshot FAILED err=%lu\n", GetLastError());
    }
    else {
        MODULEENTRY32W me = { sizeof(me) };
        int idx = 0;
        if (Module32FirstW(hSnap, &me)) {
            do {
                wprintf(L"    [%d] %-30s base=%p size=%u\n", idx++, me.szModule, me.modBaseAddr, me.modBaseSize);
                if (idx > 12) { wprintf(L"    ...(truncated)\n"); break; }
            } while (Module32NextW(hSnap, &me));
        }
        else {
            wprintf(L"    Module32First FAILED err=%lu\n", GetLastError());
        }
        CloseHandle(hSnap);
    }

    // [2] NtQueryInformationProcess PEB
    wprintf(L"\n[2] NtQueryInformationProcess(ProcessBasicInformation):\n");
    NtQueryInformationProcess_t pNtQ = (NtQueryInformationProcess_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (pNtQ) {
        PBI64 pbi64 = {};
        ULONG got = 0;
        NTSTATUS st = pNtQ(pi.hProcess, ProcessBasicInformation, &pbi64, sizeof(pbi64), &got);
        wprintf(L"    PBI64 call st=0x%08lX retlen=%lu PebBaseAddress=0x%llX\n", (unsigned long)st, got, (unsigned long long)pbi64.PebBaseAddress);
        PBI32 pbi32 = {};
        st = pNtQ(pi.hProcess, ProcessBasicInformation, &pbi32, sizeof(pbi32), &got);
        wprintf(L"    PBI32 call st=0x%08lX retlen=%lu PebBaseAddress=0x%llX\n", (unsigned long)st, got, (unsigned long long)pbi32.PebBaseAddress);
        // read ImageBaseAddress from PEB (PEB+0x10 on x64)
        if (pbi64.PebBaseAddress) {
            uint64_t imageBase = 0;
            SIZE_T rd = 0;
            if (ReadProcessMemory(pi.hProcess, (LPCVOID)(pbi64.PebBaseAddress + 0x10), &imageBase, 8, &rd) && rd == 8) {
                wprintf(L"    PEB->ImageBaseAddress = 0x%llX (file preferred=%08X %s)\n",
                    (unsigned long long)imageBase, filePreferred,
                    (DWORD)(imageBase & 0xFFFFFFFF) == filePreferred ? L"MATCH" : L"MISMATCH");
            }
            else {
                wprintf(L"    ReadProcessMemory(PEB) FAILED err=%lu\n", GetLastError());
            }
        }
    }

    // [3] 跨位宽内存操作：VirtualAllocEx + WriteProcessMemory + ReadProcessMemory
    wprintf(L"\n[3] cross-bitness VirtualAllocEx/WPM/RPM:\n");
    LPVOID pRemote = VirtualAllocEx(pi.hProcess, NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemote) {
        wprintf(L"    VirtualAllocEx FAILED err=%lu\n", GetLastError());
    }
    else {
        const char* msg = "TL_INJECT_TEST_1234";
        SIZE_T wr = 0;
        if (!WriteProcessMemory(pi.hProcess, pRemote, msg, 20, &wr)) {
            wprintf(L"    WriteProcessMemory FAILED err=%lu\n", GetLastError());
        }
        else {
            char back[32] = {};
            SIZE_T rd = 0;
            if (ReadProcessMemory(pi.hProcess, pRemote, back, 20, &rd) && rd == 20) {
                wprintf(L"    written[%hs] readback[%hs] -> %s\n", msg, back, strcmp(msg, back) == 0 ? L"PASS" : L"FAIL");
            }
            else {
                wprintf(L"    ReadProcessMemory FAILED err=%lu\n", GetLastError());
            }
        }
        VirtualFreeEx(pi.hProcess, pRemote, 0, MEM_RELEASE);
    }

    // [5] 挂起进程里能否读 exe 内存头部（MZ/PE 签名）
    wprintf(L"\n[5] read target memory PE header at file preferred base://n");
    if (filePreferred) {
        BYTE buf[2] = {};
        SIZE_T rd = 0;
        LPCVOID base = (LPCVOID)(ULONG_PTR)filePreferred;
        if (ReadProcessMemory(pi.hProcess, base, buf, 2, &rd) && rd == 2) {
            wprintf(L"    base=0x%08X first2: %02X %02X (%s)\n", filePreferred, buf[0], buf[1],
                (buf[0] == 'M' && buf[1] == 'Z') ? L"MZ hit (PASS)" : L"not MZ (FAIL)");
        }
        else {
            wprintf(L"    ReadProcessMemory(preferred base) FAILED err=%lu\n", GetLastError());
        }
    }

    // 收尾：终止实验进程
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    wprintf(L"\n=== experiment done (target terminated) ===\n");
    return 0;
}
