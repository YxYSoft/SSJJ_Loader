#pragma once

//////////////////////////////////////////////////////////////////////////
class CSettingDlg
	: public CDUIWnd
{
	MMDeclare_ClassName(CSettingDlg)
	DuiDeclare_Message_Map()

public:
	CSettingDlg();
	virtual ~CSettingDlg();
	inline bool IsRedioCheck(DWORD dwId);
	//control
protected:
	CDUIEditCtrl *					m_pEditCtrl_GamePathCtrl = NULL;
	CDUIEditCtrl *					m_pEditCtrl_GamePathTWCtrl = NULL;
	CDUIEditCtrl *					m_pEditCtrl_CustomAudioCtrl = NULL;
	CDUIComboxCtrl *					m_pComboxCtrl_Audio = NULL;

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
	void OnDuiItemSelected_IDC_ComboxCtrl_100071(const DuiNotify &Notify);
	void OnDuiSelectChanged_IDC_RadioBoxCtrl_100073(const DuiNotify &Notify);
	void OnDuiSelectChanged_IDC_RadioBoxCtrl_100074(const DuiNotify &Notify);
	void OnDuiSelectChanged_IDC_RadioBoxCtrl_100118(const DuiNotify &Notify);
	void OnDuiSelectChanged_IDC_RadioBoxCtrl_100119(const DuiNotify &Notify);
	void OnDuiClick_IDC_ButtonCtrl_selcostomaudio(const DuiNotify &Notify);
	void OnDuiClick_IDC_ButtonCtrl_SelectGamePath(const DuiNotify &Notify);
	void OnDuiClick_IDC_ButtonCtrl_SelectGamePathTW(const DuiNotify &Notify);

	//help
protected:
};
