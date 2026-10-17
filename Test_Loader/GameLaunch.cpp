// GameLaunch.cpp —— 统一启动链实现
#include <windows.h>
#include <vector>
#include "GameLaunch.h"
#include "detours.h"

// detours x86 静态库（32 位加载器 -> 64 位目标，helper 协议自动处理跨位宽）
// #if defined(_WIN64)
// #error "GameLaunch 只用于 x86 加载器；x64 目标由 detours helper 协议注入"
// #endif

#pragma comment(lib,"detour.lib")

namespace {

// ANSI 路径缓冲（detours 的 lpDllName 是 LPCSTR）
std::string ToAnsi(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 向已创建（挂起）的目标进程写入共享内存登录数据。
// 重要：不能在这里关闭映射句柄——命名 file mapping 对象在最后一个句柄关闭时即被内核销毁，
// 而此时游戏主线程尚未 ResumeThread，注入 DLL 的 watcher 还没机会打开它（典型的对象生命周期坑）。
// 因此返回句柄，由调用方在恢复线程并延迟一段时间后关闭（见 DeferredCloseMapping）。
HANDLE WriteLoginToSharedMemory(HANDLE hProcess, const TLLoginSharedData& data)
{
    wchar_t szName[128] = {};
    TLLoginMappingName(GetProcessId(hProcess), szName, 128);

    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, TL_LOGIN_MAPPING_SIZE, szName);
    if (!hMap) return nullptr;

    // 名字带 PID，不会与目标进程内其他映射冲突。
    void* pView = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, TL_LOGIN_STRUCT_SIZE);
    if (!pView) {
        CloseHandle(hMap);
        return nullptr;
    }
    memcpy(pView, &data, TL_LOGIN_STRUCT_SIZE);
    FlushViewOfFile(pView, TL_LOGIN_STRUCT_SIZE);
    UnmapViewOfFile(pView);
    // 不关闭 hMap：保持命名对象存活，交给调用方延迟关闭
    return hMap;
}

// 护工线程：注入 DLL 的 watcher 每 100ms 轮询，给它 5 秒绰绰有余；
// 之后关闭句柄，命名对象随之销毁。放在后台线程避免阻塞加载器 UI。
DWORD WINAPI DeferredCloseMapping(LPVOID param)
{
    HANDLE h = (HANDLE)param;
    if (h) {
        Sleep(5000);
        CloseHandle(h);
    }
    return 0;
}

} // namespace

bool LaunchGameWithLogin(const std::wstring& exePath,
                         const std::wstring& dllPath,
                         const TLLoginSharedData& loginData,
                         const std::wstring& cmdLine,
                         DWORD* pOutPid,
                         HANDLE* pOutMap)
{
    if (exePath.empty() || dllPath.empty()) return false;

    // DLL 文件必须存在，否则 detours 会创建一个没有登录能力的进程
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    std::string ansiDll = ToAnsi(dllPath);
    if (ansiDll.empty()) return false;

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    // CreateProcessW 的 lpCommandLine 要求可写缓冲
    std::vector<wchar_t> cmdBuf;
    wchar_t* pCmdLine = nullptr;
    if (!cmdLine.empty()) {
        cmdBuf.assign(cmdLine.c_str(), cmdLine.c_str() + cmdLine.size() + 1);
        pCmdLine = cmdBuf.data();
    }
    std::wstring strDir = exePath;
    bool bSetDir = false;
    do 
    {
		auto ps = strDir.find_last_of('\\');
        if (ps == std::wstring::npos) {
            break;
        }
		strDir.resize(ps);
		ps = strDir.find_last_of('\\');
		if (ps == std::wstring::npos) {
			break;
		}
		strDir.resize(ps+1);
        bSetDir = true;
    } while (FALSE);
    // CREATE_SUSPENDED：注入完成后 detours 不会恢复线程，由我们写共享内存后再恢复
    BOOL ok = DetourCreateProcessWithDllExW(
        exePath.c_str(),   // lpApplicationName
        pCmdLine,          // lpCommandLine
        nullptr,           // lpProcessAttributes
        nullptr,           // lpThreadAttributes
        FALSE,             // bInheritHandles
        CREATE_SUSPENDED,  // dwCreationFlags
        nullptr,           // lpEnvironment
        //nullptr,           // lpCurrentDirectory
        (bSetDir ? strDir.c_str() : nullptr) ,
        &si,               // lpStartupInfo
        &pi,               // lpProcessInformation
        ansiDll.c_str(),   // lpDllName（LPCSTR，W 版也是如此）
        nullptr);          // pfCreateProcessW

    if (!ok) {
        // detours 内部失败时已 TerminateProcess 目标
        return false;
    }

    if (pOutPid) *pOutPid = pi.dwProcessId;

    // 注入成功且线程仍挂起：写入共享内存登录数据（返回句柄，暂不全关闭）
    HANDLE hMap = WriteLoginToSharedMemory(pi.hProcess, loginData);
    if (!hMap) {
        // 绝不让无登录数据的进程跑起来
        TerminateProcess(pi.hProcess, ~0u);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }

    // 恢复游戏主线程
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (pOutMap) {
        // 抓取模式：调用方持有映射句柄，读取回写结果后再自行关闭
        // （不跑 5 秒延迟关闭，否则用户还没登录完映射就被销毁）
        *pOutMap = hMap;
    }
    else {
        // 映射句柄交给后台线程延迟关闭：既给 watcher 充足读取时间，又不阻塞 UI
        HANDLE hCloser = CreateThread(nullptr, 0, DeferredCloseMapping, hMap, 0, nullptr);
        if (hCloser) CloseHandle(hCloser);
        else { Sleep(5000); CloseHandle(hMap); }  // 极端兜底：仍保证游戏有时间读取
    }
    return true;
}
