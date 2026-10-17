// TestLoaderHook.cpp —— 游戏进程内登录 DLL（x64，4399 + 台服双端合一）
//
// 工作方式：
//   1. 加载器以 CREATE_SUSPENDED + DetourCreateProcessWithDllExW 注入本 DLL，
//      并按 PID 创建 Local\TestLoader_Login_<PID> 写入登录数据后恢复主线程。
//   2. DllMain 起后台线程轮询同名共享内存，读到合法数据后按 type 分发：
//        type=0   4399：挂 libcef!cef_browser_host_create_browser，
//                  首个浏览器创建前把 cookie（TinyBase64$ JSON）逐条
//                  SetCookie 进 https://my.4399.com/ 实现强制登录；
//                  落地页（如 https://my.4399.com/yxssjj/wd）加载完成后
//                  注入自动进服脚本：等 jQuery 就绪 -> ajaxGetLastEnter
//                  -> 跳 play-sid 进服（参考 version_x64_passNp / DoInjectAutoJoin）。
//        type=100 台服：挂同一函数并改写 client->get_load_handler，
//                  登录页 tw.battleteams1.com/index/wdLogin 加载完成后
//                  execute_java_script 注入 AJAX 登录脚本（账号+密码）。
//        type=1000 抓取（仅 4399）：不自动登录，改为 hook 全局 cookie manager 的
//                  set_cookie 记录 4399 域 cookie，后台线程把 cookie map 序列化 +
//                  TinyBase64('$') 编码写回共享内存 param0；加载器弹框阻塞等用户手动
//                  登录，确定后读回 param0 入库（移除老棉裤 ini 文件依赖）。
//   3. 跨位宽注入时本 DLL 会被 detours helper（x64 rundll32）以 ",#1" 加载，
//      导出 #1 负责执行 DetourFinishHelperProcess 完成真正的导入表注入。
//
// 实现要点：
//   - 零 libcef 静态导入：本 DLL 在进程创建瞬间（挂起态）注入，此时 libcef
//     尚未加载，静态导入会导致 DLL 加载失败。全部 CEF 交互经运行时
//     GetModuleHandleW(L"libcef.dll") + GetProcAddress 解析；set_cookie /
//     get_load_handler / execute_java_script 均为结构体方法，随指针调用。
//   - hook 装在 watcher 线程（非 DllMain），避开 loader lock。
#include <windows.h>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <cstdio>
#include "detours.h"
#include "json.hpp"

// CEF C API 头文件（仅用于编译期结构体布局，运行时不链接 libcef.lib）
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_load_handler_capi.h"
#include "include/capi/cef_cookie_capi.h"
#include "include/capi/cef_request_context_capi.h"

#include "../Test_Loader/LoginSharedData.h"
#include <obfusheader.h>

//////////////////////////////////////////////////////////////////////////
// ===== 调试输出总开关 =====
// Debug 配置（_DEBUG）默认开启，Release 自动关闭；如需强制开/关，
// 在包含本文件前 #define TL_ENABLE_DEBUG_OUTPUT 1 / 0 即可。
#ifndef TL_ENABLE_DEBUG_OUTPUT
  #ifdef _DEBUG
    #define TL_ENABLE_DEBUG_OUTPUT 1
  #else
    #define TL_ENABLE_DEBUG_OUTPUT 0
  #endif
#endif

#if !TL_ENABLE_DEBUG_OUTPUT
  // 关闭时：直接 OutputDebugStringA 调用替换为空操作
  // （Dbg/WriteLogFile 的日志输出在 Dbg() 内按同一开关统一处理）
  #ifdef OutputDebugStringA
    #undef OutputDebugStringA
  #endif
  #define OutputDebugStringA(lpOutputString)  ((void)0)
#endif

namespace {

constexpr int WATCH_TIMEOUT_MS = 30000;   // 共享内存最多等 30 秒
constexpr int WATCH_INTERVAL_MS = 100;
constexpr int LIBCEF_WAIT_MS   = 30000;   // libcef 最多等 30 秒
constexpr int LIBCEF_POLL_MS   = 500;

// 4399 登录 cookie 作用域（与参考工程 version_x64_passNp 一致）
#define  k4399CookieUrl  OBF(L"https://my.4399.com/")
// 台服登录页（与参考工程 version_Tw 一致，http/https 都匹配）
#define  kTwLoginUrlHttp   OBF(L"http://tw.battleteams1.com/index/wdLogin")
#define  kTwLoginUrlHttps  OBF(L"https://tw.battleteams1.com/index/wdLogin")
// =====================================================================
// 4399：CEF cookie 强制登录
// =====================================================================
using FnGetGlobalCookieManager = cef_cookie_manager_t * (*)(_cef_completion_callback_t*);
using FnUserfreeFree = void (*)(cef_string_userfree_t);

FnGetGlobalCookieManager g_pGetGlobalManager = nullptr;  // InstallHooks 时解析
FnUserfreeFree          g_pUserfreeFree = nullptr;

// 统一日志：同时走 OutputDebugString（供 DbgView）和文件（%TEMP%\TestLoaderHook.log）。
// 文件日志不依赖任何外部工具，真机排查首选；DbgView 看不到时看这个文件即可。
#if TL_ENABLE_DEBUG_OUTPUT
std::mutex g_logMx;
void WriteLogFile(const WCHAR* s)
{
    wchar_t tmp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    std::wstring p = std::wstring(tmp) + L"TestLoaderHook.log";
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, (int)wcslen(s), nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        std::string u8((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s, (int)wcslen(s), &u8[0], n, nullptr, nullptr);
        DWORD w = 0;
        WriteFile(h, u8.c_str(), (DWORD)u8.size(), &w, nullptr);
    }
    const char nl = '\n';
    DWORD w = 0;
    WriteFile(h, &nl, 1, &w, nullptr);
    CloseHandle(h);
}
#endif



#if TL_ENABLE_DEBUG_OUTPUT
void Dbg(const WCHAR* s)
{

    OutputDebugStringW(s);
    std::lock_guard<std::mutex> lk(g_logMx);
    WriteLogFile(s);
}
#else
#define  Dbg(s) 
#endif




// ---- 登录数据（watcher 读一次后缓存，供 libcef 回调使用）----
std::mutex g_mxData;
TLLoginSharedData g_data{};   // 零初始化
bool g_hasData = false;

// =====================================================================
// TinyBase64 —— 与加载器 CCookieMgr::DumpToken / 参考 DLL 完全同款
// （自定义字符表 + '$' 填充）。4399 token 就是用它编码的 cookie JSON。
// =====================================================================
class TinyBase64 {
public:
    explicit TinyBase64(const std::string& table =
        OBF("IVXLdjEB9k+sHtQx5YbDiafuMncN7RrUeJCOgp/KhT2FmZPw1o6WqvAlzy4S803G"))
        : chars_(table), pad_('=') {
        memset(rev_, -1, sizeof(rev_));
        for (int i = 0; i < 64; ++i) rev_[(uint8_t)chars_[i]] = i;
    }
    void setPadding(char p) { pad_ = p; }

