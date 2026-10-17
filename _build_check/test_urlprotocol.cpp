// UrlProtocol 逻辑自测：真实编译 + 真实运行（ASCII-only）
// 覆盖：解析边界 / 注册表写入 / 兜底文件往返 / 快捷方式创建与清理
#include "UrlProtocol.h"
#include <cstdio>
#include <string>
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

static int g_fail = 0;
static void CHECK(bool c, const char* w)
{
	printf(c ? "  ok   : %s\n" : "  FAIL : %s\n", w);
	if (!c) ++g_fail;
}

int main()
{
	printf("[1] ParseProtocolUrl\n");
	std::wstring payload;
	CHECK(ParseProtocolUrl(L"yxysoft://123456-789456", payload) && payload == L"123456-789456", "basic url");
	CHECK(ParseProtocolUrl(L"\"yxysoft://login-42\"", payload) && payload == L"login-42", "quoted url");
	CHECK(ParseProtocolUrl(L"yxysoft://login-2/", payload) && payload == L"login-2", "trailing slash stripped (cmd start / .url dblclick)");
	CHECK(ParseProtocolUrl(L"\"yxysoft://login-7/\"", payload) && payload == L"login-7", "quoted with trailing slash");
	CHECK(ParseProtocolUrl(L"yxysoft://login-9///", payload) && payload == L"login-9", "multiple trailing slashes stripped");
	CHECK(ParseProtocolUrl(L"yxysoft://123456-789456/", payload) && payload == L"123456-789456", "original placeholder url with slash");
	CHECK(ParseProtocolUrl(L"yxysoft:///", payload) && payload.empty(), "scheme with only slashes -> empty");
	CHECK(ParseProtocolUrl(L"yxYSoft://Login-7", payload) && payload == L"Login-7", "scheme case-insensitive, payload preserved");
	CHECK(ParseProtocolUrl(L"C:\\path with space\\Test_Loader.exe yxysoft://a-b", payload) && payload == L"a-b", "url after exe path with spaces");
	CHECK(ParseProtocolUrl(L"yxysoft://a%2Db", payload) && payload == L"a-b", "percent-decoded");
	CHECK(ParseProtocolUrl(L"yxysoft://", payload) && payload.empty(), "empty payload");
	CHECK(!ParseProtocolUrl(L"http://example.com", payload), "non-protocol url rejected");
	CHECK(!ParseProtocolUrl(L"", payload), "empty cmdline rejected");
	CHECK(!ParseProtocolUrl(L"yxysoftX://abc", payload), "similar-but-different scheme rejected");
	CHECK(ParseProtocolUrl(L"yxysoft://abc def", payload) && payload == L"abc", "stop at whitespace");

	printf("[2] EnsureProtocolRegistered\n");
	CHECK(EnsureProtocolRegistered(), "register");
	HKEY hk = NULL;
	bool bCmdOk = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\yxysoft\\shell\\open\\command", 0, KEY_READ, &hk) == ERROR_SUCCESS;
	CHECK(bCmdOk, "command key exists");
	if (bCmdOk) {
		wchar_t buf[1024] = { 0 };
		DWORD n = sizeof(buf);
		if (RegQueryValueExW(hk, NULL, NULL, NULL, (LPBYTE)buf, &n) == ERROR_SUCCESS) {
			printf("       command = %ls\n", buf);
			CHECK(wcsstr(buf, L"%1") != NULL, "command contains %1");
		}
		RegCloseKey(hk);
	}
	CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\yxysoft", 0, KEY_READ, &hk) == ERROR_SUCCESS, "root key + URL Protocol");
	if (hk) {
		DWORD n = 0;
		CHECK(RegQueryValueExW(hk, L"URL Protocol", NULL, NULL, NULL, &n) == ERROR_SUCCESS, "URL Protocol value exists");
		RegCloseKey(hk);
	}
	// 幂等：再注册一次
	CHECK(EnsureProtocolRegistered(), "re-register idempotent");

	printf("[3] pending payload file\n");
	CHECK(WritePendingPayloadFile(L"login-9"), "write pending");
	std::wstring got;
	CHECK(ReadPendingPayloadFile(got) && got == L"login-9", "round trip");
	CHECK(!ReadPendingPayloadFile(got), "file deleted after read");

	printf("[4] CreateAccountShortcut\n");
	// 用一个明显是测试的名字，验证后立即删除，不留垃圾
	const std::wstring kTestName = L"_urlprotocol_selftest_";
	CHECK(CreateAccountShortcut(123, kTestName), "create shortcut");
	PWSTR pszDesktop = NULL;
	std::wstring path;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pszDesktop))) {
		path = std::wstring(pszDesktop) + L"\\" + kTestName + L".url";
		CoTaskMemFree(pszDesktop);
	}
	HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	CHECK(hFile != INVALID_HANDLE_VALUE, "shortcut file exists on desktop");
	if (hFile != INVALID_HANDLE_VALUE) {
		char buf[1024] = { 0 };
		DWORD nRead = 0;
		ReadFile(hFile, buf, sizeof(buf) - 1, &nRead, NULL);
		CloseHandle(hFile);
		std::string content(buf, nRead);
		printf("       content = %s\n", content.c_str());
		CHECK(content.find("[InternetShortcut]") != std::string::npos, "has header");
		CHECK(content.find("URL=yxysoft://login-123") != std::string::npos, "has correct url");
		DeleteFileW(path.c_str());
	}
	// 非法字符净化
	CHECK(CreateAccountShortcut(5, L"a/b:c*d?e\"f<g>h|i"), "create with illegal chars");
	{
		std::wstring bad = L"_urlprotocol_illegal_";
		// 上面名字净化后应是 _urlprotocol_illegal_.url（原名无非法字符则直接删）
		PWSTR pszDesktop2 = NULL;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pszDesktop2))) {
			DeleteFileW((std::wstring(pszDesktop2) + L"\\a_b_c_d_e_f_g_h_i.url").c_str());
			DeleteFileW((std::wstring(pszDesktop2) + L"\\" + bad + L".url").c_str());
			CoTaskMemFree(pszDesktop2);
		}
	}
	// 空名回退
	CHECK(CreateAccountShortcut(77, L""), "create with empty name");
	{
		PWSTR pszDesktop3 = NULL;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pszDesktop3))) {
			DeleteFileW((std::wstring(pszDesktop3) + L"\\账号77.url").c_str());
			CoTaskMemFree(pszDesktop3);
		}
	}

	printf("\n%s (fail=%d)\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
	return g_fail ? 1 : 0;
}
