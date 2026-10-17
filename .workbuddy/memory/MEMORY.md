# Test_Loader 项目长期记忆

## 构建与编码铁律
- 源码 `.cpp/.h` 必须 UTF-8 BOM + CRLF（防 MSVC C3861）；改完用纯字节操作归一（先 CRLF→LF→CRLF，再补 BOM）
- 本环境 **MSBuild.exe 被 LOLBin 策略硬拦**（含 dangerouslyDisableSandbox）；替代：`_build_check/` 下 cl.exe/link.exe/rc.exe 手工构建脚本（build_all.sh 全量、zs_check.sh 语法检查）；链接必须带 comdlg32.lib
- 皮肤 zip：`Test_Loader.DuiProj/Test_Loader.DuiProj.zip`，ZipCrypto 加密，**原密码重打包**（7z -tzip -mem=ZipCrypto，条目/加密/逐文件 diff 校验后替换，旧包备份 .bak）；zip 内 XML 与 DuiProj 目录版一致（改动要同步两边）

## 关键约定
- 账号稳定 ID = sqlite `user.id` = `UserInfo::m_nIndex`；DB=%APPDATA%\ssjj_gamedata.db（sqlcipher key `YxYSoft_db_key`）；`user` 表列：`id,username,password,detal,token,ntype,serverid`（2026-10-15 加 serverid，每账号独立的 4399 服 ID，0=最近服；老库 ALTER 迁移）。`UserInfo.m_serverId` 对应，默认 0
- **ntype 语义：0=4399，1=老棉裤导入账号（仍按4399），100=台服**（2026-10-13 定）
- **游戏默认路径注册表（2026-10-15 实测）**：`HKCU\SOFTWARE\Wooduan\SSJJ-4399` 与 `SSJJ-tw`（均在 64 位 native 视图，非 WOW6432Node），值 `InstallPath` 指向安装根目录；启动器在 `<InstallPath>\MicroClient64\` 下。4399 启动器文件名是 **`MicroClient.exe`（字母 l）**；磁盘上曾出现的 `MicroC1ient.exe`（数字 1）是错的/被替换版本，**不要使用**。台服是 `LiuseLoader.exe`。`CMainDlg::GetSSJJInstallPath(nType)` 已封装「注册表探测 + 候选名存在性命中 + config 兜底」
- URL 协议 `yxysoft://login-<id>`：payload 要容忍末尾 `/`（cmd start / .url 双击都会补）；解析在 UrlProtocol.cpp，分发 CMainDlg::HandleProtocolPayload
- 共享内存契约（**已落地 2026-10-14；2026-10-15 补充 flags 语义；2026-10-15 加 capture**）：`Test_Loader/LoginSharedData.h`，magic 'TLSh'/version 1/type(**0=4399，100=台服，1000=抓取登录信息(仅4399)**)/**flags（UINT32：低8位=4399服ID(sid)，0=最近服；高24位预留；台服/capture不使用置0；不新增字段、不bump version）**/param0(WCHAR[2048]：4399/TW=cookie或账号；**capture=回写的登录cookie(base64-JSON，与DB同格式)**)/param1(台服密码)；映射名 `Local\TestLoader_Login_<PID>`（64KB）；exe 与 DLL 共用同一头文件（DLL 相对路径 include `../Test_Loader/LoginSharedData.h`）。**param0 由 1024 扩到 2048 属结构变更，exe+DLL 必须一起重编**
- 启动链（**已落地**）：`Test_Loader/GameLaunch.h/.cpp` `LaunchGameWithLogin(exe,dll,data[,cmdLine[,pOutPid[,pOutMap]]])` = `DetourCreateProcessWithDllExW(CREATE_SUSPENDED)` → 写共享内存 → ResumeThread；任一步失败即 TerminateProcess（detours 内部失败时已终止）。`pOutMap` 非 null 时返回映射句柄给调用方（用于 capture 模式读回写结果，不跑 5s 延迟关闭）。`CMainDlg::LoginIdx` 按 ntype 分发：100→GamePathTW+账号密码，其余→GamePath+cookie；**登录不再写 ini**（4399 的 ini 写入改由 DLL 在目标进程内完成）；4399 仍保留 ini 存在性检查。菜单「棉裤导入添加」(`HandleMenuAddByDll`) 与「编辑DLL导入」(`HandleMenuEditDll`) 已改为 capture 模式（启动4399+DLL注入type=1000+弹框阻塞+读回param0入库），**彻底移除 配置信息.ini 文件依赖**（老棉裤 ini 导入已废弃）
- **detours 跨位宽注入核心经验（2026-10-14 实测）**：x86 加载器调 `DetourCreateProcessWithDllExW` 注入 x64 目标时，同位宽路径必失败，detours 自动走 helper：启动 `%WINDIR%\sysnative\rundll32.exe "<用户DLL>",#1`（x64 rundll32），**被注入 DLL 必须导出序号 1（rundll32 入口签名 `void CALLBACK(HWND,HINSTANCE,LPSTR,INT)`）并在其中调 `DetourIsHelperProcess()`+`DetourFinishHelperProcess()`**，否则 rundll32 静默退出码 0、注入"假成功"（目标无 DLL、无报错）。DllMain 里也要用 `DetourIsHelperProcess()` 区分 helper 进程与目标进程（helper 里不起业务线程，否则被 ExitProcess 杀）
- helper DLL 路径改写约定：x86 调用方传的 DLL 路径含 "32." 会被 AllocExeHelper 自动替换为 "64."（跨位宽命名约定）