    // 注意：尾组字节数必须按「实际读到的字符数」精确决定，不能用 i<=len 兜底。
    // 反例：原始 JSON 长度 %3==2 时编码后剥掉 1 个 '$' 填充，尾组剩 3 字符，
    // 应解出 2 个字节；若用 i<=len 会漏掉第 2 字节，导致 JSON 末字节缺失而解析失败。
    std::string decodeToString(const std::string& str) {
        std::string s = str;
        while (!s.empty() && s.back() == pad_) s.pop_back();
        std::string out;
        size_t i = 0, len = s.size();
        while (i < len) {
            int a = (i     < len) ? rev_[(uint8_t)s[i]]     : 0;
            int b = (i + 1 < len) ? rev_[(uint8_t)s[i + 1]] : 0;
            int c = (i + 2 < len) ? rev_[(uint8_t)s[i + 2]] : 0;
            int d = (i + 3 < len) ? rev_[(uint8_t)s[i + 3]] : 0;
            if (a < 0 || b < 0 || c < 0 || d < 0)
                throw std::runtime_error(OBF("invalid base64"));
            uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
            out.push_back((char)((triple >> 16) & 0xFF));
            if (i + 2 < len) out.push_back((char)((triple >> 8) & 0xFF));
            if (i + 3 < len) out.push_back((char)(triple & 0xFF));
            i += 4;
        }
        return out;
    }

    // 与 decodeToString 逆运算：3 字节一组 -> 4 字符；尾组按实际字节数补 '$' 填充
    std::string encodeToString(const std::string& in) {
        std::string out;
        size_t i = 0, n = in.size();
        while (i < n) {
            uint32_t b0 = (i     < n) ? (uint8_t)in[i]     : 0;
            uint32_t b1 = (i + 1 < n) ? (uint8_t)in[i + 1] : 0;
            uint32_t b2 = (i + 2 < n) ? (uint8_t)in[i + 2] : 0;
            uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
            out.push_back(chars_[(triple >> 18) & 0x3F]);
            out.push_back(chars_[(triple >> 12) & 0x3F]);
            if (i + 1 < n) out.push_back(chars_[(triple >> 6) & 0x3F]);
            else           out.push_back(pad_);
            if (i + 2 < n) out.push_back(chars_[triple & 0x3F]);
            else           out.push_back(pad_);
            i += 3;
        }
        return out;
    }

private:
    std::string chars_;
    char pad_;
    int rev_[256];
};

// =====================================================================
// 抓取模式（type=1000，仅 4399）：加载器点"确定"后置 TL_CAP_REQ，DLL 见到后才按需
// 触发一次 visit，遍历实际 cookie manager 中所有 4399 域 cookie，写回共享内存 param0。
// 回写格式与现有 DB 中 m_token 完全一致（供 GetUserNameByCookie / Apply4399Cookies 复用）。
//
// 关键修正 1：不能 hook set_cookie。服务器 HTTP 响应的 Set-Cookie 由 CEF 内部
//   net::CookieStore 直接落库，不经过 cef_cookie_manager_t::set_cookie 对外 C API，
//   所以改用 visit 遍历已落库的 cookie。
// 关键修正 2（本次）：visit 是异步的——其回调在 CEF IO 线程逐条触发，调用 visit 的
//   瞬间拿不到数据。若每 500ms 盲目 visit 轮询并写回，既浪费 CPU 又存在"读取早于回调
//   完成"的竞争。改为命令-应答：加载器置 REQ -> DLL 调一次 visit -> 等待 visitor 完成
//   事件（2s 超时兜底）-> 写回 param0 并置 RDY -> 加载器轮询 RDY（10s 超时）后读取。
//   稳定、零忙等、无竞争。
// =====================================================================
// 复用下方定义的代理 base 方法（前向声明，定义见 InitMyLoadHandler 附近）
static void CEF_CALLBACK my_proxy_add_ref(_cef_base_t* self);
static int  CEF_CALLBACK my_proxy_release(_cef_base_t* self);
static int  CEF_CALLBACK my_proxy_has_one_ref(_cef_base_t* self);

std::mutex g_capMx;
std::map<std::wstring, std::wstring> g_capCookies;   // name -> value（仅 4399 域）

static _cef_request_context_t* g_capReqCtx = nullptr;   // 首个浏览器创建时的 request context（可能为 nullptr）
static cef_cookie_manager_t*   g_capManager = nullptr;  // 实际用于抓取的 manager
static HANDLE                  g_capDoneEvent = nullptr; // visitor 完成事件（手动复位），抓完置位

// 实际生效的 manager：直接复用 my_create_browser 在 CEF 线程上缓存的全局 cookie manager。
// Apply4399Cookies 已验证 4399 登录态就存于该全局 manager，手动登录的 cookie 也落在
// 同一 store，因此抓它即可；且全程不经过任何带 completion callback 的未验证 C API。
static cef_cookie_manager_t* GetEffectiveManager() {
    return g_capManager;
}

// cookie visitor：visit 回调（CEF 内部线程）把 4399 域 cookie 累积进 g_capCookies。
// 每次 visit 全量遍历，按 name 覆盖写入，最终 g_capCookies 反映当前 CookieStore 快照。
static _cef_cookie_visitor_t g_capVisitor;
static int CEF_CALLBACK MyCookieVisit(struct _cef_cookie_visitor_t* self,
                                      const cef_cookie_t* cookie,
                                      int count, int total, int* deleteCookie)
{
    if (deleteCookie) *deleteCookie = 0;   // 不删除任何 cookie
    if (!cookie) {                         // 部分 CEF 版本以 NULL 表示枚举结束
        if (g_capDoneEvent) SetEvent(g_capDoneEvent);
        return 0;
    }
    if (cookie->domain.str && cookie->name.str) {
        std::wstring domain(cookie->domain.str, cookie->domain.length);
        if (domain.find(OBF(L"4399")) != std::wstring::npos) {
            std::wstring name(cookie->name.str, cookie->name.length);
            // 仅保留登录必需的关键 cookie（白名单）：避免回写整库 cookie 导致串过长。
            // 白名单名字均为 ASCII，直接用宽串字面量比对（DLL 不引入 obfusheader/OBF）。
            static const std::wstring kNeed[] = {
                OBF(L"USESSIONID"), OBF(L"Uauth"), OBF(L"Pauth"),
                OBF(L"ck_accname"), OBF(L"Puser"), OBF(L"Xauth")
            };
            bool bHas = false;
            for (const auto& n : kNeed) { if (n == name) { bHas = true; break; } }
            if (bHas) {
                std::wstring value(cookie->value.str, cookie->value.length);
                {
                    std::lock_guard<std::mutex> lk(g_capMx);
                    g_capCookies[name] = value;
                }
                #if TL_ENABLE_DEBUG_OUTPUT
                Dbg((L"[TestLoaderHook] capture cookie: " + name).c_str());
                #endif
            }
        }
    }
    // visit 异步：回调在 CEF IO 线程逐条触发。到达最后一条（count 从 0 起）时置完成事件，
    // 抓取线程据此等待后再读 g_capCookies。total<=0 时不提前置位，交由超时兜底以免截断。
    if (total > 0 && count + 1 >= total) {
        if (g_capDoneEvent) SetEvent(g_capDoneEvent);
    }
    return 1;  // 继续遍历剩余 cookie
}

static void InitCapVisitor()
{
    memset(&g_capVisitor, 0, sizeof(g_capVisitor));
    g_capVisitor.base.size = sizeof(_cef_cookie_visitor_t);
    g_capVisitor.base.add_ref = my_proxy_add_ref;
    g_capVisitor.base.release = my_proxy_release;
    g_capVisitor.base.has_one_ref = my_proxy_has_one_ref;
    g_capVisitor.visit = MyCookieVisit;
}

// UTF-16 -> UTF-8（JSON 值用 UTF-8 存储，与现有 m_token 格式一致）
static std::string W2U8(const std::wstring& s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}

// 真正执行一次抓取并写回共享内存（在 CaptureIpcThread 中按需调用，p 为已映射的共享内存视图）：
//   清空旧快照 -> 重置完成事件 -> 调一次 visit（异步）-> 等待 visitor 完成（2s 超时兜底）
//   -> 编码 g_capCookies 写回 param0 -> 置 TL_CAP_RDY 通知加载器。
static void DoCaptureOnce(TLLoginSharedData* p)
{
    g_capManager = GetEffectiveManager();
    if (!g_capManager) {
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] capture: no cookie manager");
        #endif
        return;
    }
    { std::lock_guard<std::mutex> lk(g_capMx); g_capCookies.clear(); }
    if (g_capDoneEvent) ResetEvent(g_capDoneEvent);
    g_capManager->visit_all_cookies(g_capManager, &g_capVisitor);  // 异步，回调在 CEF IO 线程执行
    if (g_capDoneEvent) WaitForSingleObject(g_capDoneEvent, 2000); // 等末条回调，超时兜底

