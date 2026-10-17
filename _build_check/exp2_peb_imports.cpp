// 实验2：x86 进程获取 x64 挂起目标 PEB / 镜像基址 / 内存中的导入目录
// 若 PEB 可达 -> 可实施导入表修补注入（挂起态注入的唯一可靠通道）
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdint.h>

// ---- NtWow64* 原型（SysWOW64 ntdll 导出，WOW64 进程操作 64 位目标的唯一正道）----
typedef NTSTATUS(WINAPI* NtWow64QueryInformationProcess64_t)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
typedef NTSTATUS(WINAPI* NtWow64ReadVirtualMemory64_t)(HANDLE, uint64_t, PVOID, uint64_t, uint64_t*);

struct PBI64 {
    NTSTATUS ExitStatus;      // 0
    ULONG     Reserved;       // 4
    uint64_t  PebBaseAddress; // 8
    uint64_t  AffinityMask;   // 16
    LONG      BasePriority;   // 24
    uint64_t  UniqueProcessId;// 32
    uint64_t  InheritedFromUniqueProcessId; // 40
};                            // total 48
static_assert(sizeof(PBI64) == 48, "PBI64 must be 48 bytes");

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* target = (argc > 1) ? argv[1] : L"C:\\Windows\\System32\\notepad.exe";
    wprintf(L"=== exp2: PEB / image base / in-memory import dir ===\n");
    wprintf(L"target: %ls\n", target);

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    auto pQ64 = (NtWow64QueryInformationProcess64_t)GetProcAddress(hNtdll, "NtWow64QueryInformationProcess64");
    auto pR64 = (NtWow64ReadVirtualMemory64_t)GetProcAddress(hNtdll, "NtWow64ReadVirtualMemory64");
    wprintf(L"NtWow64QueryInformationProcess64 = %p\nNtWow64ReadVirtualMemory64     = %p\n", (void*)pQ64, (void*)pR64);
    if (!pQ64) { wprintf(L"!! NtWow64QueryInformationProcess64 not available\n"); return 1; }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(target, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        wprintf(L"CreateProcessW FAILED err=%lu\n", GetLastError());
        return 1;
    }
    wprintf(L"[+] suspended pid=%lu\n", pi.dwProcessId);
    {
        // 诊断：确认目标进程位宽与真实镜像路径
        USHORT pMachine = 0, nMachine = 0;
        if (IsWow64Process2(pi.hProcess, &pMachine, &nMachine)) {
            wprintf(L"[diag] ProcessMachine=%04x NativeMachine=%04x (0x014c=x86, 0x8664=x64)\n", pMachine, nMachine);
        }
        wchar_t img[MAX_PATH] = {};
        DWORD cch = MAX_PATH;
        if (QueryFullProcessImageNameW(pi.hProcess, 0, img, &cch)) {
            wprintf(L"[diag] image = %ls\n", img);
        }
    }

    // ---- 1) 拿 64 位 PEB ----
    PBI64 pbi = {};
    ULONG got = 0;
    NTSTATUS st = pQ64(pi.hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &got);
    wprintf(L"[1] NtWow64QueryInformationProcess64 st=0x%08lX retlen=%lu PEB=0x%llX pid64=0x%llX\n",
        (unsigned long)st, got, (unsigned long long)pbi.PebBaseAddress,
        (unsigned long long)pbi.UniqueProcessId);
    if (st != 0 || !pbi.PebBaseAddress) { wprintf(L"!! no PEB\n"); goto cleanup; }

    // ---- 2) PEB+0x10 = ImageBaseAddress (x64)，分别用普通 RPM 与 NtWow64ReadVirtualMemory64 尝试 ----
    {
        uint64_t imageBase = 0;
        SIZE_T rd = 0;
        bool okRpm = ReadProcessMemory(pi.hProcess, (LPCVOID)(pbi.PebBaseAddress + 0x10), &imageBase, 8, &rd) && rd == 8;
        wprintf(L"[2a] ReadProcessMemory(PEB+0x10) -> %s err=%lu\n", okRpm ? L"OK" : L"FAIL", GetLastError());
        uint64_t imageBase2 = 0;
        uint64_t rd64 = 0;
        NTSTATUS st2 = pR64 ? pR64(pi.hProcess, pbi.PebBaseAddress + 0x10, &imageBase2, 8, &rd64) : -1;
        bool okNt = (st2 == 0 && rd64 == 8);
        wprintf(L"[2b] NtWow64ReadVirtualMemory64(PEB+0x10) st=0x%08lX -> %s value=0x%llX\n",
            (unsigned long)st2, okNt ? L"OK" : L"FAIL", (unsigned long long)imageBase2);
        if (okRpm) imageBase = imageBase2; else if (okNt) imageBase = imageBase2;
        if (okRpm || okNt) {
            wprintf(L"[2] ImageBaseAddress = 0x%llX\n", (unsigned long long)imageBase);

            // ---- 3) 读内存中的 MZ/PE ----
            IMAGE_DOS_HEADER idh = {};
            if (ReadProcessMemory(pi.hProcess, (LPCVOID)imageBase, &idh, sizeof(idh), &rd) && rd == sizeof(idh) && idh.e_magic == 0x5A4D) {
                wprintf(L"[3] MZ ok e_lfanew=0x%lX\n", idh.e_lfanew);
                // 读 NT 头（取前 0x200 字节足够覆盖 FileHeader+OptionalHeader 头部）
                BYTE nh[0x200] = {};
                if (ReadProcessMemory(pi.hProcess, (LPCVOID)(imageBase + idh.e_lfanew), nh, sizeof(nh), &rd)) {
                    if (*(DWORD*)nh == 0x00004550) {
                        IMAGE_FILE_HEADER& fh = *(IMAGE_FILE_HEADER*)(nh + 4);
                        IMAGE_OPTIONAL_HEADER64& oh = *(IMAGE_OPTIONAL_HEADER64*)(nh + 24);
                        wprintf(L"[3] PE ok machine=%04x nsects=%d SizeOfImage=%lX EntryRVA=%lX\n",
                            fh.Machine, fh.NumberOfSections, oh.SizeOfImage, oh.AddressOfEntryPoint);
                        DWORD impRVA = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
                        DWORD impSize = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
                        wprintf(L"[3] Import Dir RVA=0x%lX size=0x%lX (%s)\n", impRVA, impSize,
                            impRVA ? L"present" : L"EMPTY");
                        if (impRVA) {
                            // 读前 3 个导入描述符（每个 20 字节）
                            IMAGE_IMPORT_DESCRIPTOR idesc[3] = {};
                            if (ReadProcessMemory(pi.hProcess, (LPCVOID)(imageBase + impRVA), idesc, sizeof(idesc), &rd)) {
                                for (int i = 0; i < 3; ++i) {
                                    if (idesc[i].Name == 0) { wprintf(L"    desc[%d] = NULL terminator\n", i); break; }
                                    char name[64] = {};
                                    ReadProcessMemory(pi.hProcess, (LPCVOID)(imageBase + idesc[i].Name), name, 63, &rd);
                                    wprintf(L"    desc[%d] OFT=0x%lX TS=0x%lX Fwd=0x%lX Name=0x%lX [%hs]\n",
                                        i, idesc[i].OriginalFirstThunk, idesc[i].TimeDateStamp,
                                        idesc[i].ForwarderChain, idesc[i].Name, name);
                                }
                            }
                        }
                        // ---- 4) 验证：能否改写内存（VirtualProtectEx 改节区/头部保护属性）----
                        DWORD oldProt = 0;
                        if (VirtualProtectEx(pi.hProcess, (LPVOID)imageBase, 0x1000, PAGE_READWRITE, &oldProt)) {
                            wprintf(L"[4] VirtualProtectEx on image header OK (old=0x%lX)\n", oldProt);
                            VirtualProtectEx(pi.hProcess, (LPVOID)imageBase, 0x1000, oldProt, &oldProt);
                        }
                        else {
                            wprintf(L"[4] VirtualProtectEx FAILED err=%lu\n", GetLastError());
                        }
                    }
                    else {
                        wprintf(L"[3] PE signature mismatch: 0x%08lX\n", *(DWORD*)nh);
                    }
                }
            }
            else {
                wprintf(L"[3] MZ read FAILED err=%lu\n", GetLastError());
            }
        }
        else {
            wprintf(L"[2] PEB->ImageBaseAddress read FAILED err=%lu\n", GetLastError());
        }
    }

cleanup:
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    wprintf(L"=== exp2 done ===\n");
    return 0;
}
