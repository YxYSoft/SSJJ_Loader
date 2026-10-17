// P1 双客户端 harness：台服账号入库契约 / 托盘[台]前缀 / GamePathTW+DllPath 配置回环 / 菜单ID唯一性
// 全部使用隔离环境（临时DB、临时ini），不碰用户真实数据
#include "dbwork.h"
#include "ConfigManager.hpp"
#include "EncodingConverter.hpp"
#include "../Test_Loader.DuiProj/ResourceCtrlID.h"
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include <shlobj.h>

static int g_nPass = 0;
static int g_nFail = 0;

#define CHECK(cond, name) do { \
    if (cond) { ++g_nPass; printf("  PASS  %s\n", name); } \
    else { ++g_nFail; printf("  FAIL  %s\n", name); } \
} while (0)

// ============================================================
// 1. DB 契约：台服账号（ntype=100）入库/读出往返（隔离临时DB）
//    复刻 CMainDlg::HandleMenuAddTwAccount 的 UserInfo 构造与
//    DBWork::InsertUser / GetUsers 的 SQL 语义
// ============================================================
static void TestDbTwAccountRoundtrip()
{
    printf("[1] DB 台服账号入库契约（隔离临时DB）\n");
    wchar_t szTemp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, szTemp);
    std::wstring strDb = std::wstring(szTemp) + L"p1_test_twaccount.db";
    DeleteFileW(strDb.c_str());

    CppSQLite3DB db;
    db.open(strDb.c_str());
    std::string strKey = "YxYSoft_db_key";
    db.setKey(strKey.c_str(), strKey.size());
    // 与 InitDB 同款建表
    db.execDML("CREATE TABLE user (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT, password TEXT, detal TEXT, token TEXT, ntype INTEGER)");

    auto InsertLikeDbWork = [&](const UserInfo& user) {
        // 与 DBWork::InsertUser 完全相同的 SQL 与绑定方式
        auto stmt = db.compileStatement("INSERT INTO user (username, password, detal, token, ntype) VALUES (?,?,?,?,?)");
        auto W2U8 = EncodingConverter::UnicodeToUTF8;
        stmt.bind(1, W2U8(user.m_username).c_str());
        stmt.bind(2, W2U8(user.m_password).c_str());
        stmt.bind(3, W2U8(user.m_detal).c_str());
        stmt.bind(4, W2U8(user.m_token).c_str());
        stmt.bind(5, user.m_nType);
        stmt.execDML();
    };
    auto GetUsersLikeDbWork = [&](std::vector<UserInfo>& users) {
        users.clear();
        auto query = db.execQuery("SELECT  id, username, password, detal, token, ntype FROM user");
        auto U82W = EncodingConverter::UTF8ToUnicode;
        while (!query.eof()) {
            UserInfo user;
            user.m_nIndex = query.getIntField(0);
            user.m_username = U82W(query.getStringField(1));
            user.m_password = U82W(query.getStringField(2));
            user.m_detal = U82W(query.getStringField(3));
            user.m_token = U82W(query.getStringField(4));
            user.m_nType = query.getIntField(5);
            users.emplace_back(std::move(user));
            query.nextRow();
        }
    };

    // --- 场景A：HandleMenuAddTwAccount 构造的台服账号 ---
    // 源码：UserInfo user{ dlg.GetUsername(), dlg.GetPassword(), L"", dlg.GetUsername(), 100, 0 };
    UserInfo twUser;
    twUser.m_username = L"twplayer001";
    twUser.m_password = L"p@ssw0rd";
    twUser.m_token = L"";
    twUser.m_detal = L"twplayer001";
    twUser.m_nType = 100;
    twUser.m_nIndex = 0;
    InsertLikeDbWork(twUser);

    // --- 场景B：4399 账号（ntype=0，参照现有 AddByPwd 构造） ---
    UserInfo p4399;
    p4399.m_username = L"user4399";
    p4399.m_password = L"secret4399";
    p4399.m_token = L"cookie_abc123";
    p4399.m_detal = L"user4399";
    p4399.m_nType = 0;
    p4399.m_nIndex = 0;
    InsertLikeDbWork(p4399);

    std::vector<UserInfo> users;
    GetUsersLikeDbWork(users);
    CHECK(users.size() == 2, "插入2个账号后读回数量=2");
    if (users.size() == 2) {
        CHECK(users[0].m_nType == 100, "第1条 ntype==100（台服）");
        CHECK(users[0].m_username == L"twplayer001", "台服 username 往返一致");
        CHECK(users[0].m_password == L"p@ssw0rd", "台服 password 往返一致");
        CHECK(users[0].m_token.empty(), "台服 token 为空（只存账号密码）");
        CHECK(users[0].m_detal == L"twplayer001", "台服 detal=username");
        CHECK(users[0].m_nIndex == 1, "台服 id 自增=1");
        CHECK(users[1].m_nType == 0, "第2条 ntype==0（4399，不受影响）");
        CHECK(users[1].m_token == L"cookie_abc123", "4399 token(cookie) 往返一致");
    }

    // --- 场景C：老棉裤账号 ntype=1 继续按 4399 语义（不拦截） ---
    UserInfo mjk;
    mjk.m_username = L"";
    mjk.m_password = L"";
    mjk.m_token = L"old_cookie_xyz";
    mjk.m_detal = L"老棉裤账号";
    mjk.m_nType = 1;
    mjk.m_nIndex = 0;
    InsertLikeDbWork(mjk);
    GetUsersLikeDbWork(users);
    CHECK(users.size() == 3 && users[2].m_nType == 1 && users[2].m_token == L"old_cookie_xyz",
        "ntype=1 老棉裤账号原样存取（兼容）");

    db.close();
    DeleteFileW(strDb.c_str());
}