    std::wstring cookieStr;
    {
        std::lock_guard<std::mutex> lk(g_capMx);
        if (!g_capCookies.empty()) {
            nlohmann::json j;
            for (auto& kv : g_capCookies)
                j[W2U8(kv.first)] = W2U8(kv.second);
            TinyBase64 b64; b64.setPadding('$');
            std::string enc = b64.encodeToString(j.dump());
            int n = MultiByteToWideChar(CP_ACP, 0, enc.c_str(), (int)enc.size(), nullptr, 0);
            if (n > 0) { cookieStr.resize((size_t)n); MultiByteToWideChar(CP_ACP, 0, enc.c_str(), (int)enc.size(), &cookieStr[0], n); }
        }
    }
    if (cookieStr.empty()) {
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] capture: no 4399 cookie collected");
        #endif
        return;
    }
    wcsncpy_s(p->param0, cookieStr.c_str(), _TRUNCATE);
    p->flags |= TL_CAP_RDY;                 // 通知加载器：param0 已就绪
    FlushViewOfFile(p, TL_LOGIN_STRUCT_SIZE);
    #if TL_ENABLE_DEBUG_OUTPUT
    Dbg(L"[TestLoaderHook] capture: done, RDY set");
    #endif
}

// 握手线程：仅以 100ms 轮询共享内存中的 TL_CAP_REQ 标志（4 字节读，开销极低），
// 见到后才调用一次 DoCaptureOnce，绝不做周期性 visit。加载器点"确定"后置 REQ，
// 抓完 DLL 置 RDY，加载器轮询 RDY 后读取——无忙等、无竞争。
DWORD WINAPI CaptureIpcThread(LPVOID)
{
    g_capDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    InitCapVisitor();
    wchar_t szName[128] = {};
    TLLoginMappingName(GetCurrentProcessId(), szName, 128);
    HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, szName);
    TLLoginSharedData* p = hMap ? static_cast<TLLoginSharedData*>(
        MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, TL_LOGIN_STRUCT_SIZE)) : nullptr;
    if (!p) {
        if (hMap) CloseHandle(hMap);
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] capture: cannot open shared memory");
        #endif
        return 0;
    }
    for (;;) {
        Sleep(100);
        if (p->magic == TL_LOGIN_MAGIC && (p->flags & TL_CAP_REQ)) {
            p->flags &= ~TL_CAP_REQ;        // 消费请求，避免重复触发
            #if TL_ENABLE_DEBUG_OUTPUT
            Dbg(L"[TestLoaderHook] capture: REQ seen, capturing...");
            #endif
            DoCaptureOnce(p);
        }
    }
}

