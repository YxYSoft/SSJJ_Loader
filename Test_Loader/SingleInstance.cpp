#include "SingleInstance.h"
#include "UrlProtocol.h"

SingleInstanceManager::SingleInstanceManager(const std::wstring& mutexName, const std::wstring& propName)
	: m_hMutex(NULL)
	, m_mutexName(mutexName)
	, m_propName(propName)
	, m_hFirstWnd(NULL)
{
}

SingleInstanceManager::~SingleInstanceManager()
{
	// 清理：关闭互斥量句柄
	if (m_hMutex != NULL)
	{
		CloseHandle(m_hMutex);
		m_hMutex = NULL;
	}
}

// 检查是否已有实例运行
bool SingleInstanceManager::HasAnotherInstance()
{
	// 创建命名互斥量（加"Global\\"前缀跨用户会话，更稳妥）
	m_hMutex = CreateMutex(NULL, FALSE, m_mutexName.c_str());
	if (m_hMutex == NULL)
	{
		return false; // 创建失败，默认允许运行（容错处理）
	}

	// 检查错误码：如果是ERROR_ALREADY_EXISTS，说明已有实例
	return (GetLastError() == ERROR_ALREADY_EXISTS);
}

// 【第一个实例调用】给窗口打唯一属性标签
void SingleInstanceManager::MarkWindow(HWND hWnd)
{
	// 设置窗口属性：属性名是我们的唯一标识，值设为窗口句柄本身（或任意非0值）
	SetProp(hWnd, m_propName.c_str(), (HANDLE)hWnd);
}

// 回调函数：遍历窗口，匹配属性标签
BOOL CALLBACK SingleInstanceManager::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	SingleInstanceManager* pThis = (SingleInstanceManager*)lParam;

	// 检查当前窗口是否有我们的唯一属性标签
	HANDLE hProp = GetProp(hWnd, pThis->m_propName.c_str());
	if (hProp != NULL)
	{
		// 找到了！保存窗口句柄
		pThis->m_hFirstWnd = hWnd;
		return FALSE; // 停止遍历
	}

	return TRUE; // 继续遍历下一个窗口
}

// 【第二个实例调用】激活第一个实例的窗口
bool SingleInstanceManager::ActivateFirstInstance(const wchar_t* pPayload)
{
	// 1. 遍历所有窗口，找有我们属性标签的窗口
	m_hFirstWnd = NULL;
	EnumWindows(EnumWindowsProc, (LPARAM)this);
	if (m_hFirstWnd == NULL)
	{
		// 没找到窗口（首实例可能尚在 splash 动画期，窗口未打标记）：
		// 把 payload 写入兜底文件，待首实例窗口就绪后读取
		if (pPayload && pPayload[0])
		{
			WritePendingPayloadFile(pPayload);
		}
		return false;
	}

	// 2. 传递 payload 给第一个实例
	//    必须用 SendMessage（不能 Post）：数据须在接收方读取期间有效，
	//    而本实例随后即退出，Post 的数据指针会失效
	if (pPayload && pPayload[0])
	{
		COPYDATASTRUCT cds = { 0 };
		cds.dwData = 1;
		cds.cbData = (DWORD)((wcslen(pPayload) + 1) * sizeof(wchar_t));
		cds.lpData = (PVOID)pPayload;
		SendMessageW(m_hFirstWnd, WM_COPYDATA, 0, (LPARAM)&cds);
	}

	// 3. 解决SetForegroundWindow权限限制（关键步骤）
	DWORD dwFirstThreadId = GetWindowThreadProcessId(m_hFirstWnd, NULL);
	DWORD dwCurrentThreadId = GetCurrentThreadId();

	AttachThreadInput(dwCurrentThreadId, dwFirstThreadId, TRUE);
	if (!IsWindowVisible(m_hFirstWnd)) {
		ShowWindow(m_hFirstWnd, SW_SHOW);
	}
	// 3. 如果窗口最小化，先恢复
	if (IsIconic(m_hFirstWnd))
	{
		ShowWindow(m_hFirstWnd, SW_RESTORE);
	}

	// 4. 把窗口带到前台
	SetForegroundWindow(m_hFirstWnd);
	SetFocus(m_hFirstWnd);

	// 5. （历史预留）如需给第一个窗口发自定义消息做额外处理，可在此 PostMessage
	//    注：URL 协议 payload 的传递已在上面第 2 步通过 WM_COPYDATA 完成
	// PostMessage(m_hFirstWnd, WM_USER + 200, 0, 0);

	// 6. 断开线程连接
	AttachThreadInput(dwCurrentThreadId, dwFirstThreadId, FALSE);

	return true;
}