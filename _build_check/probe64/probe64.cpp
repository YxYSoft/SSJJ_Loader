// probe64.dll：x64 探针 DLL（detours helper 协议版）
// 被 rundll32 以 ",#1" 加载时执行 DetourFinishHelperProcess 完成跨位宽注入；
// 被目标进程静态导入时写标记文件。
#include <windows.h>
#include <stdio.h>
#include "detours.h"

static DWORD WINAPI ProbeThread(LPVOID)
{
    // 延迟一点，证明是在目标进程内运行
    Sleep(500);
    FILE* f = nullptr;
    if (fopen_s(&f, "C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\tl_probe_ok.txt", "w") == 0 && f) {
        fprintf(f, "INJECTED pid=%lu tid=%lu\n", GetCurrentProcessId(), GetCurrentThreadId());
        fclose(f);
    }
    return 0;
}

// rundll32 入口（导出序号 1）：helper 协议
extern "C" __declspec(dllexport) void CALLBACK HelperEntry(HWND h, HINSTANCE i, LPSTR c, INT n)
{
    if (DetourIsHelperProcess()) {
        DetourFinishHelperProcess(h, i, c, n);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // helper rundll32 进程里不启动业务线程（马上要 ExitProcess）
        if (!DetourIsHelperProcess()) {
            CloseHandle(CreateThread(NULL, 0, ProbeThread, NULL, 0, NULL));
        }
    }
    return TRUE;
}