// 抓取模式：在 my_create_browser 时机（浏览器/request_context 已就绪，与 Apply4399Cookies 同款）
// 启动握手线程。注意必须放到浏览器创建时机，不能在 InstallHooks（那时 manager 尚未初始化）。
// 线程平时空闲，仅在加载器置 TL_CAP_REQ 后才抓一次。
static void SetupCaptureHook()
{
    CloseHandle(CreateThread(nullptr, 0, CaptureIpcThread, nullptr, 0, nullptr));
    #if TL_ENABLE_DEBUG_OUTPUT
    Dbg(L"[TestLoaderHook] capture: IPC thread started (wait for REQ)");
    #endif
}


cef_string_t W(const WCHAR* s) {
    cef_string_t t{};
    t.str = const_cast<WCHAR*>(s);
    t.length = s ? wcslen(s) : 0;
    t.dtor = nullptr;
    return t;
}

// 原始 cookie 串 "NAME=VALUE; NAME2=VALUE2" -> map。
// 每段取首个 '='（值内可能含 '='，如 base64 token），并按段/按名 trim 空白。
// 与 DumpToken（base64）不同：原始串以 ';' 分隔，且 base64 字符表里不含 ';'，
// 因此不会误判。
void ParseRawCookie(const std::string& s, std::map<std::string, std::string>& out)
{
    size_t i = 0, n = s.size();
    while (i < n) {
        size_t semi = s.find(';', i);
        std::string part = (semi == std::string::npos) ? s.substr(i) : s.substr(i, semi - i);
        i = (semi == std::string::npos) ? n : semi + 1;

        // trim 整段前后空白
        size_t a = 0, b = part.size();
        while (a < b && (part[a] == ' ' || part[a] == '\t' || part[a] == '\r' || part[a] == '\n')) ++a;
        while (b > a && (part[b-1] == ' ' || part[b-1] == '\t' || part[b-1] == '\r' || part[b-1] == '\n')) --b;
        part = part.substr(a, b - a);
        if (part.empty()) continue;

        size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string name = part.substr(0, eq);
        std::string value = part.substr(eq + 1);
        size_t na = 0, nb = name.size();
        while (na < nb && name[na] == ' ') ++na;
        while (nb > na && name[nb-1] == ' ') --nb;
        name = name.substr(na, nb - na);
        if (!name.empty()) out[name] = value;
    }
}

// token -> map<name,value> -> 逐条 SetCookie
// DB 中 m_token 可能是两种格式（都要兼容）：
//   a) DumpToken：TinyBase64('$') 编码的 cookie JSON（密码登录入库路径）
//   b) 原始 cookie 串 "NAME=VALUE; NAME2=VALUE2"（从老 ini 导入路径）
// 优先尝试 base64->JSON；失败（含 ';' 的原始串会令 base64 解码抛异常）则按原始串解析。
bool Apply4399Cookies(const WCHAR* token)
{
    if (!token || !*token) {
    #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] 4399: empty token");
    #endif
        return false;
    }
    if (!g_pGetGlobalManager) {
    #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] 4399: no global manager fn");
    #endif
        return false;
    }

    // 1) token 收窄（纯 ASCII，CP_ACP 无损）
    int n = WideCharToMultiByte(CP_ACP, 0, token, (int)wcslen(token), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    std::string narrow((size_t)n, '\0');
    WideCharToMultiByte(CP_ACP, 0, token, (int)wcslen(token), &narrow[0], n, nullptr, nullptr);
    #if TL_ENABLE_DEBUG_OUTPUT
    Dbg(token);
    #endif

    // 2) token -> cookie map：先试 base64(JSON)，失败按原始 cookie 串解析
    std::map<std::string, std::string> cookies;
    bool fromJson = false;
    try {
        TinyBase64 b64;
        b64.setPadding('$');
        std::string js = b64.decodeToString(narrow);
        #if TL_ENABLE_DEBUG_OUTPUT
        OutputDebugStringA(js.c_str());
        #endif
        auto j = nlohmann::json::parse(js);
        if (j.is_object()) {
            for (auto& [k, v] : j.items())
                if (v.is_string()) cookies[k] = v.get<std::string>();
            fromJson = !cookies.empty();
        }
    }
    catch (const std::exception& e) {
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"error of decode b64");
        #endif
        #if TL_ENABLE_DEBUG_OUTPUT
        OutputDebugStringA(e.what());
        #endif
        /* 不是 base64/JSON，走原始 cookie 解析 */ }

    if (!fromJson)
        ParseRawCookie(narrow, cookies);

    if (cookies.empty()) {
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] 4399: token parse produced no cookies");
        #endif
        return false;
    }

    // 3) 全局 cookie manager，逐条写入（session cookie：secure=0 httponly=0 无过期）
    cef_cookie_manager_t* mgr = g_pGetGlobalManager(nullptr);
    if (!mgr || !mgr->set_cookie) {
    #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] 4399: get global manager failed");
    #endif
        return false;
    }

    cef_string_t url = W(k4399CookieUrl);
    int ok = 0;
    for (auto& [name, value] : cookies) {
        // JSON 字符串按 UTF-8 转宽字符（与参考工程 CefString 赋值语义一致）
        std::wstring wn = L"", wv = L"";
        auto toW = [](const std::string& s, std::wstring& out) {
            int m = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            if (m > 0) { out.resize((size_t)m); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], m); }
        };
        toW(name, wn);
        toW(value, wv);

        cef_cookie_t ck{};
        ck.name = W(wn.c_str());
        ck.value = W(wv.c_str());
        ck.domain = W(nullptr);
        ck.path = W(nullptr);
        ck.secure = 0;
        ck.httponly = 0;
        ck.has_expires = 0;
        if (mgr->set_cookie(mgr, &url, &ck, nullptr)) ++ok;
    }
    #if TL_ENABLE_DEBUG_OUTPUT
    wchar_t okbuf[256] = {};
    swprintf(okbuf, _countof(okbuf),
             L"[TestLoaderHook] 4399: cookies applied (%d of %d, %s)",
             ok, (int)cookies.size(), fromJson ? L"json" : L"raw");
    Dbg(okbuf);
    #endif
    return ok > 0;
}

