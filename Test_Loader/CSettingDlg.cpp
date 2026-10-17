#include "framework.h"
#include "CSettingDlg.h"
#include "FileOpenSave.hpp"
#include <array>


#include <ConfigManager.hpp>
#include <EncodingConverter.hpp>

#include <obfusheader.h>
extern ConfigManager g_ConfigManager;



//////////////////////////////////////////////////////////////////////////
MMImplement_ClassName(CSettingDlg)
DuiBegin_Message_Map(CSettingDlg, CDUIWnd)
	Dui_On_Notify(DuiNotify_WndInited, OnDuiWndInited)
	Dui_On_Notify(DuiNotify_WndLayout, OnDuiWndLayout)
	Dui_On_Notify_Ctrl(DuiNotify_ItemSelected, IDC_ComboxCtrl_100071, OnDuiItemSelected_IDC_ComboxCtrl_100071)
	Dui_On_Notify_Ctrl(DuiNotify_SelectChanged, IDC_RadioBoxCtrl_100073, OnDuiSelectChanged_IDC_RadioBoxCtrl_100073)
	Dui_On_Notify_Ctrl(DuiNotify_SelectChanged, IDC_RadioBoxCtrl_100074, OnDuiSelectChanged_IDC_RadioBoxCtrl_100074)
	Dui_On_Notify_Ctrl(DuiNotify_SelectChanged, IDC_RadioBoxCtrl_100118, OnDuiSelectChanged_IDC_RadioBoxCtrl_100118)
	Dui_On_Notify_Ctrl(DuiNotify_SelectChanged, IDC_RadioBoxCtrl_100119, OnDuiSelectChanged_IDC_RadioBoxCtrl_100119)
	Dui_On_Notify_Ctrl(DuiNotify_Click, IDC_ButtonCtrl_selcostomaudio, OnDuiClick_IDC_ButtonCtrl_selcostomaudio)
	Dui_On_Notify_Ctrl(DuiNotify_Click, IDC_ButtonCtrl_SelectGamePath, OnDuiClick_IDC_ButtonCtrl_SelectGamePath)
	Dui_On_Notify_Ctrl(DuiNotify_Click, IDC_ButtonCtrl_SelectGamePathTW, OnDuiClick_IDC_ButtonCtrl_SelectGamePathTW)
DuiEnd_Message_Map()

CSettingDlg::CSettingDlg()
	: CDUIWnd(_T("DlgSetting"))
{
	return;
}

CSettingDlg::~CSettingDlg()
{
	return;
}

