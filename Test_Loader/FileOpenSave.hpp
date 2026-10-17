#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <memory>
#include <shellapi.h>
#include <commdlg.h>
// 文件过滤器结构
struct FileFilter {
	std::wstring description;  // 文件类型描述
	std::wstring extensions;   // 文件扩展名，多个用分号分隔，如 L"*.txt;*.doc"

	FileFilter(const std::wstring& desc, const std::wstring& ext)
		: description(desc), extensions(ext) {
	}
};

// 文件选择结果
struct FileSelectionResult {
	std::wstring filePath;           // 单个文件路径
	std::vector<std::wstring> filePaths;  // 多个文件路径
	bool isMultiSelect;              // 是否是多选
	bool success;                    // 是否成功选择
	std::wstring errorMessage;      // 错误信息

	FileSelectionResult()
		: isMultiSelect(false), success(false) {
	}
};

// 文件选择对话框类
class FileDialog {
public:
	// 构造函数
	FileDialog()
		: m_hwndOwner(nullptr)
		, m_title(L"选择文件")
		, m_initialDir(L"")
		, m_defaultExt(L"")
		, m_multiSelect(false)
		, m_saveDialog(false)
		, m_folderDialog(false) {
	}

	// 设置父窗口句柄
	FileDialog& setOwner(HWND hwnd) {
		m_hwndOwner = hwnd; 
		return *this;
	}

	// 设置对话框标题
	FileDialog& setTitle(const std::wstring& title) { 
		m_title = title; 
		return *this;
	}

	// 设置初始目录
	FileDialog& setInitialDirectory(const std::wstring& dir) {
		m_initialDir = dir;
		return *this;
	}

	// 设置默认扩展名
	FileDialog& setDefaultExtension(const std::wstring& ext) {
		m_defaultExt = ext;
		return *this;
	}

	// 设置是否允许多选
	FileDialog& setMultiSelect(bool multi) {
		m_multiSelect = multi;
		return *this;
	}

	// 设置是否为保存对话框
	FileDialog& setSaveDialog(bool save) {
		m_saveDialog = save;
		return *this;
	}

	// 设置是否为文件夹选择对话框
	FileDialog& setFolderDialog(bool folder) {
		m_folderDialog = folder;
		return *this;
	}

	// 添加文件过滤器
	FileDialog& addFilter(const std::wstring& description, const std::wstring& extensions) {
		m_filters.emplace_back(description, extensions);
		return *this;
	}

	// 清除所有过滤器
	FileDialog& clearFilters() {
		m_filters.clear(); 
		return *this;
	}

	// 选择文件（返回单个文件路径）
	std::wstring selectFile() {
		FileSelectionResult result = selectFileInternal(false);
		if (!result.success && !result.errorMessage.empty()) {
			// 可以选择抛出异常或记录错误日志
			// throw std::runtime_error(std::string(result.errorMessage.begin(), result.errorMessage.end()));
		}
		return result.filePath;
	}

	// 选择多个文件
	std::vector<std::wstring> selectMultipleFiles() {
		FileSelectionResult result = selectFileInternal(true);
		if (!result.success && !result.errorMessage.empty()) {
			// 可以选择抛出异常或记录错误日志
		}
		return result.filePaths;
	}

	// 选择文件夹
	std::wstring selectFolder() {
		FileSelectionResult result = selectFolderInternal();
		if (!result.success && !result.errorMessage.empty()) {
			// 可以选择抛出异常或记录错误日志
		}
		return result.filePath;
	}

