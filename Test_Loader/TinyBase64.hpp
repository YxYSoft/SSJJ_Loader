#pragma   once
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>


#include <obfusheader.h>
class TinyBase64 {
private:
	std::string chars;  // 字符表
	char pad;           // 填充字符
	int rev[256];       // 反向查找表

	void buildRev() {
		memset(rev, -1, sizeof(rev));
		for (int i = 0; i < 64; i++) {
			rev[(uint8_t)chars[i]] = i;
		}
	}

public:
	// 构造函数
	TinyBase64(const std::string& table = "") : pad('=') {
		if (table.empty()) {
			chars = OBF("IVXLdjEB9k+sHtQx5YbDiafuMncN7RrUeJCOgp/KhT2FmZPw1o6WqvAlzy4S803G");
		}
		else {
			if (table.size() != 64) {
				throw std::invalid_argument(OBF("字符表必须是64个字符"));
			}
			chars = table;
		}
		buildRev();
	}

	// 编码
	std::string encode(const std::vector<uint8_t>& data) {
		std::string out;
		size_t i = 0;
		size_t len = data.size();

		while (i < len) {
			uint32_t a = i < len ? data[i++] : 0;
			uint32_t b = i < len ? data[i++] : 0;
			uint32_t c = i < len ? data[i++] : 0;

			uint32_t triple = (a << 16) | (b << 8) | c;

			out.push_back(chars[(triple >> 18) & 0x3F]);
			out.push_back(chars[(triple >> 12) & 0x3F]);
			out.push_back(chars[(triple >> 6) & 0x3F]);
			out.push_back(chars[triple & 0x3F]);
		}

		// 填充
		size_t pad_len = len % 3;
		if (pad_len) {
			for (size_t j = pad_len; j < 3; j++) {
				out[out.size() - (3 - j)] = pad;
			}
		}

		return out;
	}

	// 编码字符串
	std::string encode(const std::string& str) {
		return encode(std::vector<uint8_t>(str.begin(), str.end()));
	}

	// 解码
	std::vector<uint8_t> decode(const std::string& str) {
		std::vector<uint8_t> out;
		std::string s = str;

		// 移除填充
		while (!s.empty() && s.back() == pad) {
			s.pop_back();
		}

		size_t i = 0;
		size_t len = s.size();

		while (i < len) {
			int a = i < len ? rev[(uint8_t)s[i++]] : 0;
			int b = i < len ? rev[(uint8_t)s[i++]] : 0;
			int c = i < len ? rev[(uint8_t)s[i++]] : 0;
			int d = i < len ? rev[(uint8_t)s[i++]] : 0;

			if (a < 0 || b < 0 || c < 0 || d < 0) {
				throw std::runtime_error(OBF("无效的Base64字符"));
			}

			uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;

			out.push_back((triple >> 16) & 0xFF);
			if (i <= len) out.push_back((triple >> 8) & 0xFF);
			if (i <= len) out.push_back(triple & 0xFF);
		}

		return out;
	}

	// 解码到字符串
	std::string decodeToString(const std::string& str) {
		auto data = decode(str);
		return std::string(data.begin(), data.end());
	}

	// 设置字符表
	void setTable(const std::string& table) {
		if (table.size() != 64) {
			throw std::invalid_argument(OBF("字符表必须是64个字符"));
		}
		chars = table;
		buildRev();
	}

	// 设置填充字符
	void setPadding(char p) {
		pad = p;
	}
};