void CSettingDlg::OnFindControl()
{
	Dui_DDX_Control(CDUIEditCtrl, m_pEditCtrl_GamePathCtrl, IDC_EditCtrl_GamePath);
	Dui_DDX_Control(CDUIEditCtrl, m_pEditCtrl_GamePathTWCtrl, IDC_EditCtrl_GamePathTW);
	Dui_DDX_Control(CDUIEditCtrl, m_pEditCtrl_CustomAudioCtrl, IDC_EditCtrl_CustomAudio);
	Dui_DDX_Control(CDUIComboxCtrl, m_pComboxCtrl_Audio, IDC_ComboxCtrl_100071);

	return;
}
bool CSettingDlg::IsRedioCheck(DWORD dwId) {
	CDUIRadioBoxCtrl* pRadioBoxCtrl;
	Dui_DDX_Control(CDUIRadioBoxCtrl, pRadioBoxCtrl, dwId);
	return pRadioBoxCtrl->IsSelected();
}
void CSettingDlg::OnInitDialog()
{
	auto audio_sel = g_ConfigManager.get(OBF("SETTING"), OBF("InnerAudioSel"), 1);
	std::string audio_path = g_ConfigManager.get(OBF("SETTING"), OBF("AudioPath"),std::string( ""));
	std::string game_path = g_ConfigManager.get(OBF("SETTING"), OBF("GamePath"), std::string(""));
	std::string game_path_tw = g_ConfigManager.get(OBF("SETTING"), OBF("GamePathTW"), std::string(""));
	auto audio_sel_mode = g_ConfigManager.get(OBF("SETTING"), OBF("AudioSel"), 1);

	m_pEditCtrl_GamePathCtrl->SetText( EncodingConverter::GBKToUnicode( game_path).c_str());
	m_pEditCtrl_GamePathTWCtrl->SetText(EncodingConverter::GBKToUnicode(game_path_tw).c_str());
	m_pEditCtrl_CustomAudioCtrl->SetText(EncodingConverter::GBKToUnicode(audio_path).c_str());
	m_pComboxCtrl_Audio->SelectItem(audio_sel);
	std::array<std::wstring, 4> audio_sel_arr  { OBF(L"关闭音效"), OBF(L"内置音效1"), OBF(L"内置音效2"), OBF(L"内置音效3") };
	m_pComboxCtrl_Audio->SetText(audio_sel_arr[audio_sel].c_str());
	CDUIRadioBoxCtrl* pRadioBoxCtrl_inner;
	Dui_DDX_Control(CDUIRadioBoxCtrl, pRadioBoxCtrl_inner, IDC_RadioBoxCtrl_100073);
	CDUIRadioBoxCtrl* pRadioBoxCtrl_custom;
	Dui_DDX_Control(CDUIRadioBoxCtrl, pRadioBoxCtrl_custom, IDC_RadioBoxCtrl_100074);
	
	if (audio_sel_mode == 1) {
		pRadioBoxCtrl_inner->Select(true,false);
	}
	else {
		pRadioBoxCtrl_custom->Select(true,false);
	}

	CDUIRadioBoxCtrl* pRadioBoxCtrl_exit;
	Dui_DDX_Control(CDUIRadioBoxCtrl, pRadioBoxCtrl_exit, IDC_RadioBoxCtrl_100118);
	CDUIRadioBoxCtrl* pRadioBoxCtrl_mini;
	Dui_DDX_Control(CDUIRadioBoxCtrl, pRadioBoxCtrl_mini, IDC_RadioBoxCtrl_100119);
	if (g_ConfigManager.get(OBF("SETTING"), OBF("CloseOpt"), 0) == 0) {
		pRadioBoxCtrl_exit->Select(true, false);
	}
	else {
		pRadioBoxCtrl_mini->Select(true, false);
	}



	return;
}

void CSettingDlg::OnDuiWndInited(const DuiNotify &Notify)
{
	return;
}

void CSettingDlg::OnDuiWndLayout(const DuiNotify &Notify)
{
	return;
}

void CSettingDlg::OnDuiItemSelected_IDC_ComboxCtrl_100071(const DuiNotify &Notify)
{
	//OutputDebugString(m_pComboxCtrl_Audio->GetText());
	g_ConfigManager.set(OBF("SETTING"), OBF("InnerAudioSel"), m_pComboxCtrl_Audio->GetCurSel());
	g_ConfigManager.save();
	;
	return;
}

void CSettingDlg::OnDuiSelectChanged_IDC_RadioBoxCtrl_100073(const DuiNotify &Notify)
{
	if(IsRedioCheck( IDC_RadioBoxCtrl_100073 ))		{
		//OutputDebugString(L"选中内置音效");
		g_ConfigManager.set(OBF("SETTING"), OBF("AudioSel"), 1);
		g_ConfigManager.save();

	}
	return;
}

void CSettingDlg::OnDuiSelectChanged_IDC_RadioBoxCtrl_100074(const DuiNotify &Notify)
{
	if (IsRedioCheck(IDC_RadioBoxCtrl_100074)) {
		//OutputDebugString(L"选中外置音效");
		g_ConfigManager.set(OBF("SETTING"), OBF("AudioSel"), 0);
		g_ConfigManager.save();
	}
	
	return;
}

