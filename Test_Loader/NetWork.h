#pragma once
#include <string>
class NetWork
{
public:
	NetWork() = default;
	~NetWork() = default;
	void LoginUser(const std::string& username, const std::string& password);

	bool IsLogin() const { return m_isLogin; }
	bool IsSuccess() const { return m_success; }
	const std::string& GetCookie() const { return m_cookie; }
	// 返回 账号密码
	const std::string& GetUsername() const { return m_username; }
	const std::string& GetPassword() const { return m_password; }
private:
	std::string m_username;
	std::string m_password;
	bool m_isLogin = false;
	bool m_success = false;
	std::string m_cookie;



};

