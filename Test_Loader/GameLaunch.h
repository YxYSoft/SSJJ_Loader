#pragma once
// GameLaunch.h —— 统一启动链：挂起创建 -> detours 注入 -> 共享内存写登录数据 -> 恢复
#include <string>
#include "LoginSharedData.h"

// 以 CREATE_SUSPENDED 创建目标进程并注入 DLL，写入共享内存登录数据后恢复主线程。
// 任一步失败都会 TerminateProcess 目标，绝不带病恢复。成功返回 true。
// exePath  : 游戏 exe 完整路径（宽字符）
// dllPath  : 注入 DLL 完整路径（宽字符，内部转 ANSI 给 detours）
// loginData: 已填好的契约结构
// cmdLine  : 可选命令行（默认空）
// pOutPid  : 可选，回传目标进程 PID
// pOutMap  : 可选，抓取模式下回传映射句柄（调用方读取回写结果后自行关闭）；
//            为 nullptr 时走默认 5 秒延迟关闭逻辑
bool LaunchGameWithLogin(const std::wstring& exePath,
                         const std::wstring& dllPath,
                         const TLLoginSharedData& loginData,
                         const std::wstring& cmdLine = L"",
                         DWORD* pOutPid = nullptr,
                         HANDLE* pOutMap = nullptr);
