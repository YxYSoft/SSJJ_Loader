#pragma once

//////////////////////////////////////////////////////////////////////////
class CMainDlg
	: public CDUIWnd
{
	MMDeclare_ClassName(CMainDlg)
	DuiDeclare_Message_Map()

public:
	CMainDlg();
	virtual ~CMainDlg();
	void refreshUserList();
	void LoginIdx(int nIdx);

protected:
	void HandleMenuAddByPwd();
	void HandleMenuAddByTxt();
	void HandleMenuAddByDll();
	void HandleMenuEditDetal(int nSel);
	void HandleMenuEditPwd(int nSel);
	void HandleMenuEditDll(int nSel);
	void HandleMenuLoginSel(int nSel);
	void HandleMenuDeleteSel(int nSel);
	void HandleMenuCreateShortcut(int nSel);
	// URL 协议激活 payload 分发（框架：具体操作后续补充）
	void HandleProtocolPayload(const std::wstring& strPayload);
	std::string GetGamePathDirectory();
	std::wstring GetSSJJInstallPath(int nType); // 注册表探测默认游戏路径(4399/TW)，失败返回空
	void RepairGamePathsFromRegistry();         // 启动时：配置路径缺失/无效则从注册表补写回配置
	std::wstring GetGamePathTW();               // 读配置
	std::wstring GetGamePath4399();             // 读配置
	std::wstring GetHookDllPath();
	bool ShowSettingDialog();

//control
protected:
	CDUIListViewCtrl *					m_pListViewCtrl_UserCtrl = NULL;

	//variant
protected:

	//override
protected:
	void OnFindControl() override;
	void OnInitDialog() override;

	//message
protected:
	LRESULT OnWndMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	LRESULT OnContextMenu(WPARAM wParam, LPARAM lParam) override;

	//notify
protected:
	void OnDuiWndInited(const DuiNotify &Notify);
	void OnDuiWndLayout(const DuiNotify &Notify);
	void OnDuiClick_IDC_BaseCtrl_100027(const DuiNotify &Notify);
	void OnDuiClick_Dui_CtrlIDInner_BtnClose(const DuiNotify &Notify);
	void OnDuiItemDbClick_IDC_ListViewCtrl_100013(const DuiNotify &Notify);

	//help
protected:
};

// URL 协议激活时首实例暂存的 payload（由 Test_Loader.cpp 写入，OnInitDialog 消费）
extern std::wstring g_strPendingProtocolPayload;
