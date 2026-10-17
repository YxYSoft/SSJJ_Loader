// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#pragma warning(disable: 4018 4244 4005 4141 4101 4003 4819)
// #pragma disable warning(4018)
// #pragma disable warning(4244)
// #pragma disable warning(4005)
//#pragma disable warning(4141)

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#define DEFAULT_WINDOW_TITLE  _T("********魂飞飞********")

//////////////////////////////////////////////////////////////////////////
#include "../XUI/XUIHead.h"
#include "../Test_Loader.DuiProj/ResourceCtrlID.h"
#ifdef _WIN64
#ifdef _DEBUG
#pragma comment(lib, "../lib/MMHelper.201764_Static_MT_D.lib")
#pragma comment(lib, "../lib/DUIThink.201764_Static_MT_D.lib")
#pragma comment(lib, "../lib/tinyxml2.201764_MT_D.lib")
#else
//#pragma comment(lib, "../lib/MMHelper.201764_Static_MT.lib")
#pragma comment(lib, "../lib/XUI.201764_Static_MT.lib")
//#pragma comment(lib, "../lib/tinyxml2.201764_MT.lib")
#endif
#else
#ifdef _DEBUG
#pragma comment(lib, "../../../lib/MMHelper.2017D.lib")
#pragma comment(lib, "../../../lib/DUIThink.2017D.lib")
#else
#pragma comment(lib, "../lib/MMHelper.2017_Static_MT.lib")
#pragma comment(lib, "../lib/DUIThink.2017_Static_MT.lib")
#pragma comment(lib, "../lib/tinyxml2.2017_MT.lib")

#endif

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
  // 关闭时：所有 OutputDebugString 调用替换为空操作（不进 DbgView，不影响 Release 运行）
  #ifdef OutputDebugStringA
    #undef OutputDebugStringA
  #endif
  #define OutputDebugStringA(lpOutputString)  ((void)0)
  #ifdef OutputDebugStringW
    #undef OutputDebugStringW
  #endif
  #define OutputDebugStringW(lpOutputString)  ((void)0)
  #ifdef OutputDebugString
    #undef OutputDebugString
  #endif
  #define OutputDebugString(lpOutputString)   ((void)0)
#endif
#endif