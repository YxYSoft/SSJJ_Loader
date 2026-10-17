#include "framework.h"
#include "CMainDlg.h"
#include "CAddUserDlg.h"
#include "CInputBoxDlg.h"
#include "CSettingDlg.h"

#include "ReadUserTxt.h"
#include "FileOpenSave.hpp"

#include "TinyBase64.hpp"
#include "dbwork.h"
#include "ThreadPool.h"

#include "NetWork.h"
#include <EncodingConverter.hpp>
#include <ConfigManager.hpp>
#include <json.hpp>
#include "SystemTray.h"

#include "SingleInstance.h"
#include "UrlProtocol.h"
#include "LoginSharedData.h"
#include "GameLaunch.h"
#include <obfusheader.h>

std::vector<UserInfo> gbl_Users;
extern ThreadPool g_ThreadPool;
extern ConfigManager g_ConfigManager;

#define  _STR(x)	    OBF(x)



extern std::unique_ptr<SingleInstanceManager> g_SingleInstanceManager;
std::unique_ptr< SystemTray >g_SystemTray = nullptr;//(GetModuleHandle(NULL), NULL, L"右键快捷登陆");

// URL 协议激活 payload 暂存（Test_Loader.cpp 写入，OnInitDialog 消费）
std::wstring g_strPendingProtocolPayload;



auto WM_USER_LOGIN_SUCCESS = RegisterWindowMessage(_T("WM_USER_LOGIN_SUCCESS"));
//////////////////////////////////////////////////////////////////////////
MMImplement_ClassName(CMainDlg)
DuiBegin_Message_Map(CMainDlg, CDUIWnd)
Dui_On_Notify(DuiNotify_WndInited, OnDuiWndInited)
Dui_On_Notify(DuiNotify_WndLayout, OnDuiWndLayout)
Dui_On_Notify_Ctrl(DuiNotify_Click, IDC_BaseCtrl_100027, OnDuiClick_IDC_BaseCtrl_100027)
Dui_On_Notify_Ctrl(DuiNotify_Click, Dui_CtrlIDInner_BtnClose, OnDuiClick_Dui_CtrlIDInner_BtnClose)
Dui_On_Notify_Ctrl(DuiNotify_ItemDbClick, IDC_ListViewCtrl_100013, OnDuiItemDbClick_IDC_ListViewCtrl_100013)
DuiEnd_Message_Map()




CMainDlg::CMainDlg()
	: CDUIWnd(_T("DlgMain"))
{

	return;
}

CMainDlg::~CMainDlg()
{
	if (g_SystemTray)g_SystemTray->ClearMenuItems();
	if (g_SystemTray)g_SystemTray->DestroyTray();
	if (g_SystemTray)g_SystemTray.reset();
	return;
}

void CMainDlg::OnFindControl()
{
	Dui_DDX_Control(CDUIListViewCtrl, m_pListViewCtrl_UserCtrl, IDC_ListViewCtrl_100013);

	return;
}


void CMainDlg::refreshUserList() {
	m_pListViewCtrl_UserCtrl->RemoveAll();
	DBWork db;
	db.InitDB();
	//auto users = db.GetAllUsers();
	db.GetUsers(gbl_Users);
	if (g_SystemTray)g_SystemTray->ClearMenuItems();
	int nIdx = 2;
	if (g_SystemTray) {
		g_SystemTray->AddMenuItem(g_SystemTray->GetMenuBase() + 1, OBF(L"显示主界面"));
		g_SystemTray->AddSeparator();
	}

	for (auto user : gbl_Users) {
		m_pListViewCtrl_UserCtrl->InsertChild(1);
		std::wstring	remark = user.m_detal;

		if (remark.empty()) {
			remark = user.m_username;
		}

		// 区服短标签前缀：例如 联通三区+小号 -> [联三]小号
		CMMString tag = CAddUserDlg::GetZoneShortTag(user.m_nType, user.m_serverId);
		std::wstring	text = tag.empty() ? remark : (std::wstring(OBF(L"[")) + tag.c_str() + OBF(L"]") + remark);

		m_pListViewCtrl_UserCtrl->GetChildAt(m_pListViewCtrl_UserCtrl->GetChildCount() - 1)->SetText(text.c_str());
		if (g_SystemTray)g_SystemTray->AddMenuItem(g_SystemTray->GetMenuBase() + nIdx, text.c_str());
		++nIdx;
	}
	if (g_SystemTray) {
		g_SystemTray->AddSeparator();
		g_SystemTray->AddMenuItem(g_SystemTray->GetMenuBase(), OBF(L"退出"));
	}

}

