#include "framework.h"
#include "CInputBoxDlg.h"
#define  _STR(x)	    (x)
//////////////////////////////////////////////////////////////////////////
MMImplement_ClassName(CInputBoxDlg)
DuiBegin_Message_Map(CInputBoxDlg, CDUIWnd)
	Dui_On_Notify(DuiNotify_WndInited, OnDuiWndInited)
	Dui_On_Notify(DuiNotify_WndLayout, OnDuiWndLayout)
	Dui_On_Notify_Ctrl(DuiNotify_Click, IDC_ButtonCtrl_100048, OnDuiClick_IDC_ButtonCtrl_100048)
DuiEnd_Message_Map()

CInputBoxDlg::CInputBoxDlg()
	: CDUIWnd(_T("DlgInputBox"))
{
	return;
}

CInputBoxDlg::~CInputBoxDlg()
{
	return;
}

void CInputBoxDlg::OnFindControl()
{
	Dui_DDX_Control(CDUIEditCtrl, m_pEditCtrl_Text, IDC_EditCtrl_100047);
	Dui_DDX_Control(CDUIStaticCtrl, m_pStaticCtrl_TipInfo, IDC_StaticCtrl_100046);
	Dui_DDX_Control(CDUIStaticCtrl, m_pDui_CtrlIDInner_Title, Dui_CtrlIDInner_DlgTitle);

	return;
}
const CMMString CInputBoxDlg::ShowInputBox(HWND hWnd,const CMMString& strTitle, const CMMString& strTipInfo, const CMMString& strDefaultText/* = _T("")*/)
{
	CInputBoxDlg box;
	box.Create(hWnd,_STR(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME);
	auto res = box.SetTitle(strTitle).SetTipInfo(strTipInfo).SetText(strDefaultText).DoModal();
	if (res == IDOK)
	{
		return box.GetText();
	}
	return _T("");
}
void CInputBoxDlg::OnInitDialog()
{
	// init dlg text
	m_pDui_CtrlIDInner_Title->SetText(m_strTitle);
	m_pStaticCtrl_TipInfo->SetText(m_strTipInfo);
	m_pEditCtrl_Text->SetText(m_strText);
	return;
}

void CInputBoxDlg::OnDuiWndInited(const DuiNotify &Notify)
{
	return;
}

void CInputBoxDlg::OnDuiWndLayout(const DuiNotify &Notify)
{
	return;
}

void CInputBoxDlg::OnDuiClick_IDC_ButtonCtrl_100048(const DuiNotify &Notify)
{
	m_strText = m_pEditCtrl_Text->GetText();
	__super::Close(IDOK);
	return;
}
