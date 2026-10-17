// exp3 端到端：x86 注入器 -> 挂起的 x64 notepad，导入表修补方式注入 x64 探针 DLL
// 参照 Detours v4.0.1 uimports.cpp 的 UpdateImports64 写法，但读写全部走 NtWow64* 跨位宽 API
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>

typedef NTSTATUS(WINAPI* NtWow64QueryInformationProcess64_t)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
typedef NTSTATUS(WINAPI* NtWow64ReadVirtualMemory64_t)(HANDLE, uint64_t, PVOID, uint64_t, uint64_t*);
typedef NTSTATUS(WINAPI* NtWow64WriteVirtualMemory64_t)(HANDLE, uint64_t, PVOID, uint64_t, uint64_t*);
typedef NTSTATUS(WINAPI* NtWow64AllocateVirtualMemory64_t)(HANDLE, uint64_t*, uint64_t, uint64_t*, ULONG, ULONG);

static NtWow64QueryInformationProcess64_t pQ64;
static NtWow64ReadVirtualMemory64_t  pR64;
static NtWow64WriteVirtualMemory64_t pW64;
static NtWow64AllocateVirtualMemory64_t pA64;

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
static bool Write64(HANDLE h, uint64_t addr, const void* buf, uint64_t size)
{
    uint64_t got = 0;
    return pW64(h, addr, (PVOID)buf, size, &got) == 0 && got == size;
}

