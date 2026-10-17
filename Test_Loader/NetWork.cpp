#include "NetWork.h"
#include <hv/TcpClient.h>
#include <hv/HttpMessage.h>
#include <hv/HttpClient.h>

#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <vector>
#include <bcrypt.h>
#include <EncodingConverter.hpp>
#include "TinyBase64.hpp"

#pragma comment(lib, "crypt32.lib")

#include <obfusheader.h>

#define  WRAPPER_MARCO(x)	OBF(x)



#define MD5_HASH_SIZE 16
#define SALT_SIZE 8
#define ITERATION_COUNT 3
#define KEY_SIZE 32
#define IV_SIZE 16

BOOL Kdf_MakeKey(const char* srcKey, BYTE key[KEY_SIZE], BYTE iv[IV_SIZE], DWORD salt[2])
{
	if (!srcKey || !key || !iv || !salt) {
		return FALSE;
	}

	BYTE allData[MD5_HASH_SIZE * ITERATION_COUNT] = { 0 };
	BYTE locData[MD5_HASH_SIZE] = { 0 };
	size_t srcKeyLen = strlen(srcKey);

	if (srcKeyLen == 0) {
		return FALSE;
	}

	// 使用加密安全的随机数生成器
	if (BCryptGenRandom(NULL, (PUCHAR)salt, SALT_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
		srand((unsigned int)GetTickCount64());
		salt[0] = MAKELONG(rand() & 0xFFFF, rand() & 0xFFFF);
		salt[1] = MAKELONG(rand() & 0xFFFF, rand() & 0xFFFF);
	}

	// OpenSSL 3.0: 获取 MD5 算法
	EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
	const EVP_MD* md = EVP_MD_fetch(NULL, "MD5", NULL);

	if (!mdctx || !md) {
		EVP_MD_CTX_free(mdctx);
		return FALSE;
	}

	for (int i = 0; i < ITERATION_COUNT; i++) {
		if (EVP_DigestInit_ex(mdctx, md, NULL) != 1) {
			break;
		}

		if (i > 0) {
			EVP_DigestUpdate(mdctx, locData, MD5_HASH_SIZE);
		}

		EVP_DigestUpdate(mdctx, srcKey, srcKeyLen);
		EVP_DigestUpdate(mdctx, salt, SALT_SIZE);

		unsigned int len = 0;
		EVP_DigestFinal_ex(mdctx, locData, &len);

		memcpy(allData + (i * MD5_HASH_SIZE), locData, MD5_HASH_SIZE);
	}

	EVP_MD_CTX_free(mdctx);
	EVP_MD_free( (evp_md_st*)md);

	memcpy(key, allData, KEY_SIZE);
	memcpy(iv, allData + KEY_SIZE, IV_SIZE);

	return TRUE;
}

// 初始化 OpenSSL（在程序启动时调用一次）
BOOL InitOpenSSL()
{
	// 加载默认提供者（包含 MD5）
	OSSL_PROVIDER* prov = OSSL_PROVIDER_load(NULL, OBF("default"));
	if (!prov) {
		// 尝试加载 legacy 提供者（某些系统可能需要）
		prov = OSSL_PROVIDER_load(NULL, OBF("legacy"));
		if (!prov) {
			return FALSE;
		}
	}
	return TRUE;
}
// 
// BOOL Kdf_MakeKey(char* srcKey, byte key[64], byte iv[16], DWORD salt[2])
// {
// 	BYTE AllData[48] = { 0 };
// 	BYTE LocData[16] = { 0 };
// 
// 	MD5_CTX ctx;
// 	int srcKeyLen = strlen(srcKey);
// 
// 	//
// 	srand(GetTickCount64());
// 	salt[0] = MAKELONG(rand() & 0xFFFF, rand() & 0xFFFF);
// 	salt[1] = MAKELONG(rand() & 0xFFFF, rand() & 0xFFFF);
// 
// 
// 	for (int i = 0; i < 3; i++)
// 	{
// 		MD5_Init(&ctx);
// 		if (i != 0)
// 		{
// 			//提交 上次的 数据
// 			MD5_Update(&ctx, LocData, 16);
// 		}
// 		MD5_Update(&ctx, srcKey, srcKeyLen);
// 		MD5_Update(&ctx, salt, 8);
// 		MD5_Final(LocData, &ctx);
// 		//将数据 拷贝到 all
// 		memcpy(AllData + i * 16, LocData, 16);
// 	}
// 	//
// 	memcpy(key, AllData, 32);
// 	memcpy(iv, AllData + 32, 16);
// 	return TRUE;
// }
std::string Make_SSJJ_Pwd(std::string srcPwd)
{

	std::string bResult;

	BYTE* inData = (BYTE*)srcPwd.c_str();
	size_t inLen = srcPwd.size();
	bResult.resize(inLen + 16 + 16);
	BYTE* outData = (BYTE*)&bResult[0];
	size_t outLen = (inLen + 16 + 16);
	BYTE key[64] = { 0 };
	BYTE iv[64] = { 0 };
	BOOL bEnc = TRUE;
	BUF_MEM* bptr = NULL;
	BIO* bio, * b64;
	BIO* cip = BIO_new(BIO_f_cipher());
	DWORD salt[2] = { 0 };
	char szKEYBuffer[] = { 108,122,89,87,53,113,97,88,86,113,97,0,0 };
	Kdf_MakeKey(szKEYBuffer, key, iv, salt);

	DWORD dwMagic[2] = { htonl(1398893684),htonl(1701076831) };

	outData[0] = 'S';
	outData[1] = 'a';
	outData[2] = 'l';
	outData[3] = 't';
	outData[4] = 'e';
	outData[5] = 'd';
	outData[6] = '_';
	outData[7] = '_';
	memcpy(outData + 8, salt, 8);
	BIO_set_cipher(cip, EVP_get_cipherbyname(OBF("AES-256-CBC")), key, iv, TRUE);
	bio = BIO_new(BIO_s_mem());

// 	魔法标志8字节  |   Salt 8 字节  |   AES加密后的数据
// 		Salted__         XXXXXXXX

	do
	{
		//AES_CRYPT
		bio = BIO_push(cip, bio);
		{
			BIO_write(bio, inData, inLen);
			BIO_flush(bio);
			BIO_get_mem_ptr(bio, &bptr);
			if (bptr->length > outLen)
			{
				outLen = bptr->length;
				break;
			}
			outLen = bptr->length;
			memcpy(outData + 16, bptr->data, bptr->length);
		}
		BIO_free_all(bio);
		bio = NULL;
		//Base64
		std::string tmp;
		b64 = BIO_new(BIO_f_base64());
		BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

		bio = BIO_new(BIO_s_mem());
		bio = BIO_push(b64, bio);
		BIO_write(bio, outData, outLen + 16);
		BIO_flush(bio);

		BIO_get_mem_ptr(bio, &bptr);

		tmp.resize(1 + bptr->length);
		char* b64Buffer = &tmp[0];


		memcpy(b64Buffer, bptr->data, bptr->length);
		b64Buffer[bptr->length] = 0;

		tmp.resize(bptr->length);

		BIO_free_all(bio);



		bResult = std::move(tmp);
	} while (FALSE);

	return bResult;
}



class CCookieMgr
{
private:
	void Stringsplit(const std::string& str, const std::string& split, std::vector<std::string>& res)
	{
		char* strc = new char[str.size() + 1];
		strcpy(strc, str.c_str());   // 将str拷贝到 char类型的strc中
		char* temp = strtok(strc, split.c_str());
		while (temp != NULL)
		{
			res.emplace_back(std::string(temp));
			temp = strtok(NULL, split.c_str());	// 下一个被分割的串
		}
		delete[] strc;
	}
public:
	CCookieMgr() {};
	~CCookieMgr() {};
	void parse(std::string strCookie)
	{
		HttpCookie ck;
		std::vector<std::string> resCk;
		Stringsplit(strCookie, "; ", resCk);
		for (auto itm : resCk)
		{
			ck.parse(itm.c_str());
			m_AllCookie[ck.name] = ck.value;
			ck.reset();
		}

	}
	void Update(http_cookies ck)
	{
		for (auto itm : ck)
		{
			m_AllCookie[itm.name] = itm.value;
		}
	}
	void Update(std::string _name, std::string _value)
	{
		m_AllCookie[_name] = _value;
	}
	std::string Dump()
	{
		std::string str;
		for (auto itm : m_AllCookie)
		{
			str += itm.first;
			str += "=";
			str += itm.second;
			str += "; ";
		}
		return str;
	}
	std::string DumpToken()
	{
		std::string str;
		nlohmann::json m_json(nlohmann::json::object());
		for (auto itm : m_AllCookie)
		{
			m_json [itm.first] = itm.second;
		}
		str = m_json.dump();
		TinyBase64 custom;
		custom.setPadding('$');  // 自定义填充字符
		auto res = custom.encode(str);
		return res;
	}
	void Lookup(std::function <void(const std::string, const std::string)> vFn)
	{
		for (auto& itm : m_AllCookie)
		{
			vFn(itm.first, itm.second);
		}
	}
private:
	std::map<std::string, std::string> m_AllCookie;

};

static unsigned char ToHex(unsigned char x)
{
	return  x > 9 ? x + 55 : x + 48;
}

static unsigned char FromHex(unsigned char x)
{
	unsigned char y = 0;
	if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
	else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
	else if (x >= '0' && x <= '9') y = x - '0';
	else assert(0);
	return y;
}
static std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			strTemp += str[i];
		else if (str[i] == ' ')
			strTemp += "+";
		else
		{
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] % 16);
		}
	}
	return strTemp;
}