// =====================================================================
// 台服：libcef hook + 登录页 JS 注入
// =====================================================================
typedef int (*cef_browser_host_create_browser_t)(
    const cef_window_info_t* windowInfo, struct _cef_client_t* client,
    const cef_string_t* url, const struct _cef_browser_settings_t* settings,
    struct _cef_request_context_t* request_context);

cef_browser_host_create_browser_t g_createbrowser = nullptr;
std::once_flag g_cookieOnce;               // 4399 cookie 只写一次
std::once_flag g_loadHandlerOnce;          // 台服 load handler 只初始化一次

// ---- 代理 load handler（参考工程同款桩：只填用到的字段）----
static _cef_load_handler_t g_my_load_handler;

static _cef_load_handler_t* CEF_CALLBACK
my_proxy_get_load_handler(_cef_client_t* self)
{
    return &g_my_load_handler;
}
static void CEF_CALLBACK my_proxy_add_ref(_cef_base_t* self) {}
static int  CEF_CALLBACK my_proxy_release(_cef_base_t* self) { return 1; }
static int  CEF_CALLBACK my_proxy_has_one_ref(_cef_base_t* self) { return 1; }

// JS 字符串字面量转义（账号/密码拼进 IIFE 实参）
std::wstring JsEscape(const std::wstring& s)
{
    std::wstring o;
    o.reserve(s.size() + 8);
    for (wchar_t c : s) {
        switch (c) {
        case L'\\': o += L"\\\\"; break;
        case L'"':  o += L"\\\""; break;
        case L'\n': o += L"\\n";  break;
        case L'\r': o += L"\\r";  break;
        default:    o += c;
        }
    }
    return o;
}

// ---- 4399 自动进服（参考 version_x64_passNp / DoInjectAutoJoin）----
// 落地页（如 https://my.4399.com/yxssjj/wd）加载完成后，等 jQuery 就绪，
// 拉 ajaxGetLastEnter 拿到上次进入的区服 ser_sid/ser_tj_name，再跳 play-sid 进服。
static std::mutex g_ajMx;
static std::wstring g_ajLastUrl;
static ULONGLONG g_ajLastTick = 0;

static bool Is4399AutoJoinTarget(const std::wstring& url)
{
    // 注入目标：4399 游戏域下的 yxssjj 页面（落地页如 .../yxssjj/wd）。
    // 排除：已跳转的 play-sid 页、已注入回执 (#yxy=)、以及 ajax 接口本身。
    if (url.find(OBF(L"my.4399.com")) == std::wstring::npos) return false;
    if (url.find(OBF(L"yxssjj")) == std::wstring::npos) return false;
    if (url.find(OBF(L"play-sid")) != std::wstring::npos) return false;
    if (url.find(OBF(L"#yxy=")) != std::wstring::npos) return false;
    if (url.find(OBF(L"ajaxGetLastEnter")) != std::wstring::npos) return false;
    return true;
}

// 4399 服列表：sid -> obj（站点标识）。与 version_x64_passNp version.cpp:2979-3003 一致。
// obj 为纯 ASCII 字符串（无编码坑）；DLL 按传入的 sid 查 obj 构造 play-sid URL。
static const wchar_t* Get4399ServerObj(int sid)
{
    struct Entry { int sid; const wchar_t* obj; };
    static const Entry kServers[] = {
        {3,  OBF(L"2_6")},  {5,  OBF(L"2_5")},  {8,  OBF(L"2_4")},  {10, OBF(L"2_3")},  {14, OBF(L"2_2")},  {16, OBF(L"2_1")},
        {2,  OBF(L"2_14")}, {4,  OBF(L"2_13")}, {6,  OBF(L"2_12")}, {7,  OBF(L"2_11")}, {9,  OBF(L"2_10")}, {11, OBF(L"2_9")},
        {13, OBF(L"2_8")},  {15, OBF(L"2_7")},  {1,  OBF(L"2_21")}, {12, OBF(L"2_20")}, {17, OBF(L"2_19")}, {18, OBF(L"2_18")},
        {19, OBF(L"2_17")}, {20, OBF(L"2_16")}, {21, OBF(L"2_15")},
    };
    for (const auto& e : kServers)
        if (e.sid == sid) return e.obj;
    return nullptr;
}

