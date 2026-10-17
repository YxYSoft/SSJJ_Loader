# Test_Loader

4399 / 台服游戏登录注入启动器：负责账号管理、游戏路径探测、基于 Detours 的 DLL 注入与共享内存登录信息传递，并在目标进程内完成 cookie / 登录信息抓取。

---

## 开源组件与依赖（版本 / License 速查）

| 组件 | 版本 | 许可 | 仓库 / 许可链接 |
| --- | --- | --- | --- |
| **XUI**（UI 框架） | 见仓库 Releases（项目链接标签 `XUI.201764`） | 见仓库 LICENSE | https://github.com/miaomiaoyule/XUI |
| **Microsoft Detours** | 4.0.1 | MIT | https://github.com/microsoft/Detours · LICENSE: https://github.com/microsoft/Detours/blob/main/LICENSE.md |
| **SQLite** | 3.44.2 | 公有领域（public domain） | https://www.sqlite.org/ · 版权: https://www.sqlite.org/copyright.html |
| **SQLCipher** | 4.5.6（community） | BSD 风格（Zetetic） | https://github.com/sqlcipher/sqlcipher · 许可: https://www.zetetic.net/sqlcipher/license/ |
| **CppSQLite3** | V3.2（2004–2005 分支） | zlib 风格（原作者 Robin Richter） | http://www.codeproject.com/Articles/6343/CppSQLite3 |
| **nlohmann/json** | 3.12.0 | MIT | https://github.com/nlohmann/json · LICENSE: https://github.com/nlohmann/json/blob/develop/LICENSE.MIT |
| **tinyxml2** | 10.0.0 | zlib | https://github.com/leethomason/tinyxml2 · LICENSE: https://github.com/leethomason/tinyxml2/blob/master/LICENSE |
| **obfusheader（OBF 宏）** | 未知（项目内集成） | 待补充 | — |
| **DUIThink / MMHelper** | XUI前身 | XUI前身 | — |

> 说明：SQLite / SQLCipher / nlohmann/json / tinyxml2 / Detours 的版本号均从项目实际引入的源码/头文件宏中核出（详见各小节）；XUI、obfusheader、DUIThink、MMHelper 未在仓库内暴露版本宏，以链接标签或仓库 Release 为准。
> **所有 License 条款以各组件官方仓库最新 LICENSE 文本为准**，本表/本文不替代原始许可。

---

## 一、UI 框架

### XUI
- **仓库**：https://github.com/miaomiaoyule/XUI
- **版本**：见仓库 Releases；项目以静态库标签 `XUI.201764_Static_MT.lib`（Release）/ `XUI.201764_Static_MT_D.lib`（Debug）链接，201764 为构建标签而非语义版本。
- **用途**：整个加载器的 UI 框架（窗口、控件、布局、消息循环、皮肤渲染）。
- **集成方式**：以 `../XUI/XUIHead.h` 引入，静态链接上述 XUI 库。
- **备注**：XUI 属于 DuiLib 系 UI 引擎封装，底层与项目随附的 `DUIThink` 库同源，XUI 在其之上提供更高层封装。
- **许可**：见仓库 LICENSE 文件。

## 二、API Hook / DLL 注入

### Microsoft Detours
- **版本**：4.0.1（源码宏 `DETOURS_VERSION = 0x4c0c1`；本地完整源码副本，随工程编译）。
- **用途**：运行时 API 挂钩（挂 `CreateProcessW/A`、`cef_browser_host_create_browser` 等），以及跨位宽 DLL 注入（`DetourCreateProcessWithDllExW`，x86 加载器注入 x64 目标，自动走 helper 协议由 x64 rundll32 加载）。
- **集成方式**：静态库
  - 加载器：`detours_x86/detours.lib`
  - 注入 DLL：`detours_x64/detours.lib`
- **许可**：MIT License（Microsoft）。LICENSE：https://github.com/microsoft/Detours/blob/main/LICENSE.md

## 三、数据存储

### SQLCipher（加密版 SQLite）
- **版本**：SQLCipher **4.5.6**（community 构建，`CIPHER_VERSION_NUMBER 4.5.6` / `CIPHER_VERSION_BUILD community`）；底层 SQLite 内核 **3.44.2**（`SQLITE_VERSION "3.44.2"`）。
- **用途**：本地账号库（`%APPDATA%\ssjj_gamedata.db`），使用密钥 `YxYSoft_db_key` 加密存储账号、密码、cookie、token、服 ID 等敏感数据。
- **集成方式**：直接编译 SQLite + SQLCipher 源码，**非预编译库**：
  - `sqlite3/sqlite3.c`、`sqlite3/sqlite3.h`（SQLite 内核 3.44.2）
  - `sqlite3/crypto.c`、`crypto_cc.c`、`crypto_impl.c`、`crypto_openssl.c`、`sqlcipher.h`（SQLCipher 4.5.6 加密层）
