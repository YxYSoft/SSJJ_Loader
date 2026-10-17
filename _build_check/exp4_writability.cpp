// exp4：探测导入描述符数组所在页是否可写（就地手术方案的前提）
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>

typedef NTSTATUS(WINAPI* NtWow64QueryInformationProcess64_t)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
typedef NTSTATUS(WINAPI* NtWow64ReadVirtualMemory64_t)(HANDLE, uint64_t, PVOID, uint64_t, uint64_t*);
typedef NTSTATUS(WINAPI* NtWow64WriteVirtualMemory64_t)(HANDLE, uint64_t, PVOID, uint64_t, uint64_t*);

static NtWow64QueryInformationProcess64_t pQ64;
static NtWow64ReadVirtualMemory64_t  pR64;
static NtWow64WriteVirtualMemory64_t pW64;

struct PBI64 {
    NTSTATUS ExitStatus; ULONG Reserved;
    uint64_t PebBaseAddress, AffinityMask; LONG BasePriority;
    uint64_t UniqueProcessId, InheritedFromUniqueProcessId;
};

static bool Read64(HANDLE h, uint64_t addr, void* buf, uint64_t size)
{
    uint64_t got = 0;
    return pR64(h, addr, buf, size, &got) == 0 && got == size;
}
static NTSTATUS Write64St(HANDLE h, uint64_t addr, const void* buf, uint64_t size)
{
    uint64_t got = 0;
    return pW64(h, addr, (PVOID)buf, size, &got);
}

int wmain()
{
    const wchar_t* target = L"C:\\Windows\\Sysnative\\notepad.exe";
    wprintf(L"=== exp4: import descriptor array writability probe ===\n");
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    pQ64 = (NtWow64QueryInformationProcess64_t)GetProcAddress(hNt, "NtWow64QueryInformationProcess64");
    pR64 = (NtWow64ReadVirtualMemory64_t)GetProcAddress(hNt, "NtWow64ReadVirtualMemory64");
    pW64 = (NtWow64WriteVirtualMemory64_t)GetProcAddress(hNt, "NtWow64WriteVirtualMemory64");

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(target, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        wprintf(L"CreateProcessW FAILED err=%lu\n", GetLastError()); return 1;
    }
    wprintf(L"[+] suspended pid=%lu\n", pi.dwProcessId);

    PBI64 pbi = {};
    ULONG got = 0;
    pQ64(pi.hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &got);
    uint64_t imageBase = 0;
    Read64(pi.hProcess, pbi.PebBaseAddress + 0x10, &imageBase, 8);
    wprintf(L"[+] imageBase=0x%llX\n", (unsigned long long)imageBase);

    IMAGE_DOS_HEADER idh = {};
    Read64(pi.hProcess, imageBase, &idh, sizeof(idh));
    IMAGE_NT_HEADERS64 nt = {};
    Read64(pi.hProcess, imageBase + idh.e_lfanew, &nt, sizeof(nt));

    // 打印节表，找导入目录所在节及特征
    DWORD secOff = idh.e_lfanew + 4 + 20 + nt.FileHeader.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> secs(nt.FileHeader.NumberOfSections);
    Read64(pi.hProcess, imageBase + secOff, secs.data(), secs.size() * sizeof(IMAGE_SECTION_HEADER));
    DWORD impRVA = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    DWORD impSize = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    wprintf(L"[+] import dir RVA=0x%lX size=0x%lX\n", impRVA, impSize);
    const char* impSec = "?";
    for (auto& s : secs) {
        char nm[9] = {};
        memcpy(nm, s.Name, 8);
        wprintf(L"    sec %-8s VA=0x%08lX VSz=0x%08lX Chr=0x%08lX %s\n",
            (wchar_t)0, s.VirtualAddress, s.Misc.VirtualSize, s.Characteristics,
            (impRVA >= s.VirtualAddress && impRVA < s.VirtualAddress + s.Misc.VirtualSize) ? L"<< IMPORT DIR HERE" : L"");
        // 上面 printf 中文别名会有乱码，单独再打
        printf("    sec %-8s VA=0x%08lX VSz=0x%08lX Chr=0x%08lX\n",
            nm, s.VirtualAddress, s.Misc.VirtualSize, s.Characteristics);
        if (impRVA >= s.VirtualAddress && impRVA < s.VirtualAddress + s.Misc.VirtualSize) impSec = nm;
    }
    printf("[+] import dir is in section: %s\n", impSec);

    // 读全部描述符
    DWORD nDesc = impSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    std::vector<IMAGE_IMPORT_DESCRIPTOR> descs(nDesc);
    Read64(pi.hProcess, imageBase + impRVA, descs.data(), nDesc * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    wprintf(L"[+] descriptors=%lu (last is terminator if all-zero)\n", nDesc);

    // NULL 终结符地址 = 数组末尾；看它后面 40 字节是什么
    uint64_t termAddr = imageBase + impRVA + (nDesc - 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR);
    BYTE after[64] = {};
    Read64(pi.hProcess, termAddr + 20, after, sizeof(after));
    printf("[+] bytes after NULL terminator (+20): ");
    for (int i = 0; i < 48; ++i) printf("%02X ", after[i]);
    printf("\n     as text: ");
    for (int i = 0; i < 48; ++i) printf("%c", (after[i] >= 32 && after[i] < 127) ? after[i] : '.');
    printf("\n");

    // 可写性探测：把最后一个真实描述符原样写回
    IMAGE_IMPORT_DESCRIPTOR& probe = descs[nDesc - 2];
    NTSTATUS st = Write64St(pi.hProcess, termAddr - sizeof(IMAGE_IMPORT_DESCRIPTOR), &probe, sizeof(probe));
    wprintf(L"[+] write-back probe on descriptor array: NTSTATUS=0x%08lX -> %s\n",
        (unsigned long)st, st == 0 ? L"WRITABLE (in-place surgery possible)" : L"READ-ONLY");

    // 再探测：往 NULL 终结符位置写全零（无害，本来就是全零）
    BYTE zeros[20] = {};
    st = Write64St(pi.hProcess, termAddr, zeros, 20);
    wprintf(L"[+] write probe on NULL terminator: NTSTATUS=0x%08lX -> %s\n",
        (unsigned long)st, st == 0 ? L"WRITABLE" : L"READ-ONLY");

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    wprintf(L"=== exp4 done ===\n");
    return 0;
}
