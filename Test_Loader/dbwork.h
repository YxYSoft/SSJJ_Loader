#pragma once
#include < string>

#include "sqlite3/CppSQLite3.h"
#include <vector>







struct UserInfo {
	std::wstring m_username;
	std::wstring m_password;
	std::wstring m_token;
	std::wstring m_detal;
	int m_nType;
	int m_nIndex;
	int m_serverId = 0;   // 4399 服 ID（每账号独立，随账号存库）；0=最近服，台服无效
	UserInfo() : m_nType(0), m_nIndex(0), m_serverId(0) {}
	UserInfo(const std::wstring& username, const std::wstring& password, const std::wstring& token, const std::wstring& detal, int nType, int nIndex) :
		m_username(username), m_password(password), m_token(token), m_detal(detal), m_nType(nType), m_nIndex(nIndex), m_serverId(0) {}
};

class DBWork {
public:
	DBWork() =default;
	~DBWork() =default;
	bool InitDB();
	bool InsertUser(UserInfo& user);
	bool UpdateUser(UserInfo& user);
	bool DeleteUser(UserInfo& user);
	bool GetUsers(std::vector<UserInfo>& users);
	//禁用一些构造函数，防止误用
	DBWork(const DBWork&) =delete;
	DBWork& operator=(const DBWork&) =delete;
private:
	CppSQLite3DB m_db;
};