static void Do4399AutoJoin(_cef_frame_t* frame, const std::wstring& url, int sid)
{
    // 诊断回执：URL 带 #yxy= 说明上一轮已注入，记录结果并跳过（避免重复注入）
    size_t pR = url.find(OBF(L"#yxy="));
    if (pR != std::wstring::npos) {
        #if TL_ENABLE_DEBUG_OUTPUT
        std::wstring log = L"[TestLoaderHook] 4399: auto-join receipt=";
        log += url.substr(pR + 5);
        Dbg(log.c_str());
        #endif
        return;
    }

    // 3 秒去重（参考工程同款，避免 on_load_end 多次触发导致重复注入）
    {
        std::lock_guard<std::mutex> lk(g_ajMx);
        ULONGLONG now = GetTickCount64();
        if (url == g_ajLastUrl && now - g_ajLastTick < 3000) return;
        g_ajLastUrl = url;
        g_ajLastTick = now;
    }

    if (!Is4399AutoJoinTarget(url)) return;

    // 指定服（sid!=0）：直接用 sid + obj 跳指定服（参考 version.cpp AutoJoin>1 分支）。
    if (sid != 0) {
        const wchar_t* obj = Get4399ServerObj(sid);
        if (obj) {
            std::wstring code =OBF( LR"JS(
(function () {
    var n = 0, sid = ')JS");
            code += std::to_wstring(sid);
            code += OBF(LR"JS(', obj = ')JS");
            code += obj;
            code += OBF(LR"JS(';
    function rcpt(s) {
        try { history.replaceState(null, '', location.pathname + location.search + '#yxy=' + s); }
        catch (e) { try { location.hash = '#yxy=' + s; } catch (e2) { } }
    }
    function go() {
        try {
            if (!(window.$ && window.$.ajax)) {
                if (n++ < 100) { setTimeout(go, 100); return; }
                rcpt('no-jquery');
                return;
            }
            rcpt('ok-sid-' + sid);
            window.location.href = 'https://my.4399.com/yxssjj/play-sid-' + sid + '-site-' + obj + '-ref-wd-channel-my-randsj-' + Math.random();
        } catch (e) { rcpt('exception'); }
    }
    go();
})();
)JS");
            cef_string_t cs = W(code.c_str());
            cef_string_t su = W(OBF(L"YxYSoft://AutoLogin"));
            frame->execute_java_script(frame, &cs, &su, 0);
            #if TL_ENABLE_DEBUG_OUTPUT
            Dbg(L"[TestLoaderHook] 4399: auto-join(specified sid) js injected");
            #endif
            return;
        }
        // sid 不在已知服列表：回退到"最近服"
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] 4399: sid not in known server list, fallback to recent");
        #endif
    }

    std::wstring code =OBF( LR"JS(
(function () {
    var n = 0;
    function rcpt(s) {
        try { history.replaceState(null, '', location.pathname + location.search + '#yxy=' + s); }
        catch (e) { try { location.hash = '#yxy=' + s; } catch (e2) {} }
    }
    function go() {
        try {
            if (!(window.$ && window.$.ajax)) {
                if (n++ < 100) { setTimeout(go, 100); return; }
                rcpt('no-jquery');
                return;
            }
            $.ajax({
                url: 'https://my.4399.com/yxssjj/ajaxGetLastEnter?_AJAX_=1&' + Math.random(),
                type: 'get',
                success: function (res1) {
                    try {
                        var res = JSON.parse(res1);
                        if (res.status == 1) {
                            rcpt('ok-sid-' + res.data[0].ser_sid);
                            var url = 'https://my.4399.com/yxssjj/play-sid-' + res.data[0].ser_sid
                                + '-site-' + res.data[0].ser_tj_name
                                + '-ref-wd-channel-my-randsj-' + Math.random();
                            window.location.href = url;
                        } else {
                            rcpt('bad-status-' + res.status);
                        }
                    } catch (e) { rcpt('parse-error'); }
                },
                error: function () { rcpt('ajax-error'); }
            });
        } catch (e) { rcpt('exception'); }
    }
    go();
})();
)JS");

    cef_string_t cs = W(code.c_str());
    cef_string_t su = W(OBF(L"YxYSoft://AutoLogin"));
    frame->execute_java_script(frame, &cs, &su, 0);
    #if TL_ENABLE_DEBUG_OUTPUT
    Dbg(L"[TestLoaderHook] 4399: auto-join js injected");
    #endif
}

// 登录页 / 落地页加载完成 -> 按 type 分发注入
static void CEF_CALLBACK
my_on_load_end(_cef_load_handler_t* self,
               _cef_browser_t* browser,
               _cef_frame_t* frame,
               int httpStatusCode)
{
    if (!frame || !frame->is_main || !frame->is_main(frame))
        return;

    DWORD type = (DWORD)-1;
    DWORD flags = 0;
    {
        std::lock_guard<std::mutex> lk(g_mxData);
        if (!g_hasData) return;
        type = g_data.type;
        flags = g_data.flags;
    }

    cef_string_userfree_t url = frame->get_url(frame);
    std::wstring urlStr;
    if (url && url->str) urlStr = url->str;
    if (url && g_pUserfreeFree) g_pUserfreeFree(url);

    if (type == TL_ACCOUNT_TAIWAN) {
        bool hit = (urlStr == (LPCWSTR)kTwLoginUrlHttp || urlStr == (LPCWSTR)kTwLoginUrlHttps);
        if (!hit) return;

        std::wstring account, password;
        {
            std::lock_guard<std::mutex> lk(g_mxData);
            account = g_data.param0;
            password = g_data.param1;
        }

        std::wstring accountEsc = JsEscape(account);
        std::wstring passwordEsc = JsEscape(password);

        std::wstring code =OBF( LR"TWJS(
(function () {
    function doLogin(log_account, log_pwd) {
        var loginData = {}
        var regexp_mail = /^(\w-*\.*)+@(\w-?)+(\.\w{2,})+$/;
        var regPhone = /^1[3|4|5|7|8][0-9]{9}$/;
        if (regPhone.test(log_account)) {
            loginData = { type: 1, phone: log_account, password: log_pwd };
        } else if (regexp_mail.test(log_account)) {
            loginData = { type: 4, email: log_account, password: log_pwd };
        } else {
            loginData = { type: 2, account: log_account, password: log_pwd };
        }
        $.ajax({
            url: '/auth/login',
            data: JSON.stringify(loginData),
            type: 'post',
            dataType: 'json',
            contentType: "application/json",
            success: function success(res) {
                if (res.code == 0) {
                    var account = res.data.user_token;
                    var url = 'https://tw.battleteams1.com/index/play?isClient=1&token=' + account;
                    window.location.href = url;
                } else {
                    alert(res.msg);
                }
            },
            error: function error(rej) {}
        });
    }
    function fire(n) {
        try {
            if (window.$ && window.$.ajax) { doLogin(")TWJS");
        code += accountEsc;
        code += OBF(LR"TWJS(", ")TWJS");
        code += passwordEsc;
        code += OBF(LR"TWJS("); return; }
        } catch (e) {}
        if (n < 100) { setTimeout(function () { fire(n + 1); }, 100); }
    }
    fire(0);
})();
)TWJS");

        cef_string_t cs = W(code.c_str());
        cef_string_t su = W(OBF(L"YxYSoft://AutoLogin"));
        frame->execute_java_script(frame, &cs, &su, 0);
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] tw: login js injected");
        #endif
        return;
    }

    if (type == TL_ACCOUNT_4399) {
        int sid = (int)(flags & 0xFF);   // flags 低 8 位 = 4399 服 ID（sid），0=最近服
        Do4399AutoJoin(frame, urlStr, sid);
        return;
    }
}