// ============================================================
// 2. 托盘 [台] 前缀逻辑（复刻 refreshUserList 表达式）
//    源码：(user.m_nType == 100 ? std::wstring(L"[台] ") + text : text)
// ============================================================
static void TestTrayPrefix()
{
    printf("[2] 托盘菜单 [台] 前缀逻辑\n");
    auto TrayText = [](const UserInfo& user) -> std::wstring {
        std::wstring text = user.m_detal;
        if (text.empty()) {
            text = user.m_username;
        }
        return (user.m_nType == 100 ? std::wstring(L"[台] ") + text : text);
    };

    UserInfo tw; tw.m_nType = 100; tw.m_detal = L"台服小号"; tw.m_username = L"tw1";
    UserInfo n;  n.m_nType = 0;   n.m_detal = L"4399大号"; n.m_username = L"u1";
    UserInfo m;  m.m_nType = 1;   m.m_detal = L"老棉裤账号"; m.m_username = L"";
    UserInfo twEmpty; twEmpty.m_nType = 100; twEmpty.m_detal = L""; twEmpty.m_username = L"backupname";

    CHECK(TrayText(tw) == L"[台] 台服小号", "台服账号托盘显示 [台] 前缀");
    CHECK(TrayText(n) == L"4399大号", "4399账号托盘无前缀");
    CHECK(TrayText(m) == L"老棉裤账号", "老棉裤账号托盘无前缀");
    CHECK(TrayText(twEmpty) == L"[台] backupname", "台服detal为空时前缀+username回退");
}

