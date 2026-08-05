/**
 * @file util.cpp
 * @brief 通用工具函数实现——Base64 编解码与字符串辅助
 *
 * 功能：
 *   实现 util.hpp 声明的 base64Encode/base64Decode/trim/shellQuote/startsWith。
 *
 * 开发思路：
 *   1. Base64 编码按 3 字节一组拼成 24 位整数 n，再拆成 4 个 6 位索引查表，
 *      不足 3 字节的尾部用 '=' 填充。
 *   2. Base64 解码用流式位累积器（n + bits），每读入 6 位，
 *      攒够 8 位就吐出一个字节，天然容忍末尾不整除。
 *   3. 解码前先构建 256 项反查表（非法字符为 -1），O(1) 判定并取值。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "util.hpp"

namespace util {

// 标准 Base64 码表（RFC 4648，62/63 位为 '+' 与 '/'）
static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief 标准 Base64 编码
 * @param data 输入字节流
 * @param len 字节长度
 * @return 编码结果（长度向上取整为 4 的倍数）
 *
 * 伪代码：
 *   每 3 字节一组:
 *     n = b0<<16 | b1<<8 | b2（缺失字节视为 0）
 *     输出 table[n>>18], table[n>>12&63]
 *     第 3/4 字符若对应字节缺失则输出 '='
 */
std::string base64Encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);  // 预分配精确容量，避免反复扩容
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];
        out += kB64Table[(n >> 18) & 63];
        out += kB64Table[(n >> 12) & 63];
        out += (i + 1 < len) ? kB64Table[(n >> 6) & 63] : '=';  // 尾部不足补 '='
        out += (i + 2 < len) ? kB64Table[n & 63] : '=';
    }
    return out;
}

/** @brief 标准 Base64 编码（string 便捷重载） */
std::string base64Encode(const std::string& data) {
    return base64Encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

/**
 * @brief 标准 Base64 解码（流式位累积实现）
 * @param in Base64 字符串
 * @param out 解码输出（先清空）
 * @return 全部字符合法返回 true，否则 false
 *
 * 实现思路：维护 6 位移位寄存器 n 与有效位数 bits，
 * 每读入一个码表字符累积 6 位，bits >= 8 时取出最高 8 位作为一个输出字节；
 * 尾部不足 8 位的残余位自动丢弃，等价于忽略填充。
 *
 * 伪代码：
 *   建 256 反查表（非法=-1）
 *   for c in in:
 *     '=' / 换行 -> 停止
 *     非法 -> return false
 *     n = n<<6 | v; bits += 6
 *     bits>=8 -> bits-=8; out += (n>>bits)&0xFF
 */
bool base64Decode(const std::string& in, std::string& out) {
    int table[256];
    for (int i = 0; i < 256; ++i) table[i] = -1;
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kB64Table[i])] = i;

    out.clear();
    unsigned int n = 0;
    int bits = 0;
    for (char ch : in) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == '=' || c == '\n' || c == '\r') break;  // 填充或行尾即结束，容忍 PEM 多行格式
        if (table[c] < 0) return false;                 // 非法字符直接判失败
        n = (n << 6) | static_cast<unsigned int>(table[c]);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((n >> bits) & 0xFF);
        }
    }
    return true;
}

/**
 * @brief 去除首尾空白
 * @param s 输入字符串
 * @return 去除 " \\t\\r\\n" 后的子串；全空白返回空串
 */
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/**
 * @brief POSIX shell 单引号转义
 * @param s 原始字符串
 * @return 形如 'abc'\''def' 的安全字符串
 *
 * 实现思路：整体用单引号包裹（单引号内 shell 不做任何展开），
 * 内部的单引号用 '\''（结束引号-转义引号-重开引号）表达，
 * 这是 POSIX 下唯一需要在单引号语境中特殊处理的字符。
 */
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += '\'';
    return out;
}

/**
 * @brief 前缀匹配判断
 * @param s 输入字符串
 * @param prefix 前缀
 * @return s 以 prefix 开头返回 true
 */
bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace util
