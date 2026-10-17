#pragma once

//////////////////////////////////////////////////////////////////////////
class CInputBoxDlg
	: public CDUIWnd
{
	MMDeclare_ClassName(CInputBoxDlg)
	DuiDeclare_Message_Map()
private:
	//data
	CMMString m_strText; // 输入内容
	CMMString m_strTipInfo; // 提示信息
	CMMString m_strTitle; // 标题
public:
	//get/set
	CInputBoxDlg& SetText(const CMMString& strText) { m_strText = strText; return *this; }
	const CMMString& GetText() const { return m_strText; }
	CInputBoxDlg& SetTipInfo(const CMMString& strTipInfo) { m_strTipInfo = strTipInfo;  return *this; }
	const CMMString& GetTipInfo() const { return m_strTipInfo; }
	CInputBoxDlg& SetTitle(const CMMString& strTitle) { m_strTitle = strTitle; return *this; }
	const CMMString& GetTitle() const { return m_strTitle; }
	static const CMMString ShowInputBox(HWND hWnd, const CMMString& strTitle, const CMMString& strTipInfo, const CMMString& strDefaultText = _T(""));
public:
	CInputBoxDlg();
	virtual ~CInputBoxDlg();

	//control
protected:
	CDUIEditCtrl *					m_pEditCtrl_Text = NULL;
	CDUIStaticCtrl *					m_pStaticCtrl_TipInfo = NULL;
	CDUIStaticCtrl *					m_pDui_CtrlIDInner_Title = NULL;

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
	void OnDuiClick_IDC_ButtonCtrl_100048(const DuiNotify &Notify);

	//help
protected:
};