static void InitMyLoadHandler()
{
    std::call_once(g_loadHandlerOnce, [] {
        memset(&g_my_load_handler, 0, sizeof(g_my_load_handler));
        g_my_load_handler.base.size = sizeof(_cef_load_handler_t);
        g_my_load_handler.base.add_ref = my_proxy_add_ref;
        g_my_load_handler.base.release = my_proxy_release;
        g_my_load_handler.base.has_one_ref = my_proxy_has_one_ref;
        g_my_load_handler.on_load_end = my_on_load_end;
    });
}

// cef_browser_host_create_browser hook：
//   台服：改写 client->get_load_handler 指向代理 handler
//   4399：首个浏览器创建前强制写入 cookie
static int CEF_CALLBACK
my_create_browser(const cef_window_info_t* windowInfo, struct _cef_client_t* client,
                  const cef_string_t* url, const struct _cef_browser_settings_t* settings,
                  struct _cef_request_context_t* request_context)
{
    // 双端都改写 client->get_load_handler：台服用它注入登录 JS，4399 用它在
    // 落地页 on_load_end 时注入自动进服脚本。
    if (client && client->get_load_handler) {
        DWORD oldProtect = 0;
        if (VirtualProtect(client, 0x1000, PAGE_READWRITE, &oldProtect)) {
            client->get_load_handler = my_proxy_get_load_handler;
            VirtualProtect(client, 0x1000, oldProtect, &oldProtect);
        }
    }
    // 缓存首个浏览器创建时的 request_context（可能为 nullptr=默认 context），
    // 并在 CEF 线程上直接取全局 cookie manager（与 Apply4399Cookies 同款、已验证），
    // 供 capture 模式在 worker 线程直接复用——避免 worker 线程调
    // request_context->get_default_cookie_manager(callback) 这条未验证路径（可能崩游戏端）。
    g_capReqCtx = request_context;
    if (g_pGetGlobalManager) g_capManager = g_pGetGlobalManager(nullptr);

    // 首个浏览器创建时：4399 强写 cookie；capture 启动握手线程（平时空闲，等加载器置 REQ 才抓）。
    // 此时全局/request_context 的 cookie manager 已就绪（与 Apply4399Cookies 同款时机）。
    std::call_once(g_cookieOnce, [] {
        std::lock_guard<std::mutex> lk(g_mxData);
        if (!g_hasData) return;
        if (g_data.type == TL_ACCOUNT_4399)
            Apply4399Cookies(g_data.param0);
        else if (g_data.type == TL_ACCOUNT_CAPTURE)
            SetupCaptureHook();
    });

    return g_createbrowser(windowInfo, client, url, settings, request_context);
}