	// 选择文件（返回完整结果）
	FileSelectionResult selectFileWithResult(bool multiSelect = false) {
		return selectFileInternal(multiSelect);
	}

private:
	// 内部实现：选择文件
	FileSelectionResult selectFileInternal(bool multiSelect) {
		FileSelectionResult result;

		// 如果设置的是文件夹选择，则使用文件夹选择对话框
		if (m_folderDialog) {
			return selectFolderInternal();
		}

		// 准备OPENFILENAME结构
		OPENFILENAMEW ofn = { 0 };
		wchar_t szFile[MAX_PATH] = { 0 };
		wchar_t szFileTitle[MAX_PATH] = { 0 };

		// 如果是多选，需要更大的缓冲区
		std::vector<wchar_t> multiFileBuffer;
		wchar_t* pBuffer = szFile;

		if (multiSelect) {
			// 为多选分配更大的缓冲区
			multiFileBuffer.resize(65536, 0);
			pBuffer = multiFileBuffer.data();
		}
		else {
			szFile[0] = L'\0';
		}

		ofn.lStructSize = sizeof(OPENFILENAMEW);
		ofn.hwndOwner = m_hwndOwner;
		auto resFilter = buildFilterString();
		ofn.lpstrFilter = resFilter.c_str();
		ofn.lpstrFile = pBuffer;

		if (multiSelect) {
			ofn.nMaxFile = static_cast<DWORD>(multiFileBuffer.size());
		}
		else {
			ofn.nMaxFile = MAX_PATH;
		}

		ofn.lpstrFileTitle = szFileTitle;
		ofn.nMaxFileTitle = MAX_PATH;
		ofn.lpstrInitialDir = m_initialDir.empty() ? nullptr : m_initialDir.c_str();
		ofn.lpstrTitle = m_title.c_str();

		// 设置标志
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

		if (multiSelect) {
			ofn.Flags |= OFN_ALLOWMULTISELECT;
		}

		if (m_saveDialog) {
			ofn.Flags |= OFN_OVERWRITEPROMPT;
		}

		ofn.lpstrDefExt = m_defaultExt.empty() ? nullptr : m_defaultExt.c_str();

		// 调用对话框
		BOOL dialogResult;

		if (m_saveDialog) {
			dialogResult = GetSaveFileNameW(&ofn);
		}
		else {
			dialogResult = GetOpenFileNameW(&ofn);
		}

		if (dialogResult) {
			result.success = true;

			if (multiSelect) {
				// 处理多选结果
				result.isMultiSelect = true;
				parseMultiSelectFiles(pBuffer, result.filePaths);

				if (!result.filePaths.empty()) {
					result.filePath = result.filePaths[0]; // 第一个文件作为主路径
				}
			}
			else {
				// 单选结果
				result.isMultiSelect = false;
				result.filePath = pBuffer;
				result.filePaths.push_back(pBuffer);
			}
		}
		else {
			// 获取错误信息
			DWORD error = CommDlgExtendedError();
			if (error != 0) {
				result.errorMessage = L"对话框错误代码: " + std::to_wstring(error);
			}
			else {
				result.errorMessage = L"用户取消选择";
			}
			result.success = false;
		}

		return result;
	}

	// 内部实现：选择文件夹
	FileSelectionResult selectFolderInternal() {
		FileSelectionResult result;

		// 使用BROWSEINFO结构创建文件夹选择对话框
		BROWSEINFOW bi = { 0 };
		wchar_t szDisplayName[MAX_PATH];

		bi.hwndOwner = m_hwndOwner;
		bi.pszDisplayName = szDisplayName;
		bi.lpszTitle = m_title.c_str();
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;

		if (!m_initialDir.empty()) {
			bi.lpfn = BrowseCallbackProc;
			bi.lParam = reinterpret_cast<LPARAM>(m_initialDir.c_str());
		}

		// 显示对话框
		PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);

		if (pidl != nullptr) {
			// 获取选择的路径
			wchar_t szPath[MAX_PATH];
			if (SHGetPathFromIDListW(pidl, szPath)) {
				result.success = true;
				result.filePath = szPath;
				result.filePaths.push_back(szPath);
			}
			else {
				result.success = false;
				result.errorMessage = L"无法获取文件夹路径";
			}

			// 释放内存
			CoTaskMemFree(pidl);
		}
		else {
			result.success = false;
			result.errorMessage = L"用户取消选择";
		}

		return result;
	}

	// 构建过滤器字符串
	std::wstring buildFilterString() {
		std::wstring filterStr;

		if (m_filters.empty()) {
			// 默认过滤器：所有文件
			filterStr = L"所有文件 (*.*)\0*.*\0";
		}
		else {
			for (const auto& filter : m_filters) {
				filterStr += filter.description + L'\0' + filter.extensions + L'\0';
			}
		}

		// 添加终止符
		filterStr += L'\0';

		return filterStr;
	}

	// 解析多选文件结果
	void parseMultiSelectFiles(const wchar_t* buffer, std::vector<std::wstring>& filePaths) {
		filePaths.clear();

		if (buffer == nullptr || buffer[0] == L'\0') {
			return;
		}

		// 第一个字符串是目录
		const wchar_t* p = buffer;
		std::wstring directory = p;

		// 移动到下一个字符串
		p += directory.length() + 1;

		if (*p == L'\0') {
			// 如果第二个字符串是空的，说明只选择了一个文件
			filePaths.push_back(directory);
		}
		else {
			// 多个文件，组合路径
			while (*p != L'\0') {
				std::wstring filename = p;
				std::wstring fullPath = directory + L"\\" + filename;
				filePaths.push_back(fullPath);
				p += filename.length() + 1;
			}
		}
	}

	// 文件夹选择对话框回调函数
	static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
		if (uMsg == BFFM_INITIALIZED) {
			// 设置初始目录
			SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
		}
		return 0;
	}

private:
	HWND m_hwndOwner;                 // 父窗口句柄
	std::wstring m_title;             // 对话框标题
	std::wstring m_initialDir;         // 初始目录
	std::wstring m_defaultExt;         // 默认扩展名
	bool m_multiSelect;               // 是否允许多选
	bool m_saveDialog;                // 是否为保存对话框
	bool m_folderDialog;              // 是否为文件夹选择对话框
	std::vector<FileFilter> m_filters; // 文件过滤器
};