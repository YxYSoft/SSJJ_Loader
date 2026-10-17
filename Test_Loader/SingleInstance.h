#pragma once
#ifndef SINGLE_INSTANCE_H
#define SINGLE_INSTANCE_H

#include <windows.h>
#include <string>

// 单例管理类（完全独立，不耦合任何其他类）
class SingleInstanceManager
{
private:
    HANDLE m_hMutex;               // 命名互斥量句柄
    std::wstring m_mutexName;      // 互斥量名字（唯一）
    std::wstring m_propName;       // 窗口属性名（唯一）
    HWND m_hFirstWnd;              // 找到的第一个实例窗口句柄

    // 回调函数：EnumWindows 遍历窗口时调用
    static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);

public:
    /**
     * @brief 构造函数
     * @param mutexName 命名互斥量的唯一名字（建议用GUID）
     * @param propName 窗口属性的唯一名字（建议用GUID）
     */
    SingleInstanceManager(const std::wstring& mutexName, const std::wstring& propName);
    ~SingleInstanceManager();

    /**
     * @brief 检查是否已有实例运行
     * @return true：已有实例运行；false：当前是第一个实例
     */
    bool HasAnotherInstance();

    /**
     * @brief 【第一个实例调用】给窗口打唯一属性标签（用于被第二个实例找到）
     * @param hWnd 要标记的窗口句柄（可以是SystemTray的隐藏窗口，也可以是主窗口）
     */
    void MarkWindow(HWND hWnd);

    /**
     * @brief 【第二个实例调用】激活第一个实例的窗口
     * @param pPayload 可选：随激活一并传递给第一个实例的数据（如 URL 协议 payload）
     *                 NULL 表示无数据。找不到窗口时会写入兜底文件
     * @return true：激活成功；false：未找到窗口
     */
    bool ActivateFirstInstance(const wchar_t* pPayload = nullptr);
};

#endif // SINGLE_INSTANCE_H