static std::string UrlDecode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		if (str[i] == '+') strTemp += ' ';
		else if (str[i] == '%')
		{
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}
	return strTemp;
}
void  NetWork::LoginUser(const std::string& username, const std::string& password) {
	// TODO: implement login logic here

		//CHttpClient loc_http;

	std::string strUrl = OBF("https://ptlogin.4399.com/ptlogin/login.do?v=1");

	std::string data =
		OBF("loginFrom=uframe&postLoginHandler=default&layoutSelfAdapting=true&externalLogin=qq&displayMode=undefined&layout=vertical&appId=my&gameId=my&css=%2F%2Fs1.img4399.com%2Fbase%2Fcss%2Fptlogin.css%3Fc5ff08d&redirectUrl=&sessionId=&mainDivId=popup_login_div&includeFcmInfo=false&level=2&regLevel=2&userNameLabel=4399%E7%94%A8%E6%88%B7%E5%90%8D&userNameTip=%E8%AF%B7%E8%BE%93%E5%85%A54399%E7%94%A8%E6%88%B7%E5%90%8D&welcomeTip=%E6%AC%A2%E8%BF%8E%E5%9B%9E%E5%88%B04399&sec=1&password=")
		+ UrlEncode(Make_SSJJ_Pwd(password))
		+ OBF("&username=")
		+ username
		;

	CCookieMgr myCookie;
	myCookie.parse("");


	HttpRequestPtr req(new HttpRequest);
	req->method = HTTP_POST;
	req->SetUrl(strUrl.c_str());
	req->ParseUrl();
	req->headers[WRAPPER_MARCO(("Host"))] = WRAPPER_MARCO(R"(ptlogin.4399.com)");
	req->headers[WRAPPER_MARCO(("Connection"))] = WRAPPER_MARCO(R"(keep-alive)");
	req->headers[WRAPPER_MARCO(("Cache-Control"))] = WRAPPER_MARCO(R"(max-age=0)");
	req->headers[WRAPPER_MARCO(("Origin"))] = WRAPPER_MARCO(R"(https://ptlogin.4399.com)");
	req->headers[WRAPPER_MARCO(("Content-Type"))] = WRAPPER_MARCO(R"(application/x-www-form-urlencoded)");
	req->headers[WRAPPER_MARCO(("Upgrade-Insecure-Requests"))] = WRAPPER_MARCO(R"(1)");
	req->headers[WRAPPER_MARCO(("User-Agent"))] = WRAPPER_MARCO(R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36 Edg/147.0.0.0)");
	req->headers[WRAPPER_MARCO(("Accept"))] = WRAPPER_MARCO(R"(text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7)");
	req->headers[WRAPPER_MARCO(("Sec-Fetch-Site"))] = WRAPPER_MARCO(R"(same-origin)");
	req->headers[WRAPPER_MARCO(("Sec-Fetch-Mode"))] = WRAPPER_MARCO(R"(navigate)");
	req->headers[WRAPPER_MARCO(("Sec-Fetch-User"))] = WRAPPER_MARCO(R"(?1)");
	req->headers[WRAPPER_MARCO(("Sec-Fetch-Dest"))] = WRAPPER_MARCO(R"(iframe)");
	req->headers[WRAPPER_MARCO(("Referer"))] = WRAPPER_MARCO(R"(https://ptlogin.4399.com/ptlogin/loginFrame.do?postLoginHandler=default&redirectUrl=&displayMode=undefined&css=%2F%2Fs1.img4399.com%2Fwebgame%2Fssjj%2Fnews%2Fcss%2Fptlogin.css%3F1a83ccd&bizId=1202000468&appId=ssjj&gameId=news&username=guiguixiaohao002&externalLogin=qq&password=&mainDivId=popup_login_div&autoLogin=false&includeFcmInfo=false&qrLogin=true&userNameLabel=4399%E7%94%A8%E6%88%B7%E5%90%8D&userNameTip=%E8%AF%B7%E8%BE%93%E5%85%A54399%E7%94%A8%E6%88%B7%E5%90%8D&welcomeTip=%E6%AC%A2%E8%BF%8E%E5%9B%9E%E5%88%B04399&level=8&regLevel=8&iframeId=popup_login_frame&v=1777626055645)");
	req->headers[WRAPPER_MARCO(("Accept-Language"))] = WRAPPER_MARCO(R"(zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6)");

	req->AllowRedirect(false);

	req->body = data;

	req->timeout = 1000;
	HttpResponsePtr res(new HttpResponse);
	http_client_send(req.get(), res.get());

	myCookie.Update(res->cookies);

	std::string cookie;
	cookie = myCookie.Dump();
	if (cookie.find(username) != std::string::npos) {
		m_isLogin = true;
		m_success = true;
		m_username = username;
		m_password = password;
		m_cookie = myCookie.DumpToken();
	}
	else {
		m_isLogin = false;
		m_success = false;
		m_username = "";
		m_password = "";
		m_cookie = "";
	}

	return ;
}





// 提交个后台线程池，异步处理 登录成功之后存入数据库


