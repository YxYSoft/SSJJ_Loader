#include "ReadUserTxt.h"

// 工具函数：去除字符串两端的空白字符
std::string trim(const std::string& str) {
	const auto start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";

	const auto end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}

// 主函数：读取用户文件
ReadResult readUsersFromFile(const std::wstring& filename) {
	ReadResult result;
	std::ifstream file(filename);

	// 检查文件是否成功打开
	if (!file.is_open()) {
		result.success = false;
		result.errorMessage = L"无法打开文件: " + filename;
		return result;
	}

	std::string line;
	int lineNumber = 0;

	while (std::getline(file, line)) {
		lineNumber++;

		// 跳过空行
		if (line.empty()) continue;

		// 跳过注释行（以#或//开头）
		std::string trimmedLine = trim(line);
		if (trimmedLine.empty() ||
			trimmedLine[0] == '#' ||
			(trimmedLine.size() >= 2 && trimmedLine.substr(0, 2) == "//")) {
			continue;
		}

		// 查找分隔符"----"
		size_t separatorPos = line.find("----");
		if (separatorPos == std::string::npos) {
			continue;
		}

		// 分割字符串
		std::string account = line.substr(0, separatorPos);
		std::string password = line.substr(separatorPos + 4);  // 4是"----"的长度

		// 去除空白字符
		account = trim(account);
		password = trim(password);

		// 检查是否为空
		if (account.empty() || password.empty()) {
			continue;
		}

		// 添加到vector
		result.users.emplace_back(account, password);
	}

	// 检查读取过程中是否出错
	if (file.bad()) {
		result.success = false;
		result.errorMessage = L"读取文件时发生I/O错误";
		return result;
	}

	// 检查文件是否为空
	if (result.users.empty()) {
		result.success = false;
		result.errorMessage = L"文件为空或没有有效数据";
		return result;
	}

	file.close();
	return result;
}