void CSettingDlg::OnDuiSelectChanged_IDC_RadioBoxCtrl_100118(const DuiNotify &Notify)
{
	if (IsRedioCheck(IDC_RadioBoxCtrl_100118)) {
		// 直接退出
		g_ConfigManager.set(OBF("SETTING"), OBF("CloseOpt"), 0);
		g_ConfigManager.save();
	}
	return;
}

void CSettingDlg::OnDuiSelectChanged_IDC_RadioBoxCtrl_100119(const DuiNotify &Notify)
{
	if (IsRedioCheck(IDC_RadioBoxCtrl_100119)) {
		// 最小化到托盘
		g_ConfigManager.set(OBF("SETTING"), OBF("CloseOpt"), 1);
		g_ConfigManager.save();
	}
	return;
}

void CSettingDlg::OnDuiClick_IDC_ButtonCtrl_selcostomaudio(const DuiNotify &Notify)
{

	FileDialog dlg;
	auto res = dlg.setOwner(this->m_hWnd)
		.setSaveDialog(false)
		.setFolderDialog(false)
		.setMultiSelect(false)
		.clearFilters()
		.addFilter(OBF(L"WAV音频文件"), OBF(L"*.wav"))
		.setDefaultExtension(OBF(L"wav"))
		.selectFile();
	if (res.empty()) {
		return ;
	}
	m_pEditCtrl_CustomAudioCtrl->SetText(res.c_str());
	g_ConfigManager.set(OBF("SETTING"), OBF("AudioPath"), EncodingConverter :: UnicodeToGBK( res) );
	g_ConfigManager.save();
	return;
}

void CSettingDlg::OnDuiClick_IDC_ButtonCtrl_SelectGamePath(const DuiNotify &Notify)
{
	FileDialog dlg;
	auto res = dlg.setOwner(this->m_hWnd)
		.setSaveDialog(false)
		.setFolderDialog(false)
		.setMultiSelect(false)
		.clearFilters()
		.addFilter(OBF(L"4399客户端(MicroClient.exe)"), OBF(L"*.exe"))
		.setDefaultExtension(OBF(L"exe"))
		.selectFile();
	if (res.empty()) {
		return ;
	}
	if (res.ends_with(OBF(L"microclient.exe"))) {
		MessageBox(Notify.pNotifyCtrl->GetWndHandle(), OBF(L"选择错误！请选择MicroClient64目录下的MicroClient.exe"), OBF(L"选择错误"), MB_ICONERROR);
		return;
	}

	m_pEditCtrl_GamePathCtrl->SetText(res.c_str());
	g_ConfigManager.set(OBF("SETTING"), OBF("GamePath"), EncodingConverter::UnicodeToGBK(res));
	g_ConfigManager.save();
	return;
}

void CSettingDlg::OnDuiClick_IDC_ButtonCtrl_SelectGamePathTW(const DuiNotify &Notify)
{
	FileDialog dlg;
	auto res = dlg.setOwner(this->m_hWnd)
		.setSaveDialog(false)
		.setFolderDialog(false)
		.setMultiSelect(false)
		.clearFilters()
		.addFilter(OBF(L"台服启动器(LiuseLoader.exe)"), L"*.exe")
		.setDefaultExtension(OBF(L"exe"))
		.selectFile();
	if (res.empty()) {
		return ;
	}
	if (res.ends_with(OBF(L"liuseloader.exe"))) {
		MessageBox(Notify.pNotifyCtrl->GetWndHandle(),OBF(L"选择错误！请选择MicroClient64目录下的LiuseLoader.exe"),OBF(L"选择错误"),MB_ICONERROR);
		return;
	}
	m_pEditCtrl_GamePathTWCtrl->SetText(res.c_str());
	g_ConfigManager.set(OBF("SETTING"), OBF("GamePathTW"), EncodingConverter::UnicodeToGBK(res));
	g_ConfigManager.save();
	return;
}
