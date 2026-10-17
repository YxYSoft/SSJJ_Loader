// test_hook_login.cpp —— TestLoaderHook 双端登录链路离线测试（不依赖真游戏）
// 用法: test_hook_login.exe <4399|tw>
//
// 流程（模拟加载器 + 游戏）：
//   1. 按 PID 创建共享内存写入登录数据（4399=token；台服=账号+密码）
//   2. LoadLibrary TestLoaderHook.dll（x64，同目录）
//   3. LoadLibrary libcef.dll（同目录的假 libcef，见 test_stub_libcef.cpp）
//   4. 等 hook 安装后构造 _cef_client_t 调 cef_browser_host_create_browser
//   5. 断言假 libcef 记录的 cookie / JS 内容
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "../Test_Loader/LoginSharedData.h"

static int g_pass = 0, g_fail = 0;
static void CHECK(bool cond, const char* name) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (cond) g_pass++; else g_fail++;
}

// ---- TinyBase64 编码（与加载器 CCookieMgr::DumpToken 同款：自定义表 + '$' 填充）----
static std::string TinyEncode(const std::string& data)
{
    static const char* chars =
        "IVXLdjEB9k+sHtQx5YbDiafuMncN7RrUeJCOgp/KhT2FmZPw1o6WqvAlzy4S803G";
    std::string out;
    size_t i = 0, len = data.size();
    while (i < len) {
        uint32_t a = i < len ? (uint8_t)data[i++] : 0;
        uint32_t b = i < len ? (uint8_t)data[i++] : 0;
        uint32_t c = i < len ? (uint8_t)data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(chars[(triple >> 18) & 0x3F]);
        out.push_back(chars[(triple >> 12) & 0x3F]);
        out.push_back(chars[(triple >> 6) & 0x3F]);
        out.push_back(chars[triple & 0x3F]);
    }
    size_t pad_len = len % 3;
    if (pad_len) {
        for (size_t j = pad_len; j < 3; j++)
            out[out.size() - (3 - j)] = '$';
    }
    return out;
}

static std::wstring DirOfSelf()
{
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) s.resize(pos);
    return s;
}

// UTF-16 文件内容转 UTF-8（假 libcef 按 UTF-16 写入）
static std::string WideFileToUtf8(const wchar_t* name)
{
    HANDLE h = CreateFileW((DirOfSelf() + L"\\" + name).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    std::vector<wchar_t> buf(8192, 0);
    DWORD n = 0;
    ReadFile(h, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)) - 2, &n, nullptr);
    CloseHandle(h);
    size_t wlen = n / sizeof(wchar_t);
    int m = WideCharToMultiByte(CP_UTF8, 0, buf.data(), (int)wlen, nullptr, 0, nullptr, nullptr);
    if (m <= 0) return std::string();
    std::string out((size_t)m, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf.data(), (int)wlen, &out[0], m, nullptr, nullptr);
    return out;
}

