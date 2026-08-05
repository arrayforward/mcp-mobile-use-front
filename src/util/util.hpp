/**
 * @file util.hpp
 * @brief 通用工具函数——Base64 编解码与字符串辅助（零第三方依赖）
 *
 * 功能：
 *   提供标准 Base64 编码/解码，以及 trim、shellQuote、startsWith 等字符串
 *   辅助函数，供命令构建、协议解析等模块复用。
 *
 * 开发思路：
 *   1. 为满足"零依赖全手写"约束，Base64 不引入第三方库，直接按 RFC 4648
 *      以 3 字节 -> 4 字符的分组方式手工实现。
 *   2. shellQuote 采用 POSIX 单引号包裹 + '\'' 转义方案，保证拼接到 adb shell
 *      命令行时不被 shell 展开，是命令注入防护的关键一环。
 *   3. base64Decode 返回 bool 表示合法性，遇到填充符或换行即停止，
 *      容忍 PEM 等多行格式。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

namespace util {

/**
 * @brief 标准 Base64 编码（RFC 4648，含 '=' 填充）
 * @param data 输入字节流
 * @param len 字节长度
 * @return Base64 字符串
 */
std::string base64Encode(const unsigned char* data, size_t len);
/** @brief 标准 Base64 编码（string 便捷重载） */
std::string base64Encode(const std::string& data);
/**
 * @brief 标准 Base64 解码
 * @param in Base64 字符串（遇 '=' / '\\n' / '\\r' 停止）
 * @param out 解码输出
 * @return 成功返回 true；含非法字符返回 false
 */
bool base64Decode(const std::string& in, std::string& out);

/**
 * @brief 去除首尾空白（空格/制表/换行/回车）
 * @param s 输入字符串
 * @return 去除空白后的子串
 */
std::string trim(const std::string& s);
/**
 * @brief POSIX shell 单引号转义，防止命令注入
 * @param s 原始字符串
 * @return 单引号包裹的安全字符串（内部 ' 转为 '\''）
 */
std::string shellQuote(const std::string& s);
/**
 * @brief 判断字符串是否以指定前缀开头
 * @param s 输入字符串
 * @param prefix 前缀
 */
bool startsWith(const std::string& s, const std::string& prefix);

}  // namespace util