// ---- CreateProcessW 诊断日志 hook（仅 4399）----
// 仅记录 launcher 拉起的子进程映像路径 + 命令行 + 工作目录，不做任何拦截/改写。
typedef BOOL (WINAPI* FnCreateProcessW)(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
static FnCreateProcessW g_RealCreateProcessW = nullptr;

typedef BOOL(WINAPI* FnCreateProcessA)(
	LPCSTR lpApplicationName, LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
static FnCreateProcessA g_RealCreateProcessA = nullptr;

#if TL_ENABLE_DEBUG_OUTPUT

static void LogCreateProcess(LPCWSTR app, LPCWSTR cmd, LPCWSTR cwd)
{
#if TL_ENABLE_DEBUG_OUTPUT
    std::wstring log = L"[TestLoaderHook] CreateProcessW: app=";
    log += (app ? app : L"(null)");
    log += L" cmd=";
    log += (cmd ? cmd : L"(null)");
    log += L" cwd=";
    log += (cwd ? cwd : L"(null)");
    Dbg(log.c_str());
#endif
}
static void LogCreateProcessA(LPCSTR app, LPCSTR cmd, LPCSTR cwd)
{
#if TL_ENABLE_DEBUG_OUTPUT
	std::string log = "[TestLoaderHook] CreateProcessA: app=";
	log += (app ? app : "(null)");
	log += " cmd=";
	log += (cmd ? cmd : "(null)");
	log += " cwd=";
	log += (cwd ? cwd : "(null)");
	OutputDebugStringA(log.c_str());
#endif
}
#else

#define LogCreateProcess(...)
#define LogCreateProcessA(...)
#endif
bool HasGameDesW(LPCWSTR s) {
	bool res = false;
	if (s && !IsBadStringPtr(s, 1)) {
		std::wstring str = s;
		//std::for_each(str.begin(), str.end(), [](char c)->char {return std::tolower(c);});
		for (auto& c : str) { c = std::tolower(c); };

		if (
			str.find(OBF(L".des")) != std::wstring::npos
			&& str.find(OBF(L"game")) != std::wstring::npos
			) {
			res = true;
		}
	}
	return res;
}
bool HasGameDesA(LPCSTR s) {
    bool res = false;
    if (s && !IsBadStringPtrA(s, 1)) {
        std::string str = s;
        //std::for_each(str.begin(), str.end(), [](char c)->char {return std::tolower(c);});
        for (auto& c : str) { c = std::tolower(c); };

        if (
            str.find(OBF(".des")) != std::wstring::npos
            && str.find(OBF("game")) != std::wstring::npos
            ) {
            res = true;
        }
    }
    return res;
}

static BOOL WINAPI MyCreateProcessA(
	LPCSTR lpApplicationName, LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	LogCreateProcessA(lpApplicationName, lpCommandLine, lpCurrentDirectory);
	if ( HasGameDesA(lpApplicationName) ||  HasGameDesA(lpCommandLine)  ) {
		
		 
            SuspendThread(GetCurrentThread());
			do
			{
				Sleep(INFINITE);
			} while (TRUE);
			ExitThread(0);
 
	}
	return g_RealCreateProcessA(lpApplicationName, lpCommandLine, lpProcessAttributes,
		lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
		lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

static BOOL WINAPI MyCreateProcessW(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    LogCreateProcess(lpApplicationName, lpCommandLine, lpCurrentDirectory);
	if (HasGameDesW(lpApplicationName) || HasGameDesW(lpCommandLine)) {


		SuspendThread(GetCurrentThread());
		do
		{
			Sleep(INFINITE);
		} while (TRUE);
		ExitThread(0);

	}
    return g_RealCreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes,
        lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
        lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

// 等 libcef 就绪后挂 hook（在 watcher 线程执行，不在 DllMain，避开 loader lock）
void InstallHooks()
{
    HMODULE hLibcef = nullptr;
    for (int i = 0; i < LIBCEF_WAIT_MS / LIBCEF_POLL_MS; ++i) {
        hLibcef = GetModuleHandleW(OBF(L"libcef.dll"));
        if (hLibcef) break;
        Sleep(LIBCEF_POLL_MS);
    }
    if (!hLibcef) {
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] libcef.dll not loaded within timeout, hooks NOT installed");
        #endif
        return;
    }

    g_createbrowser = (cef_browser_host_create_browser_t)
        GetProcAddress(hLibcef, OBF("cef_browser_host_create_browser"));
    if (!g_createbrowser) {
        #if TL_ENABLE_DEBUG_OUTPUT
        Dbg(L"[TestLoaderHook] cef_browser_host_create_browser export not found");
        #endif
        return;
    }
    g_pGetGlobalManager = (FnGetGlobalCookieManager)
        GetProcAddress(hLibcef, OBF("cef_cookie_manager_get_global_manager"));
    // 注意：cef_string_userfree_free 是宏，真实导出名是 _utf16_ 变体
    g_pUserfreeFree = (FnUserfreeFree)GetProcAddress(hLibcef, OBF("cef_string_userfree_utf16_free"));

    InitMyLoadHandler();

    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&g_createbrowser, my_create_browser);

    // 4399：额外 hook CreateProcessW，仅做诊断日志（确认 launcher 拉起的是哪个子进程）
    if (g_hasData && g_data.type == TL_ACCOUNT_4399) {
        HMODULE hK32 = GetModuleHandleW(OBF(L"kernel32.dll"));
        if (hK32) {
			g_RealCreateProcessW = (FnCreateProcessW)GetProcAddress(hK32, OBF("CreateProcessW"));
			g_RealCreateProcessA = (FnCreateProcessA)GetProcAddress(hK32, OBF("CreateProcessA"));

			if (g_RealCreateProcessW) {
				DetourAttach(&g_RealCreateProcessW, MyCreateProcessW);
			}
			if (g_RealCreateProcessA) {
				DetourAttach(&g_RealCreateProcessA, MyCreateProcessA);
			}
        }
    }

    // 抓取模式（type=1000）的握手线程启动已移至 my_create_browser 时机（见 SetupCaptureHook），
    // 此处不挂任何 capture 相关 hook：那时 cookie manager / request_context 尚未就绪，visit 无对象可遍历。

    LONG r = DetourTransactionCommit();
    #if TL_ENABLE_DEBUG_OUTPUT
    Dbg(r == 0 ? L"[TestLoaderHook] libcef hook installed"
               : L"[TestLoaderHook] DetourTransactionCommit failed");
    #endif
}

DWORD WINAPI LoginWatcherThread(LPVOID)
{
    wchar_t szName[128] = {};
    TLLoginMappingName(GetCurrentProcessId(), szName, 128);
    #if TL_ENABLE_DEBUG_OUTPUT
    wchar_t startbuf[256] = {};
    swprintf(startbuf, _countof(startbuf),
             L"[TestLoaderHook] watcher start pid=%u map=%s timeout=%dms",
             GetCurrentProcessId(), szName, WATCH_TIMEOUT_MS);
    Dbg(startbuf);
    #endif

    for (int i = 0; i < WATCH_TIMEOUT_MS / WATCH_INTERVAL_MS; ++i) {
        HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, szName);
        if (hMap) {
            const TLLoginSharedData* pData = static_cast<const TLLoginSharedData*>(
                MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, TL_LOGIN_STRUCT_SIZE));
            if (pData) {
                if (pData->magic == TL_LOGIN_MAGIC && pData->version == TL_LOGIN_VERSION) {
                    {
                        std::lock_guard<std::mutex> lk(g_mxData);
                        memcpy(&g_data, pData, TL_LOGIN_STRUCT_SIZE);
                        g_hasData = true;
                    }
                    #if TL_ENABLE_DEBUG_OUTPUT
                    Dbg(pData->type == TL_ACCOUNT_TAIWAN
                        ? L"[TestLoaderHook] login data received (taiwan)"
                        : pData->type == TL_ACCOUNT_CAPTURE
                        ? L"[TestLoaderHook] login data received (capture)"
                        : L"[TestLoaderHook] login data received (4399)");
                    #endif
                }
                else {
                    #if TL_ENABLE_DEBUG_OUTPUT
                    Dbg(L"[TestLoaderHook] shared data magic/version mismatch");
                    #endif
                }
                UnmapViewOfFile(pData);
            }
            CloseHandle(hMap);
            if (g_hasData) {
                InstallHooks();   // 4399 cookie 在首个浏览器创建时写入；台服等登录页
            }
            return 0;
        }
        Sleep(WATCH_INTERVAL_MS);
    }
    #if TL_ENABLE_DEBUG_OUTPUT
    Dbg(L"[TestLoaderHook] login data timeout");
    #endif
    return 0;
}

} // namespace

// rundll32 入口（导出序号 1）：detours 跨位宽 helper 协议。
// helper 进程（x64 rundll32）加载本 DLL 后由 detours 调用此函数完成注入。
extern "C" __declspec(dllexport) void CALLBACK HelperEntry(HWND h, HINSTANCE i, LPSTR c, INT n)
{
    if (DetourIsHelperProcess()) {
        DetourFinishHelperProcess(h, i, c, n);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 入口即打点：确认 DLL 确实被加载、跑在哪个进程、是不是 helper 进程。
        // helper 进程（rundll32）里只做注入，不起业务线程。
        wchar_t exepath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exepath, MAX_PATH);

#if TL_ENABLE_DEBUG_OUTPUT

        wchar_t buf[512] = {};
        swprintf(buf, _countof(buf),
                 L"[TestLoaderHook] DllMain attach pid=%u helper=%d exe=%s",
                 GetCurrentProcessId(), (int)DetourIsHelperProcess(), exepath);
        Dbg(buf);
#endif
        if (!DetourIsHelperProcess()) {
            CloseHandle(CreateThread(nullptr, 0, LoginWatcherThread, nullptr, 0, nullptr));
        }
    }
    return TRUE;
}