// 在镜像基址附近分配内存（RVA = addr - base 必须落在 32 位内）
static uint64_t AllocNearBase64(HANDLE h, uint64_t base, uint64_t size)
{
    const uint64_t hints[] = {
        base + 0x01000000, base - 0x01000000,
        base + 0x00100000, base - 0x00100000,
        base + 0x10000000, base - 0x10000000,
    };
    for (uint64_t hint : hints) {
        if (hint == 0) continue;
        uint64_t addr = hint;
        uint64_t sz = size;
        NTSTATUS st = pA64(h, &addr, 0, &sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (st == 0 && addr != 0) {
            wprintf(L"    alloc near base: 0x%llX (hint 0x%llX, delta=0x%llX)\n",
                (unsigned long long)addr, (unsigned long long)hint,
                (unsigned long long)(addr > base ? addr - base : base - addr));
            return addr;
        }
    }
    return 0;
}

// 导入表修补：把 dllPath 追加进挂起进程的导入表
static bool InjectViaImports(HANDLE hProcess, const char* dllPath)
{
    // 1) PEB -> ImageBaseAddress
    PBI64 pbi = {};
    ULONG got = 0;
    if (pQ64(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &got) != 0 || !pbi.PebBaseAddress) {
        wprintf(L"    [inj] PEB query FAILED\n"); return false;
    }
    uint64_t imageBase = 0;
    if (!Read64(hProcess, pbi.PebBaseAddress + 0x10, &imageBase, 8)) {
        wprintf(L"    [inj] ImageBaseAddress read FAILED\n"); return false;
    }
    wprintf(L"    [inj] imageBase = 0x%llX\n", (unsigned long long)imageBase);

    // 2) 读 DOS/NT 头
    IMAGE_DOS_HEADER idh = {};
    if (!Read64(hProcess, imageBase, &idh, sizeof(idh)) || idh.e_magic != 0x5A4D) {
        wprintf(L"    [inj] DOS header FAILED\n"); return false;
    }
    IMAGE_NT_HEADERS64 nt = {};
    if (!Read64(hProcess, imageBase + idh.e_lfanew, &nt, sizeof(nt))) {
        wprintf(L"    [inj] NT header read FAILED\n"); return false;
    }
    if (nt.Signature != 0x00004550 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.FileHeader.Machine != 0x8664) {
        wprintf(L"    [inj] not PE32+ AMD64 (sig=%08x magic=%03x machine=%04x)\n",
            nt.Signature, nt.OptionalHeader.Magic, nt.FileHeader.Machine); return false;
    }

    DWORD impRVA = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    DWORD impSize = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    wprintf(L"    [inj] import dir RVA=0x%lX size=0x%lX\n", impRVA, impSize);

    // 3) 读旧导入描述符（直到 NULL，封顶 64 个）
    std::vector<IMAGE_IMPORT_DESCRIPTOR> oldDesc;
    if (impRVA != 0) {
        for (int i = 0; i < 64; ++i) {
            IMAGE_IMPORT_DESCRIPTOR d = {};
            if (!Read64(hProcess, imageBase + impRVA + (DWORD)(i * sizeof(d)), &d, sizeof(d))) break;
            if (d.Name == 0 && d.OriginalFirstThunk == 0 && d.FirstThunk == 0) break; // NULL 终结符
            oldDesc.push_back(d);
        }
    }
    wprintf(L"    [inj] old import descriptors = %zu\n", oldDesc.size());

    // 4) 组装新导入块（布局仿 detours：描述符数组 -> thunk -> 名字串）
    const size_t nameLen = strlen(dllPath) + 1;
    const size_t nOld = oldDesc.size();
    const size_t descBytes = (nOld + 2) * sizeof(IMAGE_IMPORT_DESCRIPTOR); // +我们的+NULL终结
    const size_t obTab = (descBytes + 7) & ~7;                             // 8 字节对齐
    const size_t thunkBytes = 4 * sizeof(IMAGE_THUNK_DATA64);              // 2 组(OFT/FT) x (ordinal1 + null)
    const size_t obStr = obTab + thunkBytes;
    const size_t cbNew = obStr + ((nameLen + 7) & ~7);

    std::vector<BYTE> blk(cbNew, 0);
    auto* piid = (IMAGE_IMPORT_DESCRIPTOR*)blk.data();

    // 我们的描述符放 [0]，旧描述符整体后移
    for (size_t i = 0; i < nOld; ++i) piid[1 + i] = oldDesc[i];

    uint64_t newBase = AllocNearBase64(hProcess, imageBase, cbNew);
    if (!newBase) { wprintf(L"    [inj] alloc near base FAILED\n"); return false; }
    DWORD obBase = (DWORD)(newBase - imageBase);

    DWORD nOff = (DWORD)(obTab);
    piid[0].OriginalFirstThunk = obBase + nOff;
    auto* pt = (IMAGE_THUNK_DATA64*)(blk.data() + nOff);
    pt[0].u1.Ordinal = IMAGE_ORDINAL_FLAG64 + 1; // 导入序号1（探针DLL将导出）
    pt[1].u1.Ordinal = 0;
    nOff = (DWORD)(obTab + 2 * sizeof(IMAGE_THUNK_DATA64));
    piid[0].FirstThunk = obBase + nOff;
    pt = (IMAGE_THUNK_DATA64*)(blk.data() + nOff);
    pt[0].u1.Ordinal = IMAGE_ORDINAL_FLAG64 + 1;
    pt[1].u1.Ordinal = 0;
    piid[0].TimeDateStamp = 0;
    piid[0].ForwarderChain = 0;
    piid[0].Name = obBase + (DWORD)obStr;
    memcpy(blk.data() + obStr, dllPath, nameLen);
    // piid[1+nOld] 已是全零 = NULL 终结符

    if (!Write64(hProcess, newBase, blk.data(), cbNew)) {
        wprintf(L"    [inj] write new import block FAILED\n"); return false;
    }

    // 5) 改头部：清零 BOUND_DIRECTORY；IAT 目录为 0 时补上；IMPORT 目录指向新块；校验和清零
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;
    if (nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress == 0) {
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = obBase;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = (DWORD)cbNew;
    }
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = obBase;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = (DWORD)descBytes;
    nt.OptionalHeader.CheckSum = 0;

    DWORD oldProt = 0;
    bool vpOk = VirtualProtectEx(hProcess, (LPVOID)imageBase, nt.OptionalHeader.SizeOfHeaders, PAGE_EXECUTE_READWRITE, &oldProt);
    wprintf(L"    [inj] VirtualProtectEx(headers) -> %s err=%lu oldProt=0x%lX\n",
        vpOk ? L"OK" : L"FAIL", GetLastError(), oldProt);
    // 无论 VirtualProtectEx 成败，都尝试两种写头部的方式（NtWow64Write 可能绕过只读页）
    bool ok = Write64(hProcess, imageBase, &idh, sizeof(idh))
        && Write64(hProcess, imageBase + idh.e_lfanew, &nt, sizeof(nt));
    if (!ok) {
        SIZE_T wr = 0;
        ok = WriteProcessMemory(hProcess, (LPVOID)imageBase, &idh, sizeof(idh), &wr) && wr == sizeof(idh)
            && WriteProcessMemory(hProcess, (LPVOID)(imageBase + idh.e_lfanew), &nt, sizeof(nt), &wr) && wr == sizeof(nt);
        wprintf(L"    [inj] fallback WriteProcessMemory -> %s err=%lu\n", ok ? L"OK" : L"FAIL", GetLastError());
    }
    if (vpOk) {
        VirtualProtectEx(hProcess, (LPVOID)imageBase, nt.OptionalHeader.SizeOfHeaders, oldProt, &oldProt);
    }
    if (!ok) { wprintf(L"    [inj] header write FAILED\n"); return false; }
    wprintf(L"    [inj] import table patched OK (new import RVA=0x%lX)\n", obBase);
    return true;
}

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* target = L"C:\\Windows\\Sysnative\\notepad.exe";
    const wchar_t* dll = (argc > 1) ? argv[1] : L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\probe64.dll";
    wprintf(L"=== exp3: end-to-end import-patch injection ===\n");

    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    pQ64 = (NtWow64QueryInformationProcess64_t)GetProcAddress(hNt, "NtWow64QueryInformationProcess64");
    pR64 = (NtWow64ReadVirtualMemory64_t)GetProcAddress(hNt, "NtWow64ReadVirtualMemory64");
    pW64 = (NtWow64WriteVirtualMemory64_t)GetProcAddress(hNt, "NtWow64WriteVirtualMemory64");
    pA64 = (NtWow64AllocateVirtualMemory64_t)GetProcAddress(hNt, "NtWow64AllocateVirtualMemory64");
    if (!pQ64 || !pR64 || !pW64 || !pA64) { wprintf(L"!! NtWow64* missing\n"); return 1; }

    char dllAnsi[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, dll, -1, dllAnsi, MAX_PATH, NULL, NULL);

    // 清理旧标记
    DeleteFileA("C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\tl_probe_ok.txt");

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(target, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        wprintf(L"CreateProcessW FAILED err=%lu\n", GetLastError()); return 1;
    }
    wprintf(L"[+] suspended pid=%lu\n", pi.dwProcessId);

    if (!InjectViaImports(pi.hProcess, dllAnsi)) {
        wprintf(L"!! injection FAILED, terminate\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 2;
    }

    wprintf(L"[+] ResumeThread...\n");
    ResumeThread(pi.hThread);

    // 等待 5 秒看进程是否存活（导入表没打错的话 notepad 应正常运行）
    DWORD wr = WaitForSingleObject(pi.hProcess, 5000);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    wprintf(L"[+] wait result=%lu exitcode=%lu %s\n", wr, ec,
        (wr == WAIT_TIMEOUT) ? L"(STILL RUNNING - good)" : L"");

    // 检查探针标记
    FILE* f = nullptr;
    if (fopen_s(&f, "C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\tl_probe_ok.txt", "r") == 0 && f) {
        char line[256] = {};
        fgets(line, sizeof(line), f); fclose(f);
        wprintf(L"[PROBE] marker file: %hs\n", line);
        wprintf(L"\n=== RESULT: INJECTION SUCCEEDED ===\n");
    }
    else {
        wprintf(L"\n=== RESULT: INJECTION FAILED (no marker) ===\n");
    }

    if (wr == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 0); }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
