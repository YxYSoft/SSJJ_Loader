// Test_Loader.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "Test_Loader.h"
#include "CMainDlg.h"
#include "CSplashDlg.h"
#include "ThreadPool.h"
#include "NetWork.h"
#include <ConfigManager.hpp>
#include <mmsystem.h>

#include "SingleInstance.h"
#include "UrlProtocol.h"

#pragma comment(lib, "winmm.lib")



#include <obfusheader.h>





extern std::atomic_bool g_finsh ;






// 命名互斥量名字
const std::wstring MY_MUTEX_NAME = OBF(L"Global\\YxYSoft_UniqueMutex_8CE50B5D-7F1A-4D3E-8B9A-2C7F8D9E0A1B");
// 窗口属性名字
const std::wstring MY_PROP_NAME = OBF(L"YxYSoft_UniqueProp_8CE50B5D-7F1A-4D3E-8B9A-2C7F8D9E0A1B");




bool PlayWavFromMemoryEx(DWORD dwResId, LPCTSTR lpType, std::string strAudioPath = "", DWORD dwPlayFlags = SND_MEMORY | SND_SYNC |
	SND_NODEFAULT);
ConfigManager g_ConfigManager;
ThreadPool g_ThreadPool;

// 更完善的版本(需要完整解析WAV文件头)
bool PlayWavFromMemoryEx(DWORD dwResId,LPCTSTR lpType, std::string strAudioPath, DWORD dwPlayFlags/* = SND_MEMORY | SND_SYNC |	SND_NODEFAULT*/)
{
	// 
	if (dwResId == NULL || lpType == 0) {
		std::vector<unsigned char> smallImage_wav;
		//从文件读取wav 
		std::ifstream file(strAudioPath, std::ios::binary);
		if (file.is_open())
		{
			file.seekg(0, std::ios::end);
			int size = file.tellg();
			file.seekg(0, std::ios::beg);
			smallImage_wav.resize(size);
			file.read((char*)&smallImage_wav[0], size);
			file.close();
			sndPlaySound((LPWSTR)&smallImage_wav[0], dwPlayFlags);
		}
	}
	else {
		//播放资源文件的wav
		auto hRes = FindResource(NULL, MAKEINTRESOURCE(dwResId), lpType);
		if (hRes == NULL) {
			return false;
		}
		HGLOBAL hMem = LoadResource(NULL,hRes);
		if (hMem == NULL) {
			return false;
		}
		LPVOID pMem = LockResource(hMem);
		if (pMem == NULL) {
			return false;
		}
		DWORD dataSize = SizeofResource(NULL, hRes);
		if (dataSize == 0) {
			return false;
		}

		sndPlaySound((LPWSTR)pMem, dwPlayFlags);
	}
	return true;
}

