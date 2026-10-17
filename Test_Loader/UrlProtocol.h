#pragma once
#ifndef URL_PROTOCOL_H
#define URL_PROTOCOL_H

#include <windows.h>
#include <string>

/**
 * URL 协议激活框架（yxysoft://）
 * 独立模块：注册 / 解析 / 快捷方式 / 兜底文件，不耦合业务
 */

/**
 * @brief 注册 yxysoft 协议到 HKCU\Software\Classes（当前用户，无需管理员）
 *        覆盖写天然幂等；失败仅内部记录，不弹窗
 * @return true: 写入成功
 */
bool EnsureProtocolRegistered();

/**
 * @brief 解析命令行中的 yxysoft://<payload>
 * @param cmdLine 完整命令行（如 GetCommandLine 或 lpCmdLine）
 * @param outPayload 输出解码后的 payload（UrlUnescape）
 * @return true: 命中协议 URL
 */
bool ParseProtocolUrl(const std::wstring& cmdLine, std::wstring& outPayload);

/**
 * @brief 在桌面创建账号快捷方式（.url Internet Shortcut）
 *        内容 URL=yxysoft://login-<nAccountId>，双击即唤起并执行
 * @param nAccountId 账号稳定 ID（数据库 user.id）
 * @param strAccountName 账号显示名（用于文件名，非法字符自动净化）
 * @return true: 创建成功
 */
bool CreateAccountShortcut(unsigned nAccountId, const std::wstring& strAccountName);

/**
 * @brief 兜底：把 payload 写入 %TEMP%\yxysoft_pending_payload.txt
 *        用于首实例窗口尚未就绪（如 splash 动画期）时第二个实例传递参数
 */
bool WritePendingPayloadFile(const std::wstring& strPayload);

/**
 * @brief 读取并删除兜底文件（若存在）
 * @return true: 读到内容
 */
bool ReadPendingPayloadFile(std::wstring& outPayload);

#endif // URL_PROTOCOL_H
