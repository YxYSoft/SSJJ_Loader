#include "UrlProtocol.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#include <obfusheader.h>

// 复用 DbWork.cpp 中已有的实现（外部链接，避免重复定义）
extern std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId);

// 协议 scheme（小写，解析时大小写不敏感）
#define  s_szScheme  L"yxysoft://"
// 兜底文件名（%TEMP% 下）
#define  s_szPendingFile L"yxysoft_pending_payload.txt"

// ---------------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------------

static bool WriteRegStr(HKEY hKey, const wchar_t* pszName, const wchar_t* pszValue)
{
	DWORD cbData = (DWORD)((wcslen(pszValue) + 1) * sizeof(wchar_t));
	return RegSetValueExW(hKey, pszName, 0, REG_SZ, (const BYTE*)pszValue, cbData) == ERROR_SUCCESS;
}

static std::wstring GetSelfExePath()
{
	std::wstring strPath;
	strPath.resize(MAX_PATH);
	DWORD nLen = GetModuleFileNameW(NULL, &strPath[0], MAX_PATH);
	strPath.resize(nLen);
	return strPath;
}

static int HexValue(wchar_t ch)
{
	if (ch >= L'0' && ch <= L'9') return ch - L'0';
	if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
	if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
	return -1;
}

// 简易 percent 解码（payload 约定为 ASCII，浏览器可能对特殊字符编码）
static std::wstring PercentDecode(const std::wstring& strSrc)
{
	std::wstring strOut;
	strOut.reserve(strSrc.size());
	for (size_t i = 0; i < strSrc.size(); ++i)
	{
		if (strSrc[i] == L'%' && i + 2 < strSrc.size())
		{
			int nHi = HexValue(strSrc[i + 1]);
			int nLo = HexValue(strSrc[i + 2]);
			if (nHi >= 0 && nLo >= 0)
			{
				strOut += (wchar_t)((nHi << 4) | nLo);
				i += 2;
				continue;
			}
		}
		strOut += strSrc[i];
	}
	return strOut;
}

// GBK/ANSI 窄串写文件用（.url 为 INI 格式，shell 按本地代码页读取）
static std::string WideToAnsi(const std::wstring& strWide)
{
	if (strWide.empty()) return std::string();
	int nLen = WideCharToMultiByte(CP_ACP, 0, strWide.c_str(), (int)strWide.size(), NULL, 0, NULL, NULL);
	std::string strAnsi(nLen, '\0');
	WideCharToMultiByte(CP_ACP, 0, strWide.c_str(), (int)strWide.size(), &strAnsi[0], nLen, NULL, NULL);
	return strAnsi;
}

// UTF-8 窄串（兜底文件读写）
static std::string WideToUtf8(const std::wstring& strWide)
{
	if (strWide.empty()) return std::string();
	int nLen = WideCharToMultiByte(CP_UTF8, 0, strWide.c_str(), (int)strWide.size(), NULL, 0, NULL, NULL);
	std::string strUtf8(nLen, '\0');
	WideCharToMultiByte(CP_UTF8, 0, strWide.c_str(), (int)strWide.size(), &strUtf8[0], nLen, NULL, NULL);
	return strUtf8;
}

static std::wstring Utf8ToWide(const std::string& strUtf8)
{
	if (strUtf8.empty()) return std::wstring();
	int nLen = MultiByteToWideChar(CP_UTF8, 0, strUtf8.c_str(), (int)strUtf8.size(), NULL, 0);
	std::wstring strWide(nLen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, strUtf8.c_str(), (int)strUtf8.size(), &strWide[0], nLen);
	return strWide;
}

// ---------------------------------------------------------------------------
// 协议注册（HKCU，无需管理员）
// ---------------------------------------------------------------------------