void CMainDlg::OnInitDialog()
{
	// 启动阶段：配置里的游戏路径缺失或无效时，从注册表探测并写回配置（持久化）
	RepairGamePathsFromRegistry();

	HICON hInfoIcon = LoadIcon(NULL, IDI_INFORMATION);    // 信息图标
	HICON hShieldIcon = LoadIcon(NULL, IDI_SHIELD);        // 盾牌图标

	if (hShieldIcon == NULL) {
		hShieldIcon = hInfoIcon;
	}
	if (hShieldIcon) {
		g_SystemTray = std::make_unique< SystemTray >(GetModuleHandle(NULL), hShieldIcon, OBF(L"右键可快捷登陆"));
	}

	m_pListViewCtrl_UserCtrl->SetItemModel(OBF((L"MyListIitem")));
	if (g_SingleInstanceManager) {
		g_SingleInstanceManager->MarkWindow(m_hWnd);
	}

	if (g_SystemTray) {
		if (!g_SystemTray->InitTray()) {

		}
	}
	refreshUserList();
	// 注册托盘图标

	if (g_SystemTray)g_SystemTray->UpdateTooltip(OBF(L"右键可快捷登陆"));
	if (g_SystemTray)g_SystemTray->SetMenuClickCallback([this](UINT id) {
		int nIdx = id - g_SystemTray->GetMenuBase();
		if (nIdx == 0) {
			Close();
			return;
		}
		if (nIdx == 1) {
			//Close();
			//显示 自身
			if (!::IsWindowVisible(m_hWnd)) {
				::ShowWindow(m_hWnd, SW_SHOW);
			}
			// 3. 如果窗口最小化，先恢复
			if (::IsIconic(m_hWnd))
			{
				::ShowWindow(m_hWnd, SW_RESTORE);
			}
			return;
		}
		nIdx -= 2;
		if (nIdx >= 0 && nIdx < gbl_Users.size()) {
			// 登录
			LoginIdx(nIdx);
		}
		});
	//g_SystemTray.AddMenuItem(ID_MENU_ADD_BYPWD, _STR(L"添加账号密码"));

	// 消费 URL 协议激活带入的 payload：
	// 1) 本实例被协议直接唤起（wWinMain 阶段暂存的全局）
	// 2) 第二个实例在本实例 splash 动画期写入的兜底文件
	{
		std::wstring strPayload;
		if (!g_strPendingProtocolPayload.empty()) {
			strPayload = g_strPendingProtocolPayload;
			g_strPendingProtocolPayload.clear();
		}
		else {
			ReadPendingPayloadFile(strPayload);
		}
		if (!strPayload.empty()) {
			HandleProtocolPayload(strPayload);
		}
	}
	return;
}

LRESULT CMainDlg::OnWndMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_USER_LOGIN_SUCCESS) {
		// 登录成功
		refreshUserList();
		return 0;
	}
	if (uMsg == WM_COPYDATA) {
		// 第二个实例通过 WM_COPYDATA 转发 URL 协议 payload
		PCOPYDATASTRUCT pCds = (PCOPYDATASTRUCT)lParam;
		if (pCds && pCds->lpData && pCds->cbData >= sizeof(wchar_t)) {
			const wchar_t* pData = (const wchar_t*)pCds->lpData;
			size_t nLen = pCds->cbData / sizeof(wchar_t);
			// 防御：按第一个 null 截断（发送方已保证 null 结尾）
			for (size_t i = 0; i < nLen; ++i) {
				if (pData[i] == L'\0') { nLen = i; break; }
			}
			HandleProtocolPayload(std::wstring(pData, nLen));
		}
		return 0;
	}
	return __super::OnWndMessage(uMsg, wParam, lParam);
}


std::wstring GetUserNameByCookie(const std::wstring& cookie) {
	TinyBase64 custom;
	custom.setPadding('$');  // 自定义填充字符
	try {
		auto res = custom.decode(EncodingConverter::UnicodeToGBK(cookie));
		nlohmann::json _json = nlohmann::json::parse(res);
		// Puser
		if (_json.is_object()) {
			for (auto& itm : _json.items())
			{
				auto& itm2 = _json[itm.key()];
				if (itm2.is_string()) {
					if (itm.key() == OBF("Puser")) {
						return EncodingConverter::UTF8ToUnicode(itm2.get<std::string>());
					}
				}
			}
		}
	}
	catch (const std::exception& e) {
		// 捕获任何 base64/JSON 解析异常：cookie 串异常（截断、编码损坏等）绝不应崩加载器，
		// 交由调用方按"未解析到用户名"处理（仍会以原始 cookie 入库，数据不丢）。
		#if TL_ENABLE_DEBUG_OUTPUT
		OutputDebugStringA(e.what());
		#endif
	}
	return L"";
}