- **许可**：SQLite 内核为公有领域（public domain）；SQLCipher 由 Zetetic LLC 以 BSD 风格开源许可发布，商业用途请遵循 Zetetic 官方许可条款。LICENSE：https://www.zetetic.net/sqlcipher/license/

### CppSQLite3
- **版本**：V3.2（Robin Richter 2004–2005 年分支；无独立语义版本号，随 SQLite 3.44.2 适配）。
- **用途**：SQLite 的轻量 C++ 封装，项目通过 `dbwork.h` 中的 `CppSQLite3DB` 操作数据库。
- **集成方式**：`sqlite3/CppSQLite3.h` / `CppSQLite3.cpp` 源码随工程编译。
- **许可**：zlib 风格（见源文件头声明，原作者 Robin Richter）。源：http://www.codeproject.com/Articles/6343/CppSQLite3

## 四、序列化与解析

### nlohmann/json
- **版本**：3.12.0（`NLOHMANN_JSON_VERSION_MAJOR/MINOR/PATCH = 3/12/0`）。
- **用途**：cookie / 登录信息的 JSON 解析与构造（4399 cookie 经 base64(JSON) 传输，台服登录信息回写亦为 JSON）。
- **集成方式**：单头文件 `json.hpp`（header-only，位于项目 include 路径）。
- **许可**：MIT License。LICENSE：https://github.com/nlohmann/json/blob/develop/LICENSE.MIT

### tinyxml2
- **版本**：10.0.0（`TINYXML2_MAJOR/MINOR/PATCH_VERSION = 10/0/0`）。
- **用途**：皮肤 XML 与配置 XML 解析。
- **集成方式**：静态库 `tinyxml2.201764_MT*.lib`（Debug/Release 变体）。
- **许可**：zlib License。LICENSE：https://github.com/leethomason/tinyxml2/blob/master/LICENSE

## 五、代码防护

### obfusheader（OBF 宏）
- **版本**：未知（项目内集成，未暴露上游版本/仓库）。
- **用途**：编译期字符串常量加密（XOR + 控制流混淆），使配置键名、URL、调试文本等明文不暴露在二进制中。项目内典型用法：`OBF("SETTING")`、`OBF(L"GamePath")`、`OBF(L"DllPath")`、URL 协议文本 `[UrlProtocol] ...` 等。
- **集成方式**：头文件 `obfusheader.h`；EXE 侧及多个依赖源中以 `#include <obfusheader.h>` 引入。DLL 侧按既定工程策略**不引入** OBF（其调试字符串已被 `#if TL_ENABLE_DEBUG_OUTPUT` 整段排除，不会进 Release 二进制）。
- **许可**：见源文件署名（项目内集成，原始作者/仓库待补充）。

## 六、项目自研模块（非第三方，仅作区分）

- `LoginSharedData.h`：加载器（EXE）与注入 DLL 之间的共享内存契约（magic / version / type / flags / param0 / param1），定义于 EXE 与 DLL 共用的同一头文件。
- `GameLaunch.cpp`：基于 Detours 的 `LaunchGameWithLogin` 注入启动封装（挂起创建 → 写共享内存 → 恢复线程）。
- `DbWork.cpp` / `dbwork.h`：账号库读写（含 `EncodingConverter` GBK/UTF-8 编码转换）。
- `UrlProtocol.cpp`：自定义 `yxysoft://login-<id>` URL 协议解析与分发。
- `TestLoaderHook`（注入 DLL）：libcef 挂钩、cookie / 登录信息抓取、跨位宽注入 helper 处理。

## 七、许可与署名注意事项

1. 分发 / 商用前，请重点确认 **SQLCipher**（Zetetic 商业许可）与 **Detours**（MIT）的条款，其余组件多为 MIT / zlib / 公有领域，约束较轻。
2. 字符串混淆（OBF）仅增加逆向成本，**不构成加密保护**；敏感密钥（如 `YxYSoft_db_key`）仍以常量形式参与派生，请勿依赖其“保密性”。
3. 本文档随项目演进维护，新增第三方依赖时请同步补充对应条目、版本号与 License 链接。