## 技术资产位置
- Microsoft Detours 4.0.1 完整源码：`H:/2022_WorkSpace/smtv2/detour/`（detours.cpp/creatwth.cpp/disasm.cpp/modules.cpp + h；creatwth.cpp 含 DetourCreateProcessWithDllA/W 与跨位宽 UpdateFrom32To64，**x86 loader 可注入 x64 目标**，DLL 需 x64）；wx_Tool/detour 有同样副本；网上 git/curl 下载被 TLS 拦，用本地副本
- **detours 静态库已编译**：`_build_check/detours_x86/detours.lib`（加载器用）、`_build_check/detours_x64/detours.lib`（TestLoaderHook 用）；复现脚本 `_build_check/build_detours.sh`。**uimports.cpp 不单独编译**（被 creatwth.cpp include，Makefile OBJS 只列 10 个文件）。detours.h 里 `lpDllName` 即使是 W 版也是 LPCSTR（需 WideCharToMultiByte CP_ACP）
- 测试脚本：`_build_check/run_tests.sh` 一键跑 P1（24 项）+P2（14 项）harness；P1 链接需 DbWork.obj+CppSQLite3.obj+sqlite3.obj+libcrypto_static.lib+user32/advapi32（EncodingConverter 实现在 DbWork.cpp 的 `#define ENCODING_CPP 1`）
- DUIThink：库 `H:/360Downloads/DUIThink_git/lib`（x86 + x64 静态库都有）；**源码含头文件在 `H:/360Downloads/DUIThink_git/DUIThink`**（Position 属性解析可查 DUIControlBase.cpp / tagDuiPosition 对齐值）
- XML 布局 Position 是 9 段 dash 串（对齐类型+left/top/right/bottom 偏移+固定尺寸），仿造控件行时改 top 段偏移即可
- 菜单/设置加项套路：ResourceCtrlID.h 加 ID → Attribute.xml 加文本 ValueID → XML 加节点 → 代码分发 → 重打包 zip

## 环境事实
- loader exe = requireAdministrator + x86；游戏（WDLauncher.exe、台服）均 x64；子进程继承 token 同完整性级别，共享内存无跨 IL 问题（仍加 SDDL Everyone 保底）
- 工具链：VS2022 v143（14.51.36231），SDK 10.0.26100.0；python 用 managed 3.13.12 全路径
