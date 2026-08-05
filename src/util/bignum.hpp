/**
 * @file bignum.hpp
 * @brief 无符号大整数运算库——RSA 验签的底层算术支撑（零第三方依赖）
 *
 * 功能：
 *   以 std::vector<uint32_t> 小端 limbs（base 2^32）表示无符号大整数，
 *   提供比较、加减乘、取模、模乘、模幂等运算。
 *
 * 开发思路：
 *   1. 为满足"零依赖全手写"约束（不引入 GMP/OpenSSL BN），手写最简大数库，
 *      仅覆盖 RSA 验签所需的运算集合，不实现除法（取模用按位长除法替代）。
 *   2. 表示约定：limbs 小端序（d[0] 为最低 32 位），所有输出保持
 *      "最高非零 limb 不为 0" 的规范化形式（零表示为 {0}），使 bigCmp
 *      可先比长度再逐 limb 比较。
 *   3. 乘法/加法用 uint64_t 作进位暂存，减法用 int64_t 作借位暂存，
 *      避免手工处理 32 位溢出。
 *   4. 模幂采用从低位到高位的二进制平方-乘算法；RSA 公钥 e 通常很小
 *      （如 65537，17 位），性能足够。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace util {

// 大整数：uint32 小端序 limbs（base 2^32），无符号
using Big = std::vector<uint32_t>;

/**
 * @brief 大端字节流转大整数
 * @param data 大端字节流
 * @param len 字节长度
 * @return 规范化 Big（零表示为 {0}）
 */
Big bigFromBytes(const unsigned char* data, size_t len);
/** @brief 大端字节流转大整数（string 便捷重载） */
Big bigFromBytes(const std::string& bytes);
/**
 * @brief uint64 转大整数
 * @param v 无符号 64 位整数
 */
Big bigFromUint(uint64_t v);

/**
 * @brief 大数比较
 * @return a<b 返回 -1，a==b 返回 0，a>b 返回 1
 */
int bigCmp(const Big& a, const Big& b);
/** @brief 大数加法 a + b */
Big bigAdd(const Big& a, const Big& b);
/** @brief 大数减法 a - b（要求 a >= b，否则行为未定义） */
Big bigSub(const Big& a, const Big& b);  // 要求 a >= b
/** @brief 大数乘法 a * b（教科书 O(n*m) 竖式） */
Big bigMul(const Big& a, const Big& b);
/** @brief 大数取模 a mod m（按位长除法） */
Big bigMod(const Big& a, const Big& m);  // a mod m
/** @brief 模乘 (a * b) mod m */
Big bigModMul(const Big& a, const Big& b, const Big& m);
/** @brief 模幂 (base ^ exp) mod m（二进制平方-乘） */
Big bigModPow(const Big& base, const Big& exp, const Big& m);

}  // namespace util
