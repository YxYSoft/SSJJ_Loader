#include "SystemTray.h"
#include <commctrl.h>
#include <obfusheader.h>
// 窗口类名（唯一标识）
#define  TRAY_WND_CLASS  L"SystemTrayWindowClass"
// 托盘消息ID
const UINT TRAY_MESSAGE = WM_USER + 100;

SystemTray::SystemTray(HINSTANCE hInstance, HICON icon, const std::wstring& tooltip)
	: m_hInstance(hInstance)
	, m_hWnd(NULL)
	, m_hMenu(NULL)
	, m_trayMsg(TRAY_MESSAGE)
	, m_tooltip(tooltip)
{
	// 初始化托盘结构体
	ZeroMemory(&m_nid, sizeof(NOTIFYICONDATA));
	m_nid.cbSize = NOTIFYICONDATA_V2_SIZE; // 兼容Vista+
	m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_nid.uCallbackMessage = m_trayMsg;
	m_nid.hIcon = icon;
	wcscpy_s(m_nid.szTip, m_tooltip.c_str());
}

SystemTray::~SystemTray()
{
	DestroyTray();
}

// 注册窗口类
bool SystemTray::RegisterWindowClass()
{
	auto cls = OBF(TRAY_WND_CLASS);
	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = m_hInstance;
	wc.lpszClassName = (LPCWSTR)cls;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	return RegisterClassEx(&wc) != 0;
}

// 创建隐藏消息窗口
bool SystemTray::CreateMessageWindow()
{
	m_hWnd = CreateWindowEx(
		0,OBF( TRAY_WND_CLASS), L"SystemTrayMessageWindow",
		0, 0, 0, 0, 0, NULL, NULL, m_hInstance, this
	);
	return m_hWnd != NULL;
}

// 初始化菜单
void SystemTray::InitMenu()
{
	m_hMenu = CreatePopupMenu();
	m_menuIds.clear();
}

// 静态窗口过程
LRESULT CALLBACK SystemTray::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// 获取类实例指针
	SystemTray* pTray = (SystemTray*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (msg)
	{
	case WM_CREATE:
	{
		// 保存类实例到窗口数据
		CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pCreate->lpCreateParams);
		return 0;
	}

	case TRAY_MESSAGE:
	{
		if (!pTray) break;
		// 右键点击 -> 弹出菜单
		if (lParam == WM_RBUTTONUP)
		{
			POINT pt;
			GetCursorPos(&pt);
			pTray->ShowContextMenu(pt.x, pt.y);
		}
		return 0;
	}

	case WM_COMMAND:
	{
		if (!pTray) break;
		// 菜单点击事件
		int menuId = LOWORD(wParam);
		if (pTray->m_callback)
		{
			pTray->m_callback(menuId);
		}
		return 0;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 弹出右键菜单（修复Windows菜单消失BUG）
void SystemTray::ShowContextMenu(int x, int y)
{
	SetForegroundWindow(m_hWnd); // 关键：解决菜单点击后不消失
	TrackPopupMenu(
		m_hMenu,
		TPM_BOTTOMALIGN | TPM_LEFTALIGN,
		x, y, 0, m_hWnd, NULL
	);
}

// 初始化托盘
bool SystemTray::InitTray()
{
	if (!RegisterWindowClass() || !CreateMessageWindow())
		return false;

	m_nid.hWnd = m_hWnd;
	InitMenu();

	// 添加托盘图标
	return Shell_NotifyIcon(NIM_ADD, &m_nid);
}

// 销毁托盘
void SystemTray::DestroyTray()
{
	if (m_hMenu)
	{
		DestroyMenu(m_hMenu);
		m_hMenu = NULL;
	}
	if (m_hWnd)
	{
		Shell_NotifyIcon(NIM_DELETE, &m_nid);
		DestroyWindow(m_hWnd);
		m_hWnd = NULL;
	}
	UnregisterClass(OBF(TRAY_WND_CLASS), m_hInstance);
}

// ==================== 动态菜单实现 ====================
void SystemTray::AddMenuItem(int menuId, const std::wstring& text)
{
	AppendMenu(m_hMenu, MF_STRING, menuId, text.c_str());
	m_menuIds.push_back(menuId);
}

void SystemTray::RemoveMenuItem(int menuId)
{
	DeleteMenu(m_hMenu, menuId, MF_BYCOMMAND);
	// 移除ID列表
	for (auto it = m_menuIds.begin(); it != m_menuIds.end(); ++it)
	{
		if (*it == menuId)
		{
			m_menuIds.erase(it);
			break;
		}
	}
}

void SystemTray::ClearMenuItems()
{
	for (int id : m_menuIds)
	{
		DeleteMenu(m_hMenu, id, MF_BYCOMMAND);
	}
	m_menuIds.clear();
}

void SystemTray::InsertMenuItem(int index, int menuId, const std::wstring& text)
{
	InsertMenu(m_hMenu, index, MF_BYPOSITION | MF_STRING, menuId, text.c_str());
	if (index >= (int)m_menuIds.size())
		m_menuIds.push_back(menuId);
	else
		m_menuIds.insert(m_menuIds.begin() + index, menuId);
}

// 设置回调
void SystemTray::SetMenuClickCallback(MenuClickCallback callback)
{
	m_callback = callback;
}

// 更新提示
void SystemTray::UpdateTooltip(const std::wstring& tooltip)
{
	m_tooltip = tooltip;
	wcscpy_s(m_nid.szTip, m_tooltip.c_str());
	Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}