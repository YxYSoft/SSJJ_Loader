#pragma once



//////////////////////////////////////////////////////////////////////////
class CSplashDlg
	: public CDUIWnd
{
	MMDeclare_ClassName(CSplashDlg)
	DuiDeclare_Message_Map()

public:
	CSplashDlg();
	virtual ~CSplashDlg();

	//control
protected:
	CDUIAnimateCtrl*					m_pAnimateCtrl_100116Ctrl = NULL;

	//variant
protected:

	//override
protected:
	void OnFindControl() override;
	void OnInitDialog() override;

	//message
protected:

	//notify
protected:
	void OnDuiWndInited(const DuiNotify &Notify);
	void OnDuiWndLayout(const DuiNotify &Notify);

	//help
protected:
};
