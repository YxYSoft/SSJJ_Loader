// harness_first_instance：模拟"第一个实例"（同名互斥体 + 同名窗口属性 + WM_COPYDATA 接收）
// 用法：
//   harness_first.exe            -> 创建窗口并标记属性，收到 WM_COPYDATA 写入 harness_received.txt
//   harness_first.exe --mutexonly -> 只创建互斥体不建窗口（模拟 splash 期窗口未就绪）
#include <windows.h>
#include <string>
#include <cstdio>

static const wchar_t* MUTEX_NAME = L"Global\\YxYSoft_UniqueMutex_8CE50B5D-7F1A-4D3E-8B9A-2C7F8D9E0A1B";
static const wchar_t* PROP_NAME = L"YxYSoft_UniqueProp_8CE50B5D-7F1A-4D3E-8B9A-2C7F8D9E0A1B";
static const wchar_t* OUT_FILE = L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\harness_received.txt";

static void WriteReceived(const std::wstring& payload)
{
	HANDLE hFile = CreateFileW(OUT_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return;
	DWORD n = 0;
	WriteFile(hFile, payload.c_str(), (DWORD)((payload.size() + 1) * sizeof(wchar_t)), &n, NULL);
	CloseHandle(hFile);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_COPYDATA) {
		PCOPYDATASTRUCT c = (PCOPYDATASTRUCT)l;
		if (c && c->lpData && c->cbData >= sizeof(wchar_t)) {
			WriteReceived(std::wstring((const wchar_t*)c->lpData));
		}
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int)
{
	HANDLE hMutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
	bool bMutexOnly = (lpCmdLine && wcsstr(lpCmdLine, L"--mutexonly"));

	if (bMutexOnly) {
		// 只占互斥量，不开窗口：睡 60 秒等第二个实例来投递
		Sleep(60000);
		CloseHandle(hMutex);
		return 0;
	}

	WNDCLASSW wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"HarnessFirstWnd";
	RegisterClassW(&wc);
	HWND hWnd = CreateWindowW(L"HarnessFirstWnd", L"harness-first", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, 300, 200, NULL, NULL, wc.hInstance, NULL);
	SetPropW(hWnd, PROP_NAME, (HANDLE)1);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	RemovePropW(hWnd, PROP_NAME);
	CloseHandle(hMutex);
	return 0;
}
