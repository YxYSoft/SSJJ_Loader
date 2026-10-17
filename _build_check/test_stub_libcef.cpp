// test_stub_libcef.cpp —— 假 libcef.dll（x64），仅供 TestLoaderHook 离线测试
// 不参与正式构建/发布，仅由 _build_check/run_tests.sh 编译并拷贝为 libcef.dll。
//
// 导出（与真 libcef 同名同签名）：
//   cef_browser_host_create_browser       取 client->get_load_handler 并同步回调 on_load_end
//   cef_cookie_manager_get_global_manager 返回假 manager，set_cookie 记录 name=value 到文件
//   cef_string_userfree_utf16_free        空实现（hook 通过 GetProcAddress 按此名解析）
#include <windows.h>
#include <string>
#include <cstdio>
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_load_handler_capi.h"
#include "include/capi/cef_cookie_capi.h"

namespace {

std::wstring DirOfSelf()
{
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) s.resize(pos);
    return s;
}

void AppendLine(const wchar_t* file, const std::wstring& line)
{
    std::wstring path = DirOfSelf() + L"\\" + file;
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::wstring l = line + L"\r\n";
    DWORD n = 0;
    WriteFile(h, l.data(), (DWORD)(l.size() * sizeof(wchar_t)), &n, nullptr);
    CloseHandle(h);
}

// ---- 假 frame：is_main=1；get_url 返回环境变量 STUB_URL（默认台服登录页）----
cef_string_userfree_t CEF_CALLBACK fake_get_url(_cef_frame_t* self)
{
    static cef_string_utf16_t s = {};
    static std::wstring url;
    if (url.empty()) {
        wchar_t buf[512] = {};
        DWORD n = GetEnvironmentVariableW(L"STUB_URL", buf, 512);
        url = (n > 0 && n < 512) ? buf : L"https://tw.battleteams1.com/index/wdLogin";
    }
    s.str = const_cast<wchar_t*>(url.c_str());
    s.length = url.size();
    s.dtor = nullptr;
    return &s;
}

int CEF_CALLBACK fake_is_main(_cef_frame_t* self) { return 1; }

void CEF_CALLBACK fake_execute_java_script(_cef_frame_t* self,
                                           const cef_string_t* code,
                                           const cef_string_t* script_url,
                                           int start_line)
{
    AppendLine(L"stub_js.txt", code && code->str ? code->str : L"(null)");
}

_cef_frame_t MakeFakeFrame()
{
    _cef_frame_t f = {};
    f.base.size = sizeof(_cef_frame_t);
    f.is_main = fake_is_main;
    f.get_url = fake_get_url;
    f.execute_java_script = fake_execute_java_script;
    return f;
}

// ---- 假 cookie manager：set_cookie 记录 name=value ----
int CEF_CALLBACK fake_set_cookie(_cef_cookie_manager_t* self,
                                 const cef_string_t* url,
                                 const struct _cef_cookie_t* cookie,
                                 struct _cef_set_cookie_callback_t* callback)
{
    if (cookie && cookie->name.str && cookie->value.str)
        AppendLine(L"stub_cookies.txt",
                   std::wstring(cookie->name.str) + L"=" + cookie->value.str);
    return 1;
}

} // namespace

extern "C" int cef_browser_host_create_browser(
    const cef_window_info_t* windowInfo, struct _cef_client_t* client,
    const cef_string_t* url, const struct _cef_browser_settings_t* settings,
    struct _cef_request_context_t* request_context)
{
    AppendLine(L"stub_trace.txt", L"create_browser");
    if (client && client->get_load_handler) {
        _cef_load_handler_t* lh = client->get_load_handler(client);
        if (lh && lh->on_load_end) {
            static _cef_frame_t frame = MakeFakeFrame();
            static _cef_browser_t browser = {};
            lh->on_load_end(lh, &browser, &frame, 200);
        }
    }
    return 1;
}

extern "C" cef_cookie_manager_t*
cef_cookie_manager_get_global_manager(struct _cef_completion_callback_t* callback)
{
    static _cef_cookie_manager_t m = {};
    if (!m.base.size) {
        m.base.size = sizeof(_cef_cookie_manager_t);
        m.set_cookie = fake_set_cookie;
    }
    return &m;
}

extern "C" void cef_string_userfree_utf16_free(cef_string_userfree_t str)
{
    (void)str;  // 假实现：不释放
}
