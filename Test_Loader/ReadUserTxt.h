#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <utility>
// 定义用户结构体
struct User {
	std::string account;
	std::string password;

	User() = default;
	User(const std::string& acc, const std::string& pwd)
		: account(acc), password(pwd) {
	}
};

// 读取结果结构体，包含数据和错误信息
struct ReadResult {
	std::vector<User> users;      // 读取到的用户数据
	bool success;                 // 是否成功
	std::wstring errorMessage;     // 错误信息（如果有）
	int errorLine;                // 出错的行号（0表示无错误）

	ReadResult() : success(true), errorLine(0) {}
};

ReadResult readUsersFromFile(const std::wstring& filename);