// ============================================================
// 3. GamePathTW / DllPath 配置回环（隔离临时ini）
// ============================================================
static void TestConfigRoundtrip()
{
    printf("[3] GamePathTW / DllPath 配置回环（隔离临时ini）\n");
    wchar_t szTemp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, szTemp);
    std::string strIni = []() {
        char sz[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, sz);
        return std::string(sz) + "p1_test_config.ini";
    }();
    DeleteFileA(strIni.c_str());

    // --- GamePathTW：set -> save -> load 回环（复刻 CSettingDlg 用法） ---
    {
        ConfigManager cfg;
        cfg.set("SETTING", "GamePathTW", "D:\\Games\\TW\\TWLauncher.exe");
        cfg.set("SETTING", "GamePath", "D:\\Games\\4399\\WDLauncher.exe");
        CHECK(cfg.save(strIni), "保存配置到临时ini");
    }
    {
        ConfigManager cfg;
        cfg.load(strIni);
        std::string tw = cfg.get<std::string>("SETTING", "GamePathTW", "");
        std::string old = cfg.get<std::string>("SETTING", "GamePath", "");
        CHECK(tw == "D:\\Games\\TW\\TWLauncher.exe", "GamePathTW 读写回环一致");
        CHECK(old == "D:\\Games\\4399\\WDLauncher.exe", "GamePath（4399）不受影响");
    }

    // --- DllPath：默认值语义（复刻 GetHookDllPath 逻辑） ---
    auto ResolveDllPath = [](ConfigManager& cfg) -> std::wstring {
        auto dllpath = cfg.get<std::string>("SETTING", "DllPath", "");
        if (!dllpath.empty()) {
            return EncodingConverter::GBKToUnicode(dllpath);
        }
        wchar_t szExePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        std::wstring strDll = szExePath;
        size_t pos = strDll.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            strDll = strDll.substr(0, pos + 1);
        }
        strDll += L"TestLoaderHook.dll";
        return strDll;
    };
    {
        ConfigManager cfg;
        cfg.load(strIni); // 无 DllPath 键
        std::wstring dll = ResolveDllPath(cfg);
        CHECK(dll.size() > 18 && dll.substr(dll.size() - 18) == L"TestLoaderHook.dll",
            "DllPath 未配置 -> 默认 exe同目录\\TestLoaderHook.dll");
    }
    {
        ConfigManager cfg;
        cfg.set("SETTING", "DllPath", "D:\\hook\\MyHook64.dll");
        cfg.save(strIni);
        ConfigManager cfg2;
        cfg2.load(strIni);
        CHECK(ResolveDllPath(cfg2) == L"D:\\hook\\MyHook64.dll", "DllPath 已配置 -> 优先生效");
    }
    DeleteFileA(strIni.c_str());
}

// ============================================================
// 4. 菜单 ID 唯一性（编译期可验证部分）
// ============================================================
static void TestMenuIdUniqueness()
{
    printf("[4] 菜单/控件 ID 唯一性\n");
    CHECK(ID_MENU_ADD_TWACCOUNT == 100125, "ID_MENU_ADD_TWACCOUNT == 100125");
    CHECK(ID_MENU_ADD_TWACCOUNT != ID_MENU_CREATE_SHORTCUT, "不与创建快捷方式ID冲突");
    CHECK(ID_MENU_ADD_TWACCOUNT != ID_MENU_ADD_BYPWD && ID_MENU_ADD_TWACCOUNT != ID_MENU_ADD_BYTXT
        && ID_MENU_ADD_TWACCOUNT != ID_MENU_ADD_BYDLL, "不与现有添加账号ID冲突");
    CHECK(IDC_ContainerCtrl_GamePathTW == 100121 && IDC_StaticCtrl_GamePathTW == 100122
        && IDC_EditCtrl_GamePathTW == 100123 && IDC_ButtonCtrl_SelectGamePathTW == 100124,
        "设置界面新控件ID 100121-100124 正确");
    CHECK(ID_MENU_ADD_TWACCOUNT != IDC_ContainerCtrl_GamePathTW
        && ID_MENU_ADD_TWACCOUNT != IDC_ButtonCtrl_SelectGamePathTW, "菜单ID与控件ID不重叠");
}

int main()
{
    printf("=== P1 双客户端 harness ===\n");
    TestDbTwAccountRoundtrip();
    TestTrayPrefix();
    TestConfigRoundtrip();
    TestMenuIdUniqueness();
    printf("\n=== 结果: %d PASS, %d FAIL ===\n", g_nPass, g_nFail);
    return g_nFail == 0 ? 0 : 1;
}