static bool Contains(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

// 测试 client 的 get_load_handler：返回空。
// 台服场景下 hook 会把它改写成代理 handler；4399 场景不关心。
static _cef_load_handler_t* CEF_CALLBACK test_get_load_handler(_cef_client_t* self)
{
    return nullptr;
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring mode = (argc > 1) ? argv[1] : L"4399";
    bool isTw = (mode == L"tw");
    bool isRaw = (mode == L"4399raw");   // 验证原始 cookie 串（非 base64）路径
    bool isB = (mode == L"4399b");       // 验证 JSON 长度%3==2 时尾组解码（回归旧 bug）
    printf("=== hook login test: mode=%s ===\n", isTw ? "tw" : (isRaw ? "4399raw" : (isB ? "4399b" : "4399")));

    // 清理上次输出
    DeleteFileW((DirOfSelf() + L"\\stub_cookies.txt").c_str());
    DeleteFileW((DirOfSelf() + L"\\stub_js.txt").c_str());
    DeleteFileW((DirOfSelf() + L"\\stub_trace.txt").c_str());

    // ---- 1) 共享内存登录数据 ----
    TLLoginSharedData d = {};
    d.magic = TL_LOGIN_MAGIC;
    d.version = TL_LOGIN_VERSION;
    if (isTw) {
        d.type = TL_ACCOUNT_TAIWAN;
        wcscpy_s(d.param0, L"13800138000");
        wcscpy_s(d.param1, L"pass1234");
    }
    else {
        d.type = TL_ACCOUNT_4399;
        if (isRaw) {
            // 原始 cookie 串（从老 ini 导入的典型格式）
            wcscpy_s(d.param0, L"USESSIONID=abc123; Pauth=deadbeef; ck_accname=tester");
        }
        else {
            // 4399b：特意用长度 %3==2 的 JSON，触发尾组 3 字符解码路径（旧 bug 会丢末字节）
            const char* json = isB
                ? "{\"USESSIONID\":\"abc123\",\"Pauth\":\"deadbeef\",\"ck_accname\":\"tester\",\"extra\":\"z\"}"
                : "{\"USESSIONID\":\"abc123\",\"Pauth\":\"deadbeef\",\"ck_accname\":\"tester\"}";
            std::string token = TinyEncode(json);
            std::wstring wtoken(token.begin(), token.end());
            wcscpy_s(d.param0, wtoken.c_str());
            printf("  token(%zu bytes json): %s\n", strlen(json), token.c_str());
        }
    }

    wchar_t szName[128] = {};
    TLLoginMappingName(GetCurrentProcessId(), szName, 128);
    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, TL_LOGIN_MAPPING_SIZE, szName);
    CHECK(hMap != nullptr, "create shared mapping");
    if (!hMap) return 1;
    void* pView = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, TL_LOGIN_STRUCT_SIZE);
    CHECK(pView != nullptr, "map view");
    if (!pView) { CloseHandle(hMap); return 1; }
    memcpy(pView, &d, TL_LOGIN_STRUCT_SIZE);
    FlushViewOfFile(pView, TL_LOGIN_STRUCT_SIZE);
    UnmapViewOfFile(pView);

    // ---- 2) 加载 hook DLL 与假 libcef ----
    HMODULE hHook = LoadLibraryW(L"TestLoaderHook.dll");
    CHECK(hHook != nullptr, "load TestLoaderHook.dll");
    HMODULE hCef = LoadLibraryW(L"libcef.dll");
    CHECK(hCef != nullptr, "load stub libcef.dll");
    if (!hHook || !hCef) return 1;

    // ---- 3) 等 watcher 读数据 + hook 安装（500ms 轮询 + detours）----
    Sleep(1500);

    // ---- 4) 构造 client 并触发 create_browser ----
    static _cef_client_t client = {};
    client.base.size = sizeof(_cef_client_t);
    client.get_load_handler = test_get_load_handler;

    // 4399 自动进服需要落地页 URL 触发 on_load_end；台服走默认 wdLogin
    if (!isTw)
        SetEnvironmentVariableW(L"STUB_URL", L"https://my.4399.com/yxssjj/wd");

    typedef int (*CreateBrowserFn)(const cef_window_info_t*, struct _cef_client_t*,
                                   const cef_string_t*, const struct _cef_browser_settings_t*,
                                   struct _cef_request_context_t*);
    auto pCreate = (CreateBrowserFn)GetProcAddress(hCef, "cef_browser_host_create_browser");
    CHECK(pCreate != nullptr, "resolve create_browser");
    if (pCreate) pCreate(nullptr, &client, nullptr, nullptr, nullptr);

    Sleep(300);

    // ---- 5) 断言 ----
    // 注意：假 libcef 的 AppendLine 按 UTF-16 写文件，读取需走 WideFileToUtf8
    std::string trace = WideFileToUtf8(L"stub_trace.txt");
    CHECK(Contains(trace, "create_browser"), "create_browser reached");

    if (isTw) {
        std::string js = WideFileToUtf8(L"stub_js.txt");
        CHECK(!js.empty(), "js injected (on_load_end fired)");
        CHECK(Contains(js, "13800138000"), "js contains account");
        CHECK(Contains(js, "pass1234"), "js contains password");
        CHECK(Contains(js, "/auth/login"), "js posts /auth/login");
        CHECK(Contains(js, "index/play?isClient=1&token="), "js redirects with token");
        CHECK(Contains(js, "type: 1"), "js phone-account type");
        CHECK(Contains(js, "YxYSoft") == false, "script_url not leaked into code");
    }
    else {
        std::string cookies = WideFileToUtf8(L"stub_cookies.txt");
        CHECK(!cookies.empty(), "cookies set via cef cookie manager");
        CHECK(Contains(cookies, "USESSIONID=abc123"), "cookie USESSIONID");
        CHECK(Contains(cookies, "Pauth=deadbeef"), "cookie Pauth");
        CHECK(Contains(cookies, "ck_accname=tester"), "cookie ck_accname");
        if (isB) CHECK(Contains(cookies, "extra=z"), "cookie extra (L%3==2 tail decode)");

        // 4399 落地页 on_load_end 应注入自动进服脚本（参考 DoInjectAutoJoin）
        std::string js = WideFileToUtf8(L"stub_js.txt");
        CHECK(!js.empty(), "4399 auto-join js injected on landing page");
        CHECK(Contains(js, "ajaxGetLastEnter"), "js calls ajaxGetLastEnter");
        CHECK(Contains(js, "play-sid"), "js redirects to play-sid");
        CHECK(Contains(js, "my.4399.com/yxssjj/"), "js targets 4399 yxssjj");
        CHECK(Contains(js, "YxYSoft") == false, "script_url not leaked into code");
    }

    printf("\n%s: %d passed, %d failed\n", isTw ? "TW" : "4399", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
