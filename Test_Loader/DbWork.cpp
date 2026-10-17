#include "dbwork.h"
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>   // FOLDERID_*
#include <string>
#include <memory>

#define ENCODING_CPP	1
#include <EncodingConverter.hpp>


#include <obfusheader.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId)
{
	PWSTR pszPath = nullptr;

	HRESULT hr = SHGetKnownFolderPath(
		folderId,      // 要获取的文件夹 ID
		0,             // KNOWN_FOLDER_FLAG
		nullptr,       // 用户令牌（nullptr = 当前用户）
		&pszPath
	);

	if (FAILED(hr) || !pszPath)
	{
		return L"";
	}

	// 自动释放 CoTaskMemAlloc 分配的内存
	std::wstring path(pszPath);
	CoTaskMemFree(pszPath);

	return path;
}





bool DBWork::InitDB() {
	std::wstring appData = GetKnownFolderPath(FOLDERID_RoamingAppData);

// 	std::wstring localAppData = GetKnownFolderPath(FOLDERID_LocalAppData);
// 	std::wstring documents = GetKnownFolderPath(FOLDERID_Documents);
// 
// 	std::wcout << L"Roaming AppData: " << appData << std::endl;
// 	std::wcout << L"Local AppData: " << localAppData << std::endl;
// 	std::wcout << L"Documents: " << documents << std::endl;
	std::wstring dbPath = appData + OBF(L"\\ssjj_gamedata.db");
	m_db.open(dbPath.c_str());
	std::string str = OBF("YxYSoft_db_key");
	m_db.setKey(str.c_str(), str.size());
		//检查表是否存在
	try {
		if (!m_db.tableExists("user")) {
			// 表  id  username  password detal  token   ntype  serverid
			// 创建表
			m_db.execDML(OBF("CREATE TABLE user (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT, password TEXT, detal TEXT, token TEXT, ntype INTEGER, serverid INTEGER NOT NULL DEFAULT 0)"));
		}
		else {
			// 老库迁移：若缺少 serverid 列则补加（每账号独立的 4399 服 ID，0=最近服）
			bool hasServerId = false;
			try {
				auto colQ = m_db.execQuery(OBF("PRAGMA table_info(user)"));
				while (!colQ.eof()) {
					std::string colName = colQ.getStringField(1);
					if (colName == "serverid") { hasServerId = true; break; }
					colQ.nextRow();
				}
			}
			catch (...) { /* PRAGMA 失败则走下面的 ALTER，吞掉 */ }
			if (!hasServerId) {
				m_db.execDML(OBF("ALTER TABLE user ADD COLUMN serverid INTEGER NOT NULL DEFAULT 0"));
			}
		}
	}
	catch (std::exception& e) {
		//std::wcout << e.what() << std::endl;
		MessageBoxA(NULL, e.what(),OBF( "Error"), MB_OK);
		return false;
	}
	return true;
}
// bool InitDB();
// bool InsertUser(UserInfo& user);
// bool UpdateUser(UserInfo& user);
// bool DeleteUser(UserInfo& user);
// bool GetUsers(std::vector<UserInfo>& users);

bool DBWork::InsertUser(UserInfo& user) {
		if (m_db.tableExists(OBF("user"))) {
			try {
				std::string sql = OBF("INSERT INTO user (username, password, detal, token, ntype, serverid) VALUES (?,?,?,?,?,?)");
				//m_db.execDML(sql, user.username, user.password, user.detal, user.token, user.ntype, user.m_serverId);
				auto stmt = m_db.compileStatement(sql.c_str());
				auto W2U8 = EncodingConverter::UnicodeToUTF8;
				stmt.bind(1, W2U8(user.m_username).c_str());
				stmt.bind(2, W2U8(user.m_password).c_str());
				stmt.bind(3, W2U8(user.m_detal).c_str());
				stmt.bind(4, W2U8(user.m_token).c_str());
				stmt.bind(5, user.m_nType);
				stmt.bind(6, user.m_serverId);
				stmt.execDML();
				return true;
			}
		catch (std::exception& e) {
			//std::wcout << e.what() << std::endl;
			MessageBoxA(NULL, e.what(),OBF( "Error"), MB_OK);
			return false;
		}
	}
	return false;
}
bool DBWork::UpdateUser(UserInfo& user) {
	// 基于id更新
	if (m_db.tableExists(OBF("user"))) {
		try {
			std::string sql = OBF("UPDATE user SET username=?, password=?, detal=?, token=?, ntype=?, serverid=? WHERE id=?");
			//m_db.execDML(sql, user.username, user.password, user.detal, user.token, user.ntype, user.m_serverId, user.id);
			auto stmt = m_db.compileStatement(sql.c_str());
			auto W2U8 = EncodingConverter::UnicodeToUTF8;
			stmt.bind(1, W2U8(user.m_username).c_str());
			stmt.bind(2, W2U8(user.m_password).c_str());
			stmt.bind(3, W2U8(user.m_detal).c_str());
			stmt.bind(4, W2U8(user.m_token).c_str());
			stmt.bind(5, user.m_nType);
			stmt.bind(6, user.m_serverId);
			stmt.bind(7, user.m_nIndex);
			stmt.execDML();
			return true;
		}
		catch (std::exception& e) {
			//std::wcout << e.what() << std::endl;
			MessageBoxA(NULL, e.what(), OBF("Error"), MB_OK);
			return false;
		}
	}
	return false;
}
bool DBWork::DeleteUser(UserInfo& user) {
	// 基于id删除
	if (m_db.tableExists(OBF("user"))) {
		try {
			std::string sql = OBF("DELETE FROM user WHERE id=?");
			//m_db.execDML(sql, user.id);
			auto stmt = m_db.compileStatement(sql.c_str());
			stmt.bind(1, user.m_nIndex);
			stmt.execDML();
			return true;
		}
		catch (std::exception& e) {
			//std::wcout << e.what() << std::endl;
			MessageBoxA(NULL, e.what(), OBF("Error"), MB_OK);
			return false;
		}
	}
	return false;
}
bool DBWork::GetUsers(std::vector<UserInfo>& users) {
	// 获取所有用户信息
	users.clear();
	if (m_db.tableExists(OBF("user")) ){
		try {
			// 先执行一次查询，获取总数
			std::string sql =OBF( "SELECT COUNT(*) FROM user");
			//m_db.execQuery(sql, users);
			int nCount = m_db.execScalar(sql.c_str());
			users.reserve(nCount);
			sql = OBF("SELECT  id, username, password, detal, token, ntype, serverid FROM user");
			//m_db.execQuery(sql, users);
			auto query = m_db.execQuery(sql.c_str());
			auto U82W = EncodingConverter::UTF8ToUnicode;
			while (!query.eof()) {
				UserInfo user;
				user.m_nIndex = query.getIntField(0);
				user.m_username = U82W (query.getStringField(1));
				user.m_password = U82W (query.getStringField(2));
				user.m_detal = U82W (query.getStringField(3));
				user.m_token = U82W (query.getStringField(4));
				user.m_nType = query.getIntField(5);
				user.m_serverId = query.getIntField(6);
				users.emplace_back( std:: move(user));
				query.nextRow();
			}

			return true;
		}
		catch (std::exception& e) {
			//std::wcout << e.what() << std::endl;
			MessageBoxA(NULL, e.what(),OBF( "Error"), MB_OK);
			return false;
		}
	}
	return false;
}

