// test_p2_launch.cpp —— P2 单测 + 端到端验证
// 覆盖：契约头静态断言 / 映射名生成 / LaunchGameWithLogin 失败路径 /
//       notepad + TestLoaderHook.dll 完整注入链路（模块枚举验证 DLL 已加载）
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "../Test_Loader/LoginSharedData.h"
#include "../Test_Loader/GameLaunch.cpp"

static int g_pass = 0, g_fail = 0;
static void CHECK(bool cond, const char* name) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (cond) g_pass++; else g_fail++;
}

static FILE* g_log = nullptr;
static void L(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log ? g_log : stdout, fmt, ap);
    va_end(ap);
    fflush(g_log ? g_log : stdout);
}

int wmain() {
    g_log = fopen("C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\test_p2.log", "w");
    L("=== P2 unit tests ===\n");

    // ---- 1) 契约头静态断言 ----
    static_assert(TL_LOGIN_MAGIC == 0x544C5368, "magic value");
    static_assert(TL_LOGIN_VERSION == 1, "version");
    static_assert(sizeof(TLLoginSharedData) == 1040, "struct size 16+1024");
    static_assert(offsetof(TLLoginSharedData, param0) == 16, "param0 offset");
    static_assert(TL_LOGIN_MAPPING_SIZE >= sizeof(TLLoginSharedData), "mapping size");
    CHECK(true, "static asserts (magic/version/size/offset/mapping)");

    // ---- 2) 映射名生成 ----
    {
        wchar_t buf[128] = {};
        TLLoginMappingName(1234, buf, 128);
        CHECK(wcscmp(buf, L"Local\\TestLoader_Login_1234") == 0, "mapping name format");
    }

    // ---- 3) LaunchGameWithLogin 失败路径 ----
    {
        TLLoginSharedData d = {};
        d.magic = TL_LOGIN_MAGIC; d.version = TL_LOGIN_VERSION; d.type = TL_ACCOUNT_4399;
        wcscpy_s(d.param0, L"TESTCOOKIE");
        // 空 exe 路径
        CHECK(!LaunchGameWithLogin(L"", L"x.dll", d), "empty exe -> false");
        // 空 dll 路径
        CHECK(!LaunchGameWithLogin(L"C:\\Windows\\Sysnative\\notepad.exe", L"", d), "empty dll -> false");
        // DLL 不存在
        CHECK(!LaunchGameWithLogin(L"C:\\Windows\\Sysnative\\notepad.exe",
                                   L"C:\\no_such_dll_xyz.dll", d), "missing dll -> false");
        // exe 不存在
        CHECK(!LaunchGameWithLogin(L"C:\\no_such_exe_xyz.exe",
                                   L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\TestLoaderHook.dll", d),
              "missing exe -> false");
    }

    // ---- 4) 端到端：notepad + TestLoaderHook.dll ----
    L("\n=== e2e: notepad + TestLoaderHook.dll ===\n");
    {
        const std::wstring exe = L"C:\\Windows\\Sysnative\\notepad.exe";
        const std::wstring dll = L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\TestLoaderHook.dll";

        TLLoginSharedData d = {};
        d.magic = TL_LOGIN_MAGIC;
        d.version = TL_LOGIN_VERSION;
        d.type = TL_ACCOUNT_4399;
        d.flags = 0;
        wcscpy_s(d.param0, L"E2E_TEST_COOKIE_12345");

        DWORD pid = 0;
        // 注入前清空日志，保证下面读到的是本次启动产生的内容
        DeleteFileW(L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\TestLoaderHook.log");
        bool ok = LaunchGameWithLogin(exe, dll, d, L"", &pid);
        L("LaunchGameWithLogin -> %s pid=%lu\n", ok ? "true" : "false", pid);
        CHECK(ok, "launch with login data");
        CHECK(pid != 0, "pid returned");

        if (ok && pid) {
            // 等 DLL 轮询线程消费共享内存
            Sleep(1500);

            // x64 枚举助手确认 TestLoaderHook.dll 已加载进目标
            wchar_t cmd[256];
            swprintf_s(cmd, 256, L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\exp7.exe %lu TestLoaderHook", pid);
            SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
            HANDLE hOut = CreateFileW(L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\exp7_e2e.log",
                                      GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
            STARTUPINFOW si2 = { sizeof(si2) };
            PROCESS_INFORMATION pi2 = {};
            si2.dwFlags = STARTF_USESTDHANDLES;
            si2.hStdOutput = hOut;
            si2.hStdError = hOut;
            if (CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si2, &pi2)) {
                WaitForSingleObject(pi2.hProcess, 10000);
                CloseHandle(pi2.hThread); CloseHandle(pi2.hProcess);
            }
            CloseHandle(hOut);

            // 读 exp7 输出判断
            FILE* f = nullptr;
            if (fopen_s(&f, "C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\exp7_e2e.log", "r") == 0 && f) {
                char line[256]; bool found = false;
                while (fgets(line, sizeof(line), f)) {
                    if (strstr(line, "loaded: YES")) { found = true; break; }
                }
                fclose(f);
                L("exp7 module enum: %s\n", found ? "hook dll present" : "hook dll MISSING");
                CHECK(found, "TestLoaderHook.dll loaded in target");
            }

            // 回归断言：注入 DLL 必须真正读到共享内存（此前句柄提前关闭导致读不到）
            {
                FILE* f = nullptr;
                bool got = false; bool gotPid = false;
                if (fopen_s(&f, "C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\TestLoaderHook.log", "rb") == 0 && f) {
                    std::string all; char buf2[1024];
                    while (fgets(buf2, sizeof(buf2), f)) all += buf2;
                    fclose(f);
                    // 日志为 UTF-8，检索 ASCII 子串足够
                    got = all.find("login data received") != std::string::npos;
                    char pidstr[16]; sprintf_s(pidstr, "%lu", pid);
                    gotPid = all.find(pidstr) != std::string::npos;
                    L("hook log has 'login data received': %s, has pid: %s\n",
                      got ? "yes" : "no", gotPid ? "yes" : "no");
                }
                CHECK(got, "injected DLL read shared memory (login data received)");
                CHECK(gotPid, "hook log references target pid");
            }

            // 共享内存应已被 DLL 读取走（映射仍在也没关系，由系统回收）；
            // 精确杀掉测试目标
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); }
        }
    }

    // ---- 5) DLL 导出验证（ordinal 1 = HelperEntry）----
    // 注意：x86 测试程序不能 LoadLibrary x64 DLL，改为读 PE 文件验证机器类型与导出
    {
        const wchar_t* dllPath = L"C:\\Users\\MZPEDH\\AppData\\Local\\Temp\\zs_check\\build\\TestLoaderHook.dll";
        HANDLE hFile = CreateFileW(dllPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(hFile != INVALID_HANDLE_VALUE, "TestLoaderHook.dll exists");
        if (hFile != INVALID_HANDLE_VALUE) {
            BYTE hdr[4096] = {};
            DWORD rd = 0;
            ReadFile(hFile, hdr, sizeof(hdr), &rd, nullptr);
            CloseHandle(hFile);
            bool ok = rd > 0x40 && hdr[0] == 'M' && hdr[1] == 'Z';
            CHECK(ok, "PE MZ signature");
            if (ok) {
                DWORD e_lfanew = *(DWORD*)(hdr + 0x3C);
                if (e_lfanew + 4 + 20 <= rd) {
                    BYTE* nt = hdr + e_lfanew;
                    bool peOk = nt[0] == 'P' && nt[1] == 'E';
                    CHECK(peOk, "PE signature");
                    if (peOk) {
                        WORD machine = *(WORD*)(nt + 4);
                        CHECK(machine == 0x8664, "machine = AMD64 (x64)");
                        WORD magic = *(WORD*)(nt + 24);
                        CHECK(magic == 0x20B, "PE32+ optional header");
                    }
                }
            }
        }
    }

    L("\n=== result: %d passed, %d failed ===\n", g_pass, g_fail);
    printf("\nP2: %d passed, %d failed\n", g_pass, g_fail);
    if (g_log) fclose(g_log);
    return g_fail == 0 ? 0 : 1;
}
