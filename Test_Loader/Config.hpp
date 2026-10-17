#include "framework.h"
// 写几个读取写入注册表的类
class CMyRegKey
{
public:
	CMyRegKey(HKEY hKey, LPCTSTR lpSubKey)
	{
		m_hKey = nullptr;
		if (RegOpenKeyEx(hKey, lpSubKey, 0, KEY_ALL_ACCESS, &m_hKey) == ERROR_SUCCESS)
		{
			m_bOpen = true;
		}
	}

	~CMyRegKey()
	{
		if (m_hKey)
		{
			RegCloseKey(m_hKey);
			m_hKey = nullptr;
		}
	}

	bool WriteValue(LPCTSTR lpValueName, LPCTSTR lpData, DWORD dwType)
	{
		if (m_hKey)
		{
			return (RegSetValueEx(m_hKey, lpValueName, 0, dwType, (BYTE*)lpData, (DWORD)(wcslen(lpData) + 1) * 2) == ERROR_SUCCESS);
		}
		return false;
	}

	bool ReadValue(LPCTSTR lpValueName, LPTSTR lpData, DWORD& dwType, DWORD dwSize)
	{
		if (m_hKey)
		{
			DWORD dwDataSize = dwSize;
			if (RegQueryValueEx(m_hKey, lpValueName, 0, &dwType, (BYTE*)lpData, &dwDataSize) == ERROR_SUCCESS)
			{
				return true;
			}
		}
		return false;
	}
	// 封装一下、读写文本（std::wstring） 读写数值
	bool WriteValue(LPCTSTR lpValueName, const std::wstring& strData)
	{
		return WriteValue(lpValueName, strData.c_str(), REG_SZ);
	}

	bool ReadValue(LPCTSTR lpValueName, std::wstring& strData)
	{
		DWORD dwType = REG_SZ;
		DWORD dwSize = 1024;
		TCHAR szData[1024] = { 0 };
		if (ReadValue(lpValueName, szData, dwType, dwSize))
		{
			strData = szData;
			return true;
		}
		return false;
	}

	bool WriteValue(LPCTSTR lpValueName, int nData)
	{
		return WriteValue(lpValueName, (LPCTSTR)std::to_wstring(nData).c_str(), REG_DWORD);
	}

	bool ReadValue(LPCTSTR lpValueName, int& nData)
	{
		DWORD dwType = REG_DWORD;
		DWORD dwSize = sizeof(DWORD);
		DWORD dwData = 0;
		if (ReadValue(lpValueName, (LPTSTR)&dwData, dwType, dwSize))
		{
			nData = dwData;
			return true;
		}
		return false;
	}


private:
	HKEY m_hKey;
	bool m_bOpen;
};