BOOL InitOpenSSL();
std::unique_ptr<SingleInstanceManager> g_SingleInstanceManager=nullptr;
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
	// lpCmdLine 可能携带 URL 协议激活参数（yxysoft://...），不再忽略
	g_SingleInstanceManager = std::make_unique<SingleInstanceManager> (MY_MUTEX_NAME, MY_PROP_NAME);
	bool bHasAnotherInstance = false;
	if (g_SingleInstanceManager)
	{
		bHasAnotherInstance = g_SingleInstanceManager->HasAnotherInstance();
	}
	if (bHasAnotherInstance)
	{
		// 已有实例：如命令行带 URL 协议参数则一并传递，激活它，然后直接退出
		std::wstring strProtocolPayload;
		bool bProtocolUrl = ParseProtocolUrl(lpCmdLine, strProtocolPayload);
		g_SingleInstanceManager->ActivateFirstInstance(bProtocolUrl ? strProtocolPayload.c_str() : nullptr);
		return 0;
	}
	// 首个实例：确保 URL 协议已注册（HKCU，幂等）
	EnsureProtocolRegistered();
	// 如本实例就是被 URL 协议唤起的，先暂存 payload，待主窗口就绪后分发
	std::wstring strProtocolPayload;
	if (ParseProtocolUrl(lpCmdLine, strProtocolPayload))
	{
		g_strPendingProtocolPayload = strProtocolPayload;
	}
	InitOpenSSL();
	do 
	{
		char szPath[MAX_PATH] = { 0 };
		GetModuleFileNameA(NULL, szPath, MAX_PATH);
		std::string strPath = szPath;
		std::string strDir = strPath.substr(0, strPath.find_last_of("\\"));
		// 拼接配置文件路径
		strPath = strDir + "\\config.ini";
		g_ConfigManager.load(strPath);

	} while (FALSE);
	// 

	g_ThreadPool.submit([]() {
		do
		{
			// 音频设置(1内置，0自选)
			int nAudioSel = g_ConfigManager.get(OBF("SETTING"), OBF("AudioSel"), 1);
			if (nAudioSel == 1) { // 内置音频
				int nInnerAudioSel = g_ConfigManager.get(OBF("SETTING"), OBF("InnerAudioSel"), 1);
				if (nInnerAudioSel == 0) { // 关闭内置音频

				}
				else {
					if (nInnerAudioSel == 1) { // 内置音频1
						//播放资源文件音频
						PlayWavFromMemoryEx (IDR_WAVE_MODE1, OBF(L"WAVE"), "", SND_MEMORY | SND_SYNC | SND_NODEFAULT);
					}
					else if (nInnerAudioSel == 2) { // 内置音频2

					}
					else if (nInnerAudioSel == 3) { // 内置音频3
					}
					//播放资源文件音频

				}
			}
			else {
				// 自选音频
				// 音频文件路径
				std::string strAudioPath = g_ConfigManager.get(OBF("SETTING"), OBF("AudioPath"), std::string(""));
				if (strAudioPath.empty()) { // 没有设置音频文件路径
				}
				else { // 设置了音频文件路径
					// 加载音频文件 文件是否存在
					// 播放音频
					if (PathFileExistsA(strAudioPath.c_str())) {
						// 播放音频
						//PlaySoundA(strAudioPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
						PlayWavFromMemoryEx (NULL, NULL, strAudioPath, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
					}
				}
			}

		} while (FALSE);
		});

	//YxYSoft_LaoMianAo
	CDUIGlobal::GetInstance()->Init(hInstance);
	//CDUIGlobal::GetInstance()->SetDpi(CMMDpi::GetDpiOfMainMonitor());
	//CDUIGlobal::GetInstance()->LoadProjectFromFile( 		LR"(H:\360Downloads\Test_Loader\Test_Loader.DuiProj\test_Loader.DuiProj)" 	);       
	CDUIGlobal::GetInstance()->LoadProjectFromResZip(hInstance, MAKEINTRESOURCE(IDR_WAVE_SKIN), OBF(L"YxYSoft_LaoMianAo"), OBF(L"test_Loader"), OBF(L"WAVE"));
	//CDUIGlobal::GetInstance()->LoadProjectFromZip(LR"(H:\360Downloads\DUIThink-main\Demo\DUIThinkDemo_C++\DUIThinkDemo.DuiProj\DUIThinkDemo.DuiProj.zip)", L"", _T("DUIThinkDemo"));
	CMMDpi::SetProcessDPIAwareness( enMMPROCESS_DPI_AWARENESS::MMPROCESS_PER_MONITOR_DPI_AWARE);

	//cmd line
// 	CMMCommandLine CmdLine(lpCmdLine);
// 	CMMString strValue;

	do
	{

		CSplashDlg SplashDlg;
		SplashDlg.Create(NULL, OBF(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_CHILD, DUI_WNDSTYLE_EX_FRAME);
		


		//normal demo
	
		SplashDlg.DoModal();
		//Sleep(000);
		CMainDlg DlgDemo;
		DlgDemo.Create(NULL, OBF(DEFAULT_WINDOW_TITLE), DUI_WNDSTYLE_FRAME & (~WS_MAXIMIZEBOX), DUI_WNDSTYLE_EX_FRAME);

		do
		{
			Sleep(50);
		} while (!g_finsh.load());
		DlgDemo.DoModal();

	} while (false);

	CDUIGlobal::GetInstance()->UnInit();
	if (g_SingleInstanceManager) {
		g_SingleInstanceManager.reset();
	}
    return (int) 0;
}

