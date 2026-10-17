#include "framework.h"
#include "CAddUserDlg.h"
#include <obfusheader.h>

//////////////////////////////////////////////////////////////////////////
MMImplement_ClassName(CAddUserDlg)
DuiBegin_Message_Map(CAddUserDlg, CDUIWnd)
	Dui_On_Notify(DuiNotify_WndInited, OnDuiWndInited)
	Dui_On_Notify(DuiNotify_WndLayout, OnDuiWndLayout)
	Dui_On_Notify_Ctrl(DuiNotify_Click, IDC_ButtonCtrl_100036, OnDuiClick_IDC_ButtonCtrl_100036)
	Dui_On_Notify_Ctrl(DuiNotify_ComboxExpand, IDC_ComboxCtrl_100153, OnComboxExpand)
DuiEnd_Message_Map()

// C++ 唯一数据源：区服名 -> (ntype, sid)。顺序即下拉显示顺序。
// sid 取自 version.cpp 权威表（DLL 端 Get4399ServerObj 已与之对齐）。
const CAddUserDlg::ZoneInfo CAddUserDlg::kZones[] = {
	{ CMMString(OBF(L"台服壹区")),   100, 0  },
	{ CMMString(OBF(L"4399默认")),   0,   0  },
	{ CMMString(OBF(L"联通一区")),   0,   3  },
	{ CMMString(OBF(L"联通二区")),   0,   5  },
	{ CMMString(OBF(L"联通三区")),   0,   8  },
	{ CMMString(OBF(L"联通四区")),   0,   10 },
	{ CMMString(OBF(L"联通五区")),   0,   14 },
	{ CMMString(OBF(L"联通六区")),   0,   16 },
	{ CMMString(OBF(L"电信一区")),   0,   2  },
	{ CMMString(OBF(L"电信二区")),   0,   4  },
	{ CMMString(OBF(L"电信三区")),   0,   6  },
	{ CMMString(OBF(L"电信四区")),   0,   7  },
	{ CMMString(OBF(L"电信五区")),   0,   9  },
	{ CMMString(OBF(L"电信六区")),   0,   11 },
	{ CMMString(OBF(L"电信七区")),   0,   13 },
	{ CMMString(OBF(L"电信八区")),   0,   15 },
	{ CMMString(OBF(L"双线一区")),   0,   1  },
	{ CMMString(OBF(L"双线二区")),   0,   12 },
	{ CMMString(OBF(L"双线三区")),   0,   17 },
	{ CMMString(OBF(L"双线四区")),   0,   18 },
	{ CMMString(OBF(L"双线五区")),   0,   19 },
	{ CMMString(OBF(L"双线六区")),   0,   20 },
	{ CMMString(OBF(L"双线七区")),   0,   21 },
};
const int CAddUserDlg::kZoneCount = (int)(sizeof(kZones) / sizeof(kZones[0]));

// 由 (ntype, sid) 求区服短标签，用于账号列表/托盘前缀显示
CMMString CAddUserDlg::GetZoneShortTag(int nType, int nSid)
{
	// 1) 先按 (ntype, sid) 找回完整区服名
	CMMString name;
	for (int i = 0; i < kZoneCount; ++i) {
		if (kZones[i].ntype == nType && kZones[i].sid == nSid) {
			name = kZones[i].name;
			break;
		}
	}
	if (name.empty()) {
		// 未命中表时按类型兜底（台服账号 / 最近服）
		if (nType == 100) name = OBF(L"台服壹区");
		else name = OBF(L"4399默认");
	}

	// 2) 完整区服名 -> 短标签
	if (name == OBF(L"台服壹区")) return OBF(L"台服");
	if (name == OBF(L"4399默认")) return OBF(L"最近");

	size_t posQu = name.find(OBF(L"区"));
	CMMString mid = (posQu == CMMString::npos) ? name.substr(2) : name.substr(2, posQu - 2);

	if (name.compare(0, 2, OBF(L"联通")) == 0) return OBF(L"联") + mid;
	if (name.compare(0, 2, OBF(L"电信")) == 0) return OBF(L"电") + mid;
	if (name.compare(0, 2, OBF(L"双线")) == 0) return OBF(L"双") + mid;

	return L"";   // 未知兜底：无前缀
}

CAddUserDlg::CAddUserDlg()
	: CDUIWnd(_T("DlgAddUser"))
{
	return;
}

CAddUserDlg::~CAddUserDlg()
{
	return;
}

