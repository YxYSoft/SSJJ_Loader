#pragma once
#ifndef SYSTEM_TRAY_H
#define SYSTEM_TRAY_H

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>
#include <vector>

// 菜单点击回调函数类型
// 参数：点击的菜单项ID
using MenuClickCallback = std::function<void(int)>;

class SystemTray
{
private:
    HINSTANCE m_hInstance;       // 程序实例句柄
    HWND m_hWnd;                 // 隐藏消息窗口句柄
    NOTIFYICONDATA m_nid;        // 托盘数据结构
    HMENU m_hMenu;               // 右键菜单句柄
    UINT m_trayMsg;              // 托盘自定义消息
    std::wstring m_tooltip;      // 托盘提示文本
    MenuClickCallback m_callback;// 菜单点击回调

    // 菜单项ID管理（避免重复）
    const int MENU_ID_BASE = 1000;
    std::vector<int> m_menuIds;

    // 窗口过程（静态，处理托盘/菜单消息）
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 注册隐藏窗口类
    bool RegisterWindowClass();
    // 创建隐藏消息窗口
    bool CreateMessageWindow();
    // 初始化菜单
    void InitMenu();
    // 弹出右键菜单
    void ShowContextMenu(int x, int y);

public:
    int GetMenuBase() const { return MENU_ID_BASE; }
    /**
     * @brief 构造函数
     * @param hInstance 程序实例句柄（传WinMain的hInstance）
     * @param icon 托盘图标句柄（NULL使用系统默认图标）
     * @param tooltip 托盘悬浮提示文本
     */
    SystemTray(HINSTANCE hInstance, HICON icon, const std::wstring& tooltip);
    ~SystemTray();

    // 初始化托盘（必须调用）
    bool InitTray();
    // 销毁托盘（程序退出前调用）
    void DestroyTray();

    // ==================== 动态菜单操作 ====================
    // 添加菜单项
    void AddMenuItem(int menuId, const std::wstring& text);
    // 按ID删除菜单项
    void RemoveMenuItem(int menuId);
    // 清空所有菜单项
    void ClearMenuItems();
    // 插入菜单项（指定索引）
    void InsertMenuItem(int index, int menuId, const std::wstring& text);
	void AddSeparator()
	{
		// MF_SEPARATOR：创建分隔符，不需要ID和文本
		AppendMenu(m_hMenu, MF_SEPARATOR, 0, NULL);
	}
    // 设置菜单点击回调
    void SetMenuClickCallback(MenuClickCallback callback);
    // 更新托盘提示文本
    void UpdateTooltip(const std::wstring& tooltip);
};

#endif // SYSTEM_TRAY_H