/*
	id			-> 唯一ID
	username	-> 账号
	password	-> 密码
	cookie		-> 登录cookie
	remark		-> 备注
	type		-> 0:账号密码 1:token
*/
LRESULT CMainDlg::OnContextMenu(WPARAM wParam, LPARAM lParam)
{
	//	m_pListViewCtrl_UserCtrl->RemoveAll();
		//m_pListViewCtrl_UserCtrl->SetContextMenu(true);
	auto pEventCtrl = m_pCaptureCtrl ? m_pCaptureCtrl : m_pFocusCtrl;
	while (pEventCtrl && false == pEventCtrl->IsContextMenu())
	{
		pEventCtrl = pEventCtrl->GetParent();
	}
	if (NULL == m_pListViewCtrl_UserCtrl)
		return 0;

	if (m_pListViewCtrl_UserCtrl == pEventCtrl) {
		// 弹出菜单 根据选中项是否为空，禁用菜单项
		CDUIMenu menu;
		menu.LoadMenu(_STR(L"MyListMenu"));
		if (m_pListViewCtrl_UserCtrl->GetCurSel() == -1) {
			menu.GetRootMenu()->GetMenuItem(0)->SetEnabled(false);
			menu.GetRootMenu()->GetMenuItem(2)->SetEnabled(false);
			menu.GetRootMenu()->GetMenuItem(3)->SetEnabled(false);
			menu.GetRootMenu()->GetMenuItem(4)->SetEnabled(false);
		}
		auto res = menu.TrackPopupMenu(m_hWnd);

		if (res.uMenuID == 0) {
			return 0;
		}
		else if (res.uMenuID == ID_MENU_ADD_BYPWD) {
			HandleMenuAddByPwd();
		}
		else if (res.uMenuID == ID_MENU_ADD_BYTXT) {
			HandleMenuAddByTxt();
		}
		else if (res.uMenuID == ID_MENU_ADD_BYDLL) {
			HandleMenuAddByDll();
		}
// 		else if (res.uMenuID == ID_MENU_ADD_TWACCOUNT) {
// 			HandleMenuAddTwAccount();
// 		}
		else {
			int nSel = m_pListViewCtrl_UserCtrl->GetCurSel();
			if (nSel == -1) {
				MessageBox(m_hWnd, _STR(_T("请选择一个用户")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
				return 0;
			}

			if (res.uMenuID == ID_MENU_EDITDETAL) {
				HandleMenuEditDetal(nSel);
			}
			else if (res.uMenuID == ID_MENU_EDITPWD) {
				HandleMenuEditPwd(nSel);
			}
			else if (res.uMenuID == ID_MENU_EDITDLL) {
				HandleMenuEditDll(nSel);
			}
			else if (res.uMenuID == ID_MENU_LOGIN_SEL) {
				HandleMenuLoginSel(nSel);
			}
			else if (res.uMenuID == ID_MENU_DELETESEL) {
				HandleMenuDeleteSel(nSel);
			}
			else if (res.uMenuID == ID_MENU_CREATE_SHORTCUT) {
				HandleMenuCreateShortcut(nSel);
			}
		}
	}
	return __super::OnContextMenu(wParam, lParam);
}

void CMainDlg::OnDuiWndInited(const DuiNotify& Notify)
{
	return;
}

void CMainDlg::OnDuiWndLayout(const DuiNotify& Notify)
{
	return;
}

void CMainDlg::OnDuiClick_IDC_BaseCtrl_100027(const DuiNotify& Notify)
{
	// 设置与关于信息
	CSettingDlg dlg;
	dlg.Create(this->m_hWnd, _STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME);
	dlg.DoModal();
	return;
}

void CMainDlg::OnDuiClick_Dui_CtrlIDInner_BtnClose(const DuiNotify& Notify)
{
	int nCloseOpt = g_ConfigManager.get<int>(OBF("SETTING"), OBF("CloseOpt"), 0);
	if (nCloseOpt == 0) {
		__super::Close();
	}
	else {

		// 最小化
		::ShowWindow(m_hWnd, SW_MINIMIZE);
		//		::ShowWindow(m_hWnd,SW_HIDE);

	}
	return;
}
void CMainDlg::LoginIdx(int nSel) {
	if (nSel < 0 || nSel >= (int)gbl_Users.size()) {
		return;
	}
	const UserInfo& user = gbl_Users[nSel];
	std::wstring dllPath = GetHookDllPath();

	if (user.m_nType == TL_ACCOUNT_TAIWAN) {
		// ===== 台服：账号 + 密码，走 GamePathTW =====
		std::wstring gameExe = GetGamePathTW();
		if (gameExe.empty()) {
			return;
		}

		TLLoginSharedData data = {};
		data.magic = TL_LOGIN_MAGIC;
		data.version = TL_LOGIN_VERSION;
		data.type = TL_ACCOUNT_TAIWAN;
		data.flags = 0;
		wcsncpy_s(data.param0, user.m_username.c_str(), _TRUNCATE);
		wcsncpy_s(data.param1, user.m_password.c_str(), _TRUNCATE);

		if (!LaunchGameWithLogin(gameExe, dllPath, data, std::wstring(L"\"") +  (gameExe) + OBF(L"\" http://tw.battleteams1.com/index/wdLogin"))) {
			MessageBox(m_hWnd, _STR(_T("台服启动失败，请检查路径与 DLL")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
		}
		return;
	}

	// ===== 4399：cookie，注册表自动探测，config 兜底 =====
	std::wstring gameExe = GetGamePath4399();
	if (gameExe.empty()) {
		return;
	}

	TLLoginSharedData data = {};
	data.magic = TL_LOGIN_MAGIC;
	data.version = TL_LOGIN_VERSION;
	data.type = TL_ACCOUNT_4399;
	data.flags = (DWORD)(user.m_serverId & 0xFF);   // 低 8 位 = 4399 服 ID（sid），0=最近服；台服不使用
	wcsncpy_s(data.param0, user.m_token.c_str(), _TRUNCATE);

	if (!LaunchGameWithLogin(gameExe, dllPath, data, std::wstring(L"\"") + gameExe + OBF( L"\" https://my.4399.com/yxssjj/wd"))) {
		MessageBox(m_hWnd, _STR(_T("游戏启动失败，请检查路径与 DLL")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
	}
}

void CMainDlg::HandleMenuAddByPwd()
{
	CAddUserDlg dlg;
	dlg.Create(this->m_hWnd, _STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME);
	if (dlg.DoModal() == IDOK)
	{
		int nType = dlg.GetSelectedNtype();
		int nSid  = dlg.GetSelectedSid();
		CMMString username = dlg.GetUsername();
		CMMString password = dlg.GetPassword();
		if (nType == 100) {
			// 台服账号：纯存账号密码，不联网登录（ntype=100 与共享内存 LOGIN_TYPE_TW 对齐）
			DBWork db;
			db.InitDB();
			UserInfo user{ username, password, L"", username, 100, 0 };
			db.InsertUser(user);
			refreshUserList();
		}
		else {
			// 4399 账号：联网登录拿 cookie，并写入所选区服 ID（0=最近服）
			g_ThreadPool.submit([this, nSid](CMMString username, CMMString password) {
				NetWork net;
				net.LoginUser(EncodingConverter::UnicodeToGBK(username.c_str()), EncodingConverter::UnicodeToGBK(password.c_str()));
				if (net.IsSuccess()) {
					DBWork db;
					db.InitDB();
					UserInfo user{ username, password, EncodingConverter::UTF8ToUnicode(net.GetCookie()), username, 0, 0 };
					user.m_serverId = nSid;
					db.InsertUser(user);
				}
				else {
					MessageBox(m_hWnd, _STR(_T("登录失败，请检查账号密码.或者使用老棉裤获取登录信息")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
				}
				this->PostMessage(WM_USER_LOGIN_SUCCESS);
				}, username, password);
		}
	}
}

void CMainDlg::HandleMenuAddByTxt()
{
	FileDialog dlg;
	auto res = dlg.setOwner(this->m_hWnd)
		.setSaveDialog(false)
		.setFolderDialog(false)
		.setMultiSelect(false)
		.clearFilters()
		.addFilter(OBF(L"文本文件"), OBF(L"*.txt"))
		.addFilter(OBF(L"所有文件"), OBF(L"*.*"))
		.setDefaultExtension(OBF(L"txt"))
		.selectFile();
	if (res.empty()) {
		return;
	}

	auto user_list = readUsersFromFile(res);
	for (auto user : user_list.users) {
		g_ThreadPool.submit([this](CMMString username, CMMString password) {
			NetWork net;
			net.LoginUser(EncodingConverter::UnicodeToGBK(username.c_str()), EncodingConverter::UnicodeToGBK(password.c_str()));
			if (!net.IsSuccess())
			{
				net.LoginUser(EncodingConverter::UnicodeToGBK(username.c_str()), EncodingConverter::UnicodeToGBK(password.c_str()));
			}
			if (net.IsSuccess()) {
				DBWork db;
				db.InitDB();
				UserInfo user{ username, password, EncodingConverter::UTF8ToUnicode(net.GetCookie()), username, 0, 0 };
				db.InsertUser(user);
				this->PostMessage(WM_USER_LOGIN_SUCCESS);
			}
			}, CMMString(user.account.c_str()), CMMString(user.password.c_str()));
	}
}

void CMainDlg::HandleMenuAddByDll()
{
	// 棉裤导入添加（抓取模式）：启动 4399 客户端并注入 DLL，DLL 后台抓取登录 cookie，
	// 加载器弹框阻塞等待用户手动登录，确定后读回共享内存的 cookie 入库。
	std::wstring exePath = GetGamePath4399();
	if (exePath.empty()) {
		return;
	}
	std::wstring dllPath = GetHookDllPath();
	if (!PathFileExists(exePath.c_str())) {
		MessageBox(m_hWnd, _STR(_T("游戏启动器不存在，请检查游戏路径")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
		return;
	}

	// 构造抓取请求：type=1000，DLL 不自动登录，改为后台抓 cookie
	TLLoginSharedData data = {};
	data.magic = TL_LOGIN_MAGIC;
	data.version = TL_LOGIN_VERSION;
	data.type = TL_ACCOUNT_CAPTURE;
	data.flags = 0;

	// 命令行：打开 4399 落地页，便于用户手动登录（与 4399 自动登录同款 URL）
	std::wstring cmdLine = L"\"" + exePath + OBF(L"\" https://my.4399.com/yxssjj/wd");

	HANDLE hMap = nullptr;
	DWORD pid = 0;
	if (!LaunchGameWithLogin(exePath, dllPath, data, cmdLine, &pid, &hMap)) {
		MessageBox(m_hWnd, _STR(_T("游戏启动失败，请检查路径与 DLL")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
		return;
	}

	// 弹框阻塞：用户在游戏客户端手动完成登录后再点确定（取消则不导入）
	int r = MessageBox(m_hWnd,
		_STR(_T("请在游戏客户端中手动完成登录，登录成功后再点击[确定]导入账号。\n（取消则不导入）")),
		_STR(_T("提示")), MB_OKCANCEL | MB_ICONINFORMATION);

	std::wstring cookie;
	if (r == IDOK && hMap) {
		// 命令-应答握手：置 TL_CAP_REQ 请求 DLL 立即抓取并回写 param0；
		// 再轮询 TL_CAP_RDY（带 10s 超时）后读取，避免读到 visit 异步回调尚未完成的空数据。
		void* pView = MapViewOfFile(hMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, TL_LOGIN_STRUCT_SIZE);
		if (pView) {
			auto* p = static_cast<TLLoginSharedData*>(pView);
			if (p->magic == TL_LOGIN_MAGIC) {
				p->flags |= TL_CAP_REQ;
				DWORD waited = 0;
				const DWORD kCapTimeout = 10000;
				while (waited < kCapTimeout) {
					if (p->flags & TL_CAP_RDY) break;
					Sleep(100);
					waited += 100;
				}
				if (p->flags & TL_CAP_RDY) cookie = p->param0;
			}
			UnmapViewOfFile(pView);
		}
	}
	if (hMap) CloseHandle(hMap);

	if (r != IDOK) return;

	if (cookie.empty()) {
		MessageBox(m_hWnd, _STR(_T("未捕获到登录信息，请确认已在游戏中完成登录后再点确定")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return;
	}

	std::wstring strUsername = GetUserNameByCookie(cookie);
	DBWork db;
	db.InitDB();
	if (strUsername.empty()) {
		UserInfo user{ L"", L"", cookie, OBF(L"老棉裤账号"), 0, 0 };
		db.InsertUser(user);
	}
	else {
		UserInfo user{ strUsername, L"", cookie, strUsername, 0, 1 };
		db.InsertUser(user);
	}
	refreshUserList();
}

void CMainDlg::HandleMenuEditDetal(int nSel)
{
	CInputBoxDlg dlg;
	dlg.SetTipInfo(OBF(L"请输入新的备注")).SetText(gbl_Users[nSel].m_detal.c_str()).SetTitle(OBF(L"备注"));
	dlg.Create(this->m_hWnd, _STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME);
	if (IDOK == dlg.DoModal())
	{
		DBWork db;
		db.InitDB();
		gbl_Users[nSel].m_detal = dlg.GetText();
		db.UpdateUser(gbl_Users[nSel]);
		refreshUserList();
	}
}

void CMainDlg::HandleMenuEditPwd(int nSel)
{
	CAddUserDlg dlg;
	bool bNoPassword = gbl_Users[nSel].m_password.empty();   // 抓取导入、无密码账号
	dlg.SetEditMode(true, bNoPassword, gbl_Users[nSel].m_nType, gbl_Users[nSel].m_serverId);
	dlg.SetUsername(gbl_Users[nSel].m_username.c_str()).SetPassword(gbl_Users[nSel].m_password.c_str());
	dlg.Create(this->m_hWnd, _STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME);
	if (IDOK == dlg.DoModal())
	{
		int nNewType = dlg.GetSelectedNtype();
		int nNewSid  = dlg.GetSelectedSid();
		if (bNoPassword) {
			// 无密码（抓取）账号：不重跑联网登录，仅改区服（对话框内已移除台服选项，nNewType 必为 0）
			gbl_Users[nSel].m_nType = nNewType;
			gbl_Users[nSel].m_serverId = nNewSid;
			DBWork db;
			db.InitDB();
			db.UpdateUser(gbl_Users[nSel]);
			refreshUserList();
		}
		else if (nNewType == 100) {
			// 改为/保持台服：纯存，不联网
			gbl_Users[nSel].m_username = dlg.GetUsername();
			gbl_Users[nSel].m_password = dlg.GetPassword();
			gbl_Users[nSel].m_nType = 100;
			gbl_Users[nSel].m_serverId = 0;
			DBWork db;
			db.InitDB();
			db.UpdateUser(gbl_Users[nSel]);
			refreshUserList();
		}
		else {
			// 4399 账号：改区服。密码未变则不重登（离线也能改区）；密码变了才重登刷新 cookie。
			// 重登失败也兜底保存区服改动（沿用原账号信息），不会丢改动。
			// 注意：必须带上真实主键 m_nIndex，否则 UpdateUser 的 WHERE id=? 命中不到账号行（原代码写 0，改动被静默丢弃）。
			CMMString oldUser  = gbl_Users[nSel].m_username;
			CMMString oldPwd   = gbl_Users[nSel].m_password;
			CMMString oldToken = gbl_Users[nSel].m_token;
			CMMString detal    = gbl_Users[nSel].m_detal;
			int       nIndex   = gbl_Users[nSel].m_nIndex;
			bool      bPwdChanged = (dlg.GetPassword() != oldPwd);
			g_ThreadPool.submit([this, nNewSid, bPwdChanged, oldUser, oldPwd, oldToken, detal, nIndex](CMMString username, CMMString password) {
				CMMString saveUser = oldUser;
				CMMString savePwd  = oldPwd;
				CMMString token    = oldToken;
				if (bPwdChanged) {
					// 密码变了：联网重登刷新 cookie
					NetWork net;
					net.LoginUser(EncodingConverter::UnicodeToGBK(username.c_str()), EncodingConverter::UnicodeToGBK(password.c_str()));
					if (net.IsSuccess()) {
						token    = EncodingConverter::UTF8ToUnicode(net.GetCookie());
						saveUser = username;
						savePwd  = password;
					}
					else {
						// 重登失败：保留原账号信息，仅更新区服
						MessageBox(m_hWnd, _STR(_T("登录失败，已保留原登录信息并仅更新区服")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
					}
				}
				// 无论是否重登成功，都写回区服（及最新 cookie/密码）
				DBWork db;
				db.InitDB();
				UserInfo user{ saveUser, savePwd, token, detal, 0, nIndex };
				user.m_serverId = nNewSid;
				db.UpdateUser(user);
				this->PostMessage(WM_USER_LOGIN_SUCCESS);
				}, dlg.GetUsername(), dlg.GetPassword());
		}
	}
}

void CMainDlg::HandleMenuEditDll(int nSel)
{
	// 编辑 DLL 导入账号的登录信息：改为抓取模式重新捕获该账号的 cookie 并更新。
	std::wstring exePath = GetGamePath4399();
	if (exePath.empty()) {
		return;
	}
	std::wstring dllPath = GetHookDllPath();
	if (!PathFileExists(exePath.c_str())) {
		MessageBox(m_hWnd, _STR(_T("游戏启动器不存在，请检查游戏路径")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
		return;
	}

	TLLoginSharedData data = {};
	data.magic = TL_LOGIN_MAGIC;
	data.version = TL_LOGIN_VERSION;
	data.type = TL_ACCOUNT_CAPTURE;
	data.flags = 0;

	std::wstring cmdLine = L"\"" + exePath + OBF(L"\" https://my.4399.com/yxssjj/wd");

	HANDLE hMap = nullptr;
	DWORD pid = 0;
	if (!LaunchGameWithLogin(exePath, dllPath, data, cmdLine, &pid, &hMap)) {
		MessageBox(m_hWnd, _STR(_T("游戏启动失败，请检查路径与 DLL")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
		return;
	}

	int r = MessageBox(m_hWnd,
		_STR(_T("请在游戏客户端中手动完成登录，登录成功后再点击[确定]更新该账号的登录信息。\n（取消则不更新）")),
		_STR(_T("提示")), MB_OKCANCEL | MB_ICONINFORMATION);

	std::wstring cookie;
	if (r == IDOK && hMap) {
		// 命令-应答握手：置 TL_CAP_REQ 请求 DLL 立即抓取并回写 param0；
		// 再轮询 TL_CAP_RDY（带 10s 超时）后读取，避免读到 visit 异步回调尚未完成的空数据。
		void* pView = MapViewOfFile(hMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, TL_LOGIN_STRUCT_SIZE);
		if (pView) {
			auto* p = static_cast<TLLoginSharedData*>(pView);
			if (p->magic == TL_LOGIN_MAGIC) {
				p->flags |= TL_CAP_REQ;
				DWORD waited = 0;
				const DWORD kCapTimeout = 10000;
				while (waited < kCapTimeout) {
					if (p->flags & TL_CAP_RDY) break;
					Sleep(100);
					waited += 100;
				}
				if (p->flags & TL_CAP_RDY) cookie = p->param0;
			}
			UnmapViewOfFile(pView);
		}
	}
	if (hMap) CloseHandle(hMap);

	if (r != IDOK) return;

	if (cookie.empty()) {
		MessageBox(m_hWnd, _STR(_T("未捕获到登录信息，请确认已在游戏中完成登录后再点确定")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return;
	}

	gbl_Users[nSel].m_token = cookie;
	gbl_Users[nSel].m_username = GetUserNameByCookie(cookie);
	DBWork db;
	db.InitDB();
	db.UpdateUser(gbl_Users[nSel]);
	refreshUserList();
}

void CMainDlg::HandleMenuLoginSel(int nSel)
{
	LoginIdx(nSel);
}

void CMainDlg::HandleMenuDeleteSel(int nSel)
{
	#if TL_ENABLE_DEBUG_OUTPUT
	OutputDebugString((m_pListViewCtrl_UserCtrl->GetChildAt(m_pListViewCtrl_UserCtrl->GetCurSel())->GetText().c_str()));
	#endif
	DBWork db;
	db.InitDB();
	db.DeleteUser(gbl_Users[nSel]);
	refreshUserList();
}

void CMainDlg::HandleMenuCreateShortcut(int nSel)
{
	if (nSel < 0 || nSel >= (int)gbl_Users.size()) {
		return;
	}
	const UserInfo& user = gbl_Users[nSel];
	std::wstring strName = user.m_detal.empty() ? user.m_username : user.m_detal;
	if (CreateAccountShortcut((unsigned)user.m_nIndex, strName)) {
		MessageBox(m_hWnd, _STR(_T("桌面快捷方式创建成功")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
	}
	else {
		MessageBox(m_hWnd, _STR(_T("桌面快捷方式创建失败")), _STR(_T("提示")), MB_OK | MB_ICONERROR);
	}
}

void CMainDlg::HandleProtocolPayload(const std::wstring& strPayload)
{
	// ===== URL 协议 payload 分发框架 =====
	// 浏览器/桌面快捷方式唤起的 yxysoft://<payload> 最终统一到达这里
	// 当前只做格式识别与记录，具体执行操作后续按需补充
	if (strPayload.empty()) {
		return;
	}

	// 约定格式一：login-<账号id>（<账号id> 为数据库 user.id）
	// 容错：唤起方可能在 payload 尾部带 '/' 等杂字符，按前缀取数字段、遇非数字即止
	const std::wstring strLoginPrefix =OBF( L"login-");
	if (strPayload.size() > strLoginPrefix.size()
		&& _wcsnicmp(strPayload.c_str(), strLoginPrefix.c_str(), strLoginPrefix.size()) == 0)
	{
		std::wstring strId;
		for (size_t i = strLoginPrefix.size(); i < strPayload.size(); ++i) {
			const wchar_t ch = strPayload[i];
			if (ch < L'0' || ch > L'9') {
				break;
			}
			strId += ch;
		}
		if (!strId.empty()) {
			const int nAccountId = _wtoi(strId.c_str());
			// 账号 id（user.id）-> gbl_Users 列表索引，走与右键菜单同一登录逻辑
			for (size_t i = 0; i < gbl_Users.size(); ++i) {
				if (gbl_Users[i].m_nIndex == nAccountId) {
					#if TL_ENABLE_DEBUG_OUTPUT
					OutputDebugStringW((OBF(L"[UrlProtocol] login account id=") + strId).c_str());
					#endif
					LoginIdx((int)i);
					return;
				}
			}
			#if TL_ENABLE_DEBUG_OUTPUT
			OutputDebugStringW((OBF(L"[UrlProtocol] account not found, id=") + strId).c_str());
			#endif
			MessageBox(m_hWnd, _STR(_T("未找到对应账号，请确认该账号是否已添加")), _STR(_T("提示")), MB_OK | MB_ICONWARNING);
			return;
		}
	}

	// TODO(后续)：其他扩展操作按 payload 前缀在此继续分发
	#if TL_ENABLE_DEBUG_OUTPUT
	OutputDebugStringW((OBF(L"[UrlProtocol] unhandled payload: ") + strPayload).c_str());
	#endif
}

std::string CMainDlg::GetGamePathDirectory()
{
	auto gamepath = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePath"), "");
	if (gamepath.empty()) {
		MessageBox(m_hWnd, _STR(_T("请设置游戏路径")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return "";
	}
	size_t pos = gamepath.find_last_of('\\');
	if (pos != std::string::npos) {
		return gamepath.substr(0, pos);
	}
	MessageBox(m_hWnd, _STR(_T("游戏路径格式错误")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
	return "";
}

std::wstring CMainDlg::GetSSJJInstallPath(int nType)
{
	// 默认安装注册表位置（64 位 native 视图，已核对本机真实存在）：
	//   4399 : HKCU\SOFTWARE\Wooduan\SSJJ-4399
	//   台服 : HKCU\SOFTWARE\Wooduan\SSJJ-tw
	// 值 InstallPath 指向安装根目录（不含 MicroClient64\启动器）
	std::wstring pszSubKey = (nType == TL_ACCOUNT_TAIWAN)
		? OBF(L"SOFTWARE\\Wooduan\\SSJJ-tw")
		: OBF(L"SOFTWARE\\Wooduan\\SSJJ-4399");

	// 启动器候选（按优先级尝试，命中第一个存在的文件）。
	// 4399 启动器为 MicroClient64\MicroClient.exe（字母 l，非数字 1）；
	// 台服为 LiuseLoader.exe。
	std::wstring pszExeCandidates;
	int nCandidates = 0;
	if (nType == TL_ACCOUNT_TAIWAN) {
		pszExeCandidates = OBF(L"MicroClient64\\LiuseLoader.exe");
		nCandidates = 1;
	}
	else {
		pszExeCandidates =OBF( L"MicroClient64\\MicroClient.exe"); // 字母 l，非数字 1
		nCandidates = 1;
	}

	HKEY hKey = NULL;
	wchar_t szInstall[MAX_PATH] = { 0 };
	DWORD dwType = 0;
	DWORD dwLen = sizeof(szInstall); // 字节数，RegQueryValueEx 要求以字节计
	LONG lRet = RegOpenKeyExW(HKEY_CURRENT_USER, pszSubKey.c_str(), 0, KEY_QUERY_VALUE, &hKey);
	if (lRet != ERROR_SUCCESS) {
		return L"";
	}
	lRet = RegQueryValueExW(hKey, OBF(L"InstallPath"), NULL, &dwType, (LPBYTE)szInstall, &dwLen);
	RegCloseKey(hKey);
	if (lRet != ERROR_SUCCESS || szInstall[0] == 0) {
		return L"";
	}

	// 规整尾随反斜杠
	std::wstring strInstall = szInstall;
	if (!strInstall.empty() && strInstall.back() != L'\\') {
		strInstall += L'\\';
	}

	for (int i = 0; i < nCandidates; ++i) {
		std::wstring strFull = strInstall + pszExeCandidates[i];
		if (PathFileExistsW(strFull.c_str())) {
			return strFull;
		}
	}
	return L"";
}

std::wstring CMainDlg::GetGamePathTW()
{
	auto gamepath_tw = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePathTW"), "");
	if (gamepath_tw.empty()) {
		MessageBox(m_hWnd, _STR(_T("请设置台服启动器路径")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return L"";
	}
	return EncodingConverter::GBKToUnicode(gamepath_tw);
}

std::wstring CMainDlg::GetGamePath4399()
{
	auto gamepath = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePath"), "");
	if (gamepath.empty()) {
		MessageBox(m_hWnd, _STR(_T("请设置游戏路径")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		if (!ShowSettingDialog()) {
			return L"";
		}
		gamepath = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePath"), "");
		if (gamepath.empty()) {
			return L"";
		}
	}
	return EncodingConverter::GBKToUnicode(gamepath);
}

void CMainDlg::RepairGamePathsFromRegistry()
{
	// 启动阶段：若配置里的游戏路径为空或文件不存在，则从注册表探测并写回配置（持久化）。
	// 之后启动游戏时直接读配置即可，不在启动那一刻用注册表替代。

	// 4399 : GamePath
	{
		std::string strCfg = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePath"), "");
		std::wstring wCfg = EncodingConverter::GBKToUnicode(strCfg);
		if (strCfg.empty() || !PathFileExistsW(wCfg.c_str())) {
			std::wstring strReg = GetSSJJInstallPath(TL_ACCOUNT_4399);
			if (!strReg.empty()) {
				g_ConfigManager.set(OBF("SETTING"), OBF("GamePath"), EncodingConverter::UnicodeToGBK(strReg));
				g_ConfigManager.save();
			}
		}
	}

	// 台服 : GamePathTW
	{
		std::string strCfg = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePathTW"), "");
		std::wstring wCfg = EncodingConverter::GBKToUnicode(strCfg);
		if (strCfg.empty() || !PathFileExistsW(wCfg.c_str())) {
			std::wstring strReg = GetSSJJInstallPath(TL_ACCOUNT_TAIWAN);
			if (!strReg.empty()) {
				g_ConfigManager.set(OBF("SETTING"), OBF("GamePathTW"), EncodingConverter::UnicodeToGBK(strReg));
				g_ConfigManager.save();
			}
		}
	}
}

std::wstring CMainDlg::GetHookDllPath()
{
	// DLL 路径可配置；未配置时默认 exe 同目录 TestLoaderHook.dll
	auto dllpath = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("DllPath"), "");
	if (!dllpath.empty()) {
		return EncodingConverter::GBKToUnicode(dllpath);
	}
	wchar_t szExePath[MAX_PATH] = {};
	GetModuleFileNameW(NULL, szExePath, MAX_PATH);
	std::wstring strDll = szExePath;
	size_t pos = strDll.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		strDll = strDll.substr(0, pos + 1);
	}
	strDll += OBF(L"loader.dll");
	return strDll;
}

bool CMainDlg::ShowSettingDialog()
{
	CSettingDlg dlg;
	if (!dlg.Create(this->m_hWnd, _STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME)) {
		MessageBox(m_hWnd, _STR(_T("对话框创建失败")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
		return false;
	}
	dlg.DoModal();
	return true;
}

void CMainDlg::OnDuiItemDbClick_IDC_ListViewCtrl_100013(const DuiNotify& Notify)
{
	int nSel = m_pListViewCtrl_UserCtrl->GetCurSel();
	if (nSel == -1) {
		MessageBox(m_hWnd, _STR(_T("请选择一个用户")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return;
	}
	LoginIdx(nSel);
#if 0

	auto gamepath = g_ConfigManager.get<std::string>(OBF("SETTING"), OBF("GamePath"), "");
	std::string gameExecutablePath = gamepath;
	if (gamepath.empty()) {
		MessageBox(m_hWnd, _STR(_T("请设置游戏路径")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		CSettingDlg dlg;
		if (!dlg.Create(this->m_hWnd, _STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME)) {
			MessageBox(m_hWnd, _STR(_T("对话框创建失败")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
			return;
		}
		dlg.DoModal();
		return;
	} 
	size_t pos = gamepath.find_last_of('\\');
	if (pos != std::string::npos) {
		gamepath = gamepath.substr(0, pos);
	}
	else {
		MessageBox(m_hWnd, _STR(_T("游戏路径格式错误")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return;
	}
	auto ini_path = EncodingConverter::GBKToUnicode(gamepath + OBF("\\配置信息.ini"));
	if (!PathFileExists(ini_path.c_str())) {
		MessageBox(m_hWnd, _STR(_T("配置文件不存在,请先配置老棉裤")), _STR(_T("提示")), MB_OK | MB_ICONINFORMATION);
		return;
	}
	// 				std::wstring strTmp;
	// 				strTmp.resize(0x7FFF);
	// 				DWORD nCount = GetPrivateProfileStringW(CONFIG_SETTING, CONFIG_COOKIE, L"", (wchar_t*)strTmp.data(), 0x7FFF, ini_path.c_str());
	// 				std::wstring Cooki
	WritePrivateProfileString(CONFIG_SETTING, CONFIG_COOKIE, gbl_Users[nSel].m_token.c_str(), ini_path.c_str());
	// 启动游戏
	HINSTANCE hResult = ShellExecuteA(NULL, "open", gameExecutablePath.c_str(), "", NULL, SW_SHOWNORMAL);
	if ((intptr_t)hResult <= 32) {
		MessageBox(m_hWnd, _STR(_T("游戏启动失败")), _STR(_T("错误")), MB_OK | MB_ICONERROR);
	}
#endif
	return;
}