void CAddUserDlg::SetEditMode(bool bEdit, bool bNoPassword, int nType, int nSid)
{
	m_bEditMode = bEdit;
	m_bNoPassword = bNoPassword;
	m_nInitType = nType;
	m_nInitSid = nSid;
}

void CAddUserDlg::OnFindControl()
{
	Dui_DDX_Control(CDUIEditCtrl, m_pEditCtrl_username, IDC_EditCtrl_100031);
	Dui_DDX_Control(CDUIEditCtrl, m_pEditCtrl_password, IDC_EditCtrl_100034);
	Dui_DDX_Control(CDUIComboxCtrl, m_pComboxCtrl_100153Ctrl, IDC_ComboxCtrl_100153);

	return;
}

void CAddUserDlg::OnInitDialog()
{
	m_pEditCtrl_username->SetText(m_strUsername);
	m_pEditCtrl_password->SetText(m_strPassword);

	// 下拉项实际来自 ViewCombobox_Server.xml，与 kZones[] 同序（索引一一对应）。
	// 框架在用户点击展开时才懒加载 m_pShowListView（InitDialog 时仍为 NULL），
	// 因此这里无法也不应去操作 listview；只需按 (ntype, sid) 算出原区服在 kZones[]
	// 中的索引，直接设置折叠框显示文本；展开瞬间 (OnComboxExpand) 再高亮对应项。
	// 新增模式默认选中“4399默认”(索引 1)；编辑模式匹配原账号区服。
	m_nInitSel = 1;   // 默认 4399默认
	if (m_bEditMode) {
		for (int i = 0; i < kZoneCount; ++i) {
			if (kZones[i].ntype == m_nInitType && kZones[i].sid == m_nInitSid) {
				m_nInitSel = i;
				break;
			}
		}
	}
	// 无密码（抓取）编辑模式：不允许切到台服，回落到 4399默认
	if (m_bNoPassword && kZones[m_nInitSel].ntype == 100) m_nInitSel = 1;

	// 折叠框直接显示已存/默认区服名
	m_pComboxCtrl_100153Ctrl->SetText(kZones[m_nInitSel].name);

	// 无密码账号：禁用账号/密码输入框（仅允许改区服）
	if (m_bNoPassword) {
		m_pEditCtrl_username->SetEnabled(false);
		m_pEditCtrl_password->SetEnabled(false);
	}

	return;
}

void CAddUserDlg::OnDuiWndInited(const DuiNotify &Notify)
{
	return;
}

void CAddUserDlg::OnDuiWndLayout(const DuiNotify &Notify)
{
	return;
}

void CAddUserDlg::OnDuiClick_IDC_ButtonCtrl_100036(const DuiNotify &Notify)
{
	m_strUsername = m_pEditCtrl_username->GetText();
	m_strPassword = m_pEditCtrl_password->GetText();
	__super::Close(IDOK);
	return;
}

void CAddUserDlg::OnComboxExpand(const DuiNotify &Notify)
{
	// 下拉展开时 m_pShowListView 已由框架 LoadDui 创建完毕，
	// 预高亮原区服项（与折叠框显示文本保持一致）。
	if (m_pComboxCtrl_100153Ctrl)
		m_pComboxCtrl_100153Ctrl->SelectItem(m_nInitSel);
	return;
}

int CAddUserDlg::GetSelectedNtype() const
{
	int sel = m_pComboxCtrl_100153Ctrl->GetCurSel();
	if (sel >= 0 && sel < (int)m_vecVisible.size())
		return kZones[m_vecVisible[sel]].ntype;
	// 兜底：按选中文本查表（应对视图未成功加载、仍显示 XML 默认项的情况）
	CMMString txt = m_pComboxCtrl_100153Ctrl->GetText();
	for (int i = 0; i < kZoneCount; ++i)
		if (kZones[i].name == txt) return kZones[i].ntype;
	return 0;
}

int CAddUserDlg::GetSelectedSid() const
{
	int sel = m_pComboxCtrl_100153Ctrl->GetCurSel();
	if (sel >= 0 && sel < (int)m_vecVisible.size())
		return kZones[m_vecVisible[sel]].sid;
	// 兜底：按选中文本查表（应对视图未成功加载、仍显示 XML 默认项的情况）
	CMMString txt = m_pComboxCtrl_100153Ctrl->GetText();
	for (int i = 0; i < kZoneCount; ++i)
		if (kZones[i].name == txt) return kZones[i].sid;
	return 0;
}
