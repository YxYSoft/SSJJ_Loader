#pragma once
// LoginSharedData.h —— 加载器(x86 exe) 与 游戏进程内 DLL(x64) 的共享内存契约
//
// 契约内容：
//   1. 加载器以 CREATE_SUSPENDED + DetourCreateProcessWithDllExW 启动游戏并注入
//      TestLoaderHook.dll（此时主线程仍挂起）。
//   2. 加载器按 PID 创建命名映射 Local\TestLoader_Login_<PID>，写入登录数据。
//   3. 加载器 ResumeThread 恢复游戏主线程。
//   4. DLL 内后台线程轮询同名映射，读到合法数据后按 type 分发执行登录动作。
//   任一步失败：加载器 TerminateProcess，绝不让无登录数据的游戏进程跑起来。
#include <windows.h>
#include <cstdio>
#include <obfusheader.h>

#define TL_LOGIN_MAGIC    'TLSh'          // 0x544C5368
#define TL_LOGIN_VERSION  1

// 账号类型：0=4399（老棉裤 cookie），100=台服（账号+密码），1000=抓取登录信息（仅 4399）
enum TLAccountType {
    TL_ACCOUNT_4399    = 0,
    TL_ACCOUNT_TAIWAN  = 100,
    TL_ACCOUNT_CAPTURE = 1000,  // 抓取模式：DLL 不自动登录，改为后台抓 cookie 写回 param0
};

// flags 位定义（capture 抓取模式内部握手，仅 4399 抓取使用）
#define TL_CAP_REQ  0x00000100   // 加载器 -> DLL：请求立即抓取（visit）并回写 param0
#define TL_CAP_RDY  0x00000200   // DLL -> 加载器：抓取完成，param0 已就绪可读取

// 共享数据定长结构（后续可能调整，两端同步改）
#pragma pack(push, 1)
struct TLLoginSharedData {
    DWORD magic;         // TL_LOGIN_MAGIC
    DWORD version;       // TL_LOGIN_VERSION
    DWORD type;          // TLAccountType
    DWORD flags;         // 预留参数（UINT32）：
                        //   低 8 位  = 4399 服 ID（sid），0=最近服；台服/抓取不使用（置 0）
                        //   位 8 (0x100) = CAP_REQ：加载器->DLL 请求立即抓取并回写 param0（仅抓取模式）
                        //   位 9 (0x200) = CAP_RDY：DLL->加载器 抓取完成，param0 已就绪可读取（仅抓取模式）
                        //   位 10..31 = 预留给后续用途
    WCHAR param0[2048];  // 4399/TW=cookie/账号；CAPTURE=回写的登录 cookie（base64-JSON，与 DB 同格式）
    WCHAR param1[1024];  // 4399=空；   台服=密码
};
#pragma pack(pop)

// 映射大小：4KB 足够容纳一个结构，取 64KB 留余量
#define TL_LOGIN_MAPPING_SIZE  (64 * 1024)

// 生成映射名 Local\TestLoader_Login_<pid>
inline void TLLoginMappingName(DWORD pid, wchar_t* buf, size_t cch)
{
    swprintf_s(buf, cch, OBF(L"Local\\TestLoader_Login_%lu"), pid);
}

// 结构自检（编译期大小断言用）
#define TL_LOGIN_STRUCT_SIZE  (sizeof(TLLoginSharedData))
