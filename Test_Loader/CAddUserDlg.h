#pragma once

#include <vector>

#include "framework.h"

//////////////////////////////////////////////////////////////////////////
class CAddUserDlg
	: public CDUIWnd
{
	MMDeclare_ClassName(CAddUserDlg)
	DuiDeclare_Message_Map()
private:

	//data
	CMMString m_strUsername;
	CMMString m_strPassword;
public:
	CMMString GetUsername() const { return m_strUsername; }
	CMMString GetPassword() const { return m_strPassword; }
	CAddUserDlg& SetUsername(const CMMString& strUsername) { m_strUsername = strUsername; return *this; }
	CAddUserDlg& SetPassword(const CMMString& strPassword) { m_strPassword = strPassword;  return *this; }

	// 区服表（C++ 唯一数据源；双线七区直接在此维护，无需改 XML）
	struct ZoneInfo {
		CMMString name;   // 下拉显示名
		int ntype;        // 100=台服，0=4399
		int sid;          // 4399 服 ID（低 8 位写入共享内存 flags），台服置 0
	};
	static const ZoneInfo kZones[];
	static const int kZoneCount;

	// 由 (ntype, sid) 求区服短标签，用于主列表/托盘前缀：
	//   联通X区->联X，电信X区->电X，双线X区->双X，台服壹区->台服，4399默认->最近
	static CMMString GetZoneShortTag(int nType, int nSid);

	// 编辑模式参数（Create 前调用 SetEditMode，OnInitDialog 读取）
	bool m_bEditMode = false;
	bool m_bNoPassword = false;   // 抓取导入、无密码账号
	int  m_nInitType = 0;
	int  m_nInitSid = 0;
	int  m_nInitSel = 1;   // 预选区服在 kZones[] 中的索引（折叠框显示 + 展开高亮）

	// 下拉框可见项对应的 kZones 索引（无密码编辑模式不含台服）
	std::vector<int> m_vecVisible;

	void SetEditMode(bool bEdit, bool bNoPassword, int nType, int nSid);
	int  GetSelectedNtype() const;
	int  GetSelectedSid() const;

public:
	CAddUserDlg();
	virtual ~CAddUserDlg();

	//control
protected:
	CDUIEditCtrl *					m_pEditCtrl_username = NULL;
	CDUIEditCtrl *					m_pEditCtrl_password = NULL;
	CDUIComboxCtrl *				m_pComboxCtrl_100153Ctrl = NULL;

	//override
protected:
	void OnFindControl() override;
	void OnInitDialog() override;

	//message
protected:
	void OnDuiWndInited(const DuiNotify &Notify);
	void OnDuiWndLayout(const DuiNotify &Notify);
	void OnDuiClick_IDC_ButtonCtrl_100036(const DuiNotify &Notify);
	void OnComboxExpand(const DuiNotify &Notify);
};
