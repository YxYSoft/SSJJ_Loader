#include "framework.h"
#include "CSplashDlg.h"

//////////////////////////////////////////////////////////////////////////
MMImplement_ClassName(CSplashDlg)
DuiBegin_Message_Map(CSplashDlg, CDUIWnd)
Dui_On_Notify(DuiNotify_WndInited, OnDuiWndInited)
Dui_On_Notify(DuiNotify_WndLayout, OnDuiWndLayout)
DuiEnd_Message_Map()

std::atomic_bool g_finsh{ false };

CSplashDlg::CSplashDlg()
	: CDUIWnd(_T("DlgSplash"))
{
	return;
}

CSplashDlg::~CSplashDlg()
{
	return;
}

void CSplashDlg::OnFindControl()
{
	Dui_DDX_Control(CDUIAnimateCtrl, m_pAnimateCtrl_100116Ctrl, IDC_AnimateCtrl_100116);
	
	return;
}

void CSplashDlg::OnInitDialog()
{
	SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	m_pAnimateCtrl_100116Ctrl->SetSequenceFrameSpeed(100);
	m_pAnimateCtrl_100116Ctrl->PlayAnimate();
 	std::thread([this]() {
		int nCount = m_pAnimateCtrl_100116Ctrl->GetFrameCount();
		do 
		{
			Sleep(100 * nCount-1);
		} while (FALSE);
		m_pAnimateCtrl_100116Ctrl->PauseAnimate();
		g_finsh.store(true);
		Sleep(1000);
 		PostMessage(WM_CLOSE);
 	}).detach();
	return;
}

void CSplashDlg::OnDuiWndInited(const DuiNotify &Notify)
{
	return;
}

void CSplashDlg::OnDuiWndLayout(const DuiNotify &Notify)
{
	return;
}