bool EnsureProtocolRegistered()
{
	std::wstring strExe = GetSelfExePath();
	if (strExe.empty())
	{
		return false;
	}

	HKEY hKey = NULL;

	// HKCU\Software\Classes\yxysoft
	if (RegCreateKeyExW(HKEY_CURRENT_USER, OBF(L"Software\\Classes\\yxysoft"), 0, NULL, 0,
		KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
	{
		return false;
	}
	WriteRegStr(hKey, NULL, OBF(L"URL:yxysoft Protocol"));
	WriteRegStr(hKey, OBF(L"URL Protocol"), L""); // 空串即存在性标记
	RegCloseKey(hKey);

	// DefaultIcon
	if (RegCreateKeyExW(HKEY_CURRENT_USER, OBF(L"Software\\Classes\\yxysoft\\DefaultIcon"), 0, NULL, 0,
		KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
	{
		std::wstring strIcon = strExe + OBF(L",0");
		WriteRegStr(hKey, NULL, strIcon.c_str());
		RegCloseKey(hKey);
	}

	// shell\open\command
	if (RegCreateKeyExW(HKEY_CURRENT_USER, OBF(L"Software\\Classes\\yxysoft\\shell\\open\\command"), 0, NULL, 0,
		KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
	{
		std::wstring strCmd =OBF( L"\"") + strExe + OBF(L"\" \"%1\"");
		WriteRegStr(hKey, NULL, strCmd.c_str());
		RegCloseKey(hKey);
	}

	return true;
}

// ---------------------------------------------------------------------------
// 命令行解析
// ---------------------------------------------------------------------------

bool ParseProtocolUrl(const std::wstring& cmdLine, std::wstring& outPayload)
{
	outPayload.clear();
	if (cmdLine.empty())
	{
		return false;
	}
	auto szScheme = OBF(s_szScheme);
	// 大小写不敏感查找 scheme
	size_t nPos = std::wstring::npos;
	const size_t nSchemeLen = wcslen((LPCWSTR)szScheme);
	for (size_t i = 0; i + nSchemeLen <= cmdLine.size(); ++i)
	{
		if (_wcsnicmp(cmdLine.c_str() + i, (LPCWSTR)szScheme, nSchemeLen) == 0)
		{
			nPos = i;
			break;
		}
	}
	if (nPos == std::wstring::npos)
	{
		return false;
	}

	// 取 scheme 之后到首个空白/引号为止
	size_t nStart = nPos + nSchemeLen;
	size_t nEnd = cmdLine.find_first_of(L" \t\"", nStart);
	if (nEnd == std::wstring::npos)
	{
		nEnd = cmdLine.size();
	}

	std::wstring strRaw = cmdLine.substr(nStart, nEnd - nStart);
	outPayload = PercentDecode(strRaw);
	// Windows 经协议注册表唤起时（cmd 的 start / 双击 .url 快捷方式）会在 URL
	// 末尾补 '/'，这里统一剥掉尾部的 '/'（如 yxysoft://login-2/ -> login-2）
	while (!outPayload.empty() && outPayload.back() == L'/')
	{
		outPayload.pop_back();
	}
	return true;
}

// ---------------------------------------------------------------------------
// 桌面快捷方式（.url）
// ---------------------------------------------------------------------------

bool CreateAccountShortcut(unsigned nAccountId, const std::wstring& strAccountName)
{
	std::wstring strDesktop = GetKnownFolderPath(FOLDERID_Desktop);
	if (strDesktop.empty())
	{
		return false;
	}
	if (strDesktop.back() != L'\\')
	{
		strDesktop += L'\\';
	}

	// 文件名：显示名 -> 净化非法字符 -> 空则 账号<id>
	std::wstring strName = strAccountName;
	for (auto& ch : strName)
	{
		if (wcschr(L"\\/:*?\"<>|", ch))
		{
			ch = L'_';
		}
	}
	// 去掉首尾空白
	while (!strName.empty() && iswspace(strName.back())) strName.pop_back();
	size_t nFirst = strName.find_first_not_of(L" \t");
	if (nFirst == std::wstring::npos)
	{
		strName = OBF(L"账号") + std::to_wstring(nAccountId);
	}
	else
	{
		strName = strName.substr(nFirst);
	}
	std::wstring strFilePath = strDesktop + strName + OBF(L".url");

	std::wstring strExe = GetSelfExePath();
	std::string strContent;
	strContent += OBF("[InternetShortcut]\r\n");
	strContent += OBF("URL=") + WideToAnsi(OBF(L"yxysoft://login-") + std::to_wstring(nAccountId)) + OBF("\r\n");
	if (!strExe.empty())
	{
		strContent += OBF("IconFile=") + WideToAnsi(strExe) + OBF("\r\n");
		strContent += OBF("IconIndex=0\r\n");
	}

	HANDLE hFile = CreateFileW(strFilePath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	DWORD dwWritten = 0;
	BOOL bOk = WriteFile(hFile, strContent.data(), (DWORD)strContent.size(), &dwWritten, NULL);
	FlushFileBuffers(hFile);
	CloseHandle(hFile);
	return bOk && dwWritten == strContent.size();
}

// ---------------------------------------------------------------------------
// 兜底 payload 文件（%TEMP%\yxysoft_pending_payload.txt，UTF-8 + BOM）
// ---------------------------------------------------------------------------

bool WritePendingPayloadFile(const std::wstring& strPayload)
{
	if (strPayload.empty())
	{
		return false;
	}
	wchar_t szTmp[MAX_PATH] = { 0 };
	if (GetTempPathW(MAX_PATH, szTmp) == 0)
	{
		return false;
	}
	std::wstring strFilePath = std::wstring(szTmp) + OBF(s_szPendingFile);

	std::string strUtf8 = WideToUtf8(strPayload);
	std::string strBuf;
	strBuf.push_back((char)0xEF);
	strBuf.push_back((char)0xBB);
	strBuf.push_back((char)0xBF);
	strBuf += strUtf8;

	HANDLE hFile = CreateFileW(strFilePath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	DWORD dwWritten = 0;
	BOOL bOk = WriteFile(hFile, strBuf.data(), (DWORD)strBuf.size(), &dwWritten, NULL);
	CloseHandle(hFile);
	return bOk && dwWritten == strBuf.size();
}

bool ReadPendingPayloadFile(std::wstring& outPayload)
{
	outPayload.clear();
	wchar_t szTmp[MAX_PATH] = { 0 };
	if (GetTempPathW(MAX_PATH, szTmp) == 0)
	{
		return false;
	}
	std::wstring strFilePath = std::wstring(szTmp) + OBF(s_szPendingFile);
	DWORD dwAttr = GetFileAttributesW(strFilePath.c_str());
	if (dwAttr == INVALID_FILE_ATTRIBUTES || (dwAttr & FILE_ATTRIBUTE_DIRECTORY))
	{
		return false;
	}

	HANDLE hFile = CreateFileW(strFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	std::string strRaw;
	char szBuf[4096] = { 0 };
	DWORD dwRead = 0;
	while (ReadFile(hFile, szBuf, sizeof(szBuf), &dwRead, NULL) && dwRead > 0)
	{
		strRaw.append(szBuf, dwRead);
	}
	CloseHandle(hFile);
	DeleteFileW(strFilePath.c_str());

	// 去 UTF-8 BOM
	size_t nOff = 0;
	if (strRaw.size() >= 3 && (unsigned char)strRaw[0] == 0xEF
		&& (unsigned char)strRaw[1] == 0xBB && (unsigned char)strRaw[2] == 0xBF)
	{
		nOff = 3;
	}
	outPayload = Utf8ToWide(strRaw.substr(nOff));
	return !outPayload.empty();
}
