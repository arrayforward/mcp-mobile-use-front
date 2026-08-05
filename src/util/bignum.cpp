/**
 * @file bignum.cpp
 * @brief 无符号大整数运算实现——比较/加减乘/取模/模幂
 *
 * 功能：
 *   实现 bignum.hpp 声明的全部大数运算，供 RSA 验签（bigModPow）使用。
 *
 * 开发思路：
 *   1. 所有函数输出均为规范化形式（最高 limb 非零，零为 {0}），
 *      保证 bigCmp 语义正确、下游序列化长度可预期。
 *   2. 进位/借位统一用 64 位临时变量承接 32 位运算的溢出部分，
 *      逻辑清晰且不易出错。
 *   3. bigMod 不实现通用除法，改用按位长除法：从被除数最高位起逐位
 *      下移，维护不变量 r < m，每次 r = r*2+bit 后至多减一次 m，
 *      用简单换正确（RSA 验签只调少量次数，性能可接受）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "bignum.hpp"

#include <algorithm>
#include <cstring>

namespace util {

/**
 * @brief 大端字节流转大整数
 * @param data 大端字节流
 * @param len 字节长度
 * @return 规范化 Big
 *
 * 实现思路：从字节流尾部向前每次取至多 4 字节（一个 chunk），
 * 组内按大端拼成一个 uint32 limb，依次 push_back 得到小端 limbs；
 * 最后剥掉高位零 limb 完成规范化。
 *
 * 重要说明：早期版本用 `i -= 4` 步进，当 len 不是 4 的倍数时
 * size_t 无符号下溢（i 从 1/2/3 直接绕到巨大值），导致越界读取。
 * 现改为显式 chunk = min(i, 4) 的方式处理尾部不足 4 字节的部分，
 * 彻底规避下溢。
 *
 * 伪代码：
 *   i = len
 *   while i > 0:
 *     chunk = min(i, 4); start = i - chunk
 *     limb = data[start..i) 按大端拼装
 *     push_back(limb); i = start
 *   剥掉高位零 limb；空则压入 {0}
 */
Big bigFromBytes(const unsigned char* data, size_t len) {
    Big out;
    size_t i = len;
    while (i > 0) {
        size_t chunk = i >= 4 ? 4 : i;  // 每次处理 4 字节（不足 4 字节取剩余）
        size_t start = i - chunk;
        uint32_t limb = 0;
        for (size_t j = start; j < i; ++j) limb = (limb << 8) | data[j];  // chunk 内大端拼装
        out.push_back(limb);
        i = start;
    }
    while (out.size() > 1 && out.back() == 0) out.pop_back();  // 规范化：剥高位零
    if (out.empty()) out.push_back(0);                         // 零的规范表示为 {0}
    return out;
}

/** @brief 大端字节流转大整数（string 便捷重载） */
Big bigFromBytes(const std::string& bytes) {
    return bigFromBytes(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

/**
 * @brief uint64 转大整数
 * @param v 无符号 64 位整数
 * @return 规范化 Big
 */
Big bigFromUint(uint64_t v) {
    Big out;
    while (v) {
        out.push_back(static_cast<uint32_t>(v & 0xFFFFFFFF));
        v >>= 32;
    }
    if (out.empty()) out.push_back(0);
    return out;
}

/**
 * @brief 大数比较
 * @param a 操作数 a（须为规范化形式）
 * @param b 操作数 b（须为规范化形式）
 * @return a<b 返回 -1，a==b 返回 0，a>b 返回 1
 *
 * 实现思路：规范化保证下先比 limb 数（数大者大），
 * 同长度则从最高 limb 向最低逐位比较。
 */
int bigCmp(const Big& a, const Big& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i > 0; --i) {
        if (a[i - 1] != b[i - 1]) return a[i - 1] < b[i - 1] ? -1 : 1;
    }
    return 0;
}

/**
 * @brief 大数加法
 * @param a 操作数 a
 * @param b 操作数 b
 * @return a + b（规范化）
 *
 * 伪代码：
 *   carry = 0
 *   for i in 0..max(len):
 *     sum = carry + a[i]? + b[i]?（越界按 0）
 *     out[i] = sum 低 32 位; carry = sum 高 32 位
 *   carry 非零则追加一个 limb
 */
Big bigAdd(const Big& a, const Big& b) {
    size_t n = std::max(a.size(), b.size());
    Big out(n, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t sum = carry;
        if (i < a.size()) sum += a[i];
        if (i < b.size()) sum += b[i];
        out[i] = static_cast<uint32_t>(sum & 0xFFFFFFFF);
        carry = sum >> 32;  // 两 32 位数加进位最多 33 位，uint64 承接不会溢出
    }
    if (carry) out.push_back(static_cast<uint32_t>(carry));  // 最高位进位落为新 limb
    return out;
}

/**
 * @brief 大数减法（要求 a >= b）
 * @param a 被减数
 * @param b 减数
 * @return a - b（规范化）
 *
 * 伪代码：
 *   borrow = 0
 *   for i in 0..max(len):
 *     diff = a[i]? - b[i]? - borrow（用 int64 承接负值）
 *     diff < 0 -> diff += 2^32; borrow = 1，否则 borrow = 0
 *     out[i] = diff 低 32 位
 *   剥掉高位零 limb
 */
Big bigSub(const Big& a, const Big& b) {
    size_t n = std::max(a.size(), b.size());
    Big out(n, 0);
    int64_t borrow = 0;
    for (size_t i = 0; i < n; ++i) {
        int64_t diff = static_cast<int64_t>(i < a.size() ? a[i] : 0) -
                       static_cast<int64_t>(i < b.size() ? b[i] : 0) - borrow;
        if (diff < 0) {
            diff += 0x100000000LL;  // 向高位借 1：加回 2^32
            borrow = 1;
        } else {
            borrow = 0;
        }
        out[i] = static_cast<uint32_t>(diff);
    }
    while (out.size() > 1 && out.back() == 0) out.pop_back();
    return out;
}

/**
 * @brief 大数乘法（教科书竖式）
 * @param a 操作数 a
 * @param b 操作数 b
 * @return a * b（规范化）
 *
 * 伪代码：
 *   任一操作数为 0 -> 返回 {0}
 *   out 长度 = a.len + b.len
 *   for i in a:
 *     carry = 0
 *     for j in b:
 *       cur = out[i+j] + a[i]*b[j] + carry
 *       out[i+j] = 低 32 位; carry = 高 32 位
 *     内层结束后把残余 carry 继续向更高 limb 传播
 *   剥掉高位零 limb
 */
Big bigMul(const Big& a, const Big& b) {
    if ((a.size() == 1 && a[0] == 0) || (b.size() == 1 && b[0] == 0))
        return Big{0};
    Big out(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        uint64_t ai = a[i];
        for (size_t j = 0; j < b.size(); ++j) {
            // uint64 足够承接：out[i+j](<=2^32-1) + ai*b[j](<= (2^32-1)^2) + carry(<=2^32-1)
            uint64_t cur = out[i + j] + ai * b[j] + carry;
            out[i + j] = static_cast<uint32_t>(cur & 0xFFFFFFFF);
            carry = cur >> 32;
        }
        size_t k = i + b.size();
        while (carry) {
            // 内层结束后残余进位继续向高位传播；与已有值相加可能再产生进位
            uint64_t cur = out[k] + carry;
            out[k] = static_cast<uint32_t>(cur & 0xFFFFFFFF);
            carry = cur >> 32;
            ++k;
        }
    }
    while (out.size() > 1 && out.back() == 0) out.pop_back();
    return out;
}

namespace {

/**
 * @brief r = r * 2 + bit（按位长除法的核心辅助）
 * @param r 余数（原地修改）
 * @param bit 新移入的最低位（0 或 1）
 *
 * 实现思路：等价于整体左移 1 位并把 bit 放到最低位；
 * 逐 limb 处理：(r[i]<<1)|carry，carry 取高 32 位。
 * bigMod 中维护不变量 r < m，故移位后 r < 2*m，一次减法即可恢复不变量。
 */
// r = r * 2 + bit（r < 2*m 不变量保证，一次减法足够）
void shiftLeftOneAdd(Big& r, uint32_t bit) {
    uint64_t carry = bit;
    for (size_t i = 0; i < r.size(); ++i) {
        uint64_t cur = (static_cast<uint64_t>(r[i]) << 1) | carry;
        r[i] = static_cast<uint32_t>(cur & 0xFFFFFFFF);
        carry = cur >> 32;
    }
    if (carry) r.push_back(static_cast<uint32_t>(carry));
}

}  // namespace

/**
 * @brief 大数取模（按位长除法）
 * @param a 被除数
 * @param m 模数（须非零）
 * @return a mod m
 *
 * 伪代码：
 *   a < m -> 直接返回 a
 *   r = 0
 *   从 a 的最高 limb 最高位开始逐位:
 *     r = r*2 + bit
 *     r >= m -> r -= m（不变量 r < m，故至多减一次）
 *   return r
 */
Big bigMod(const Big& a, const Big& m) {
    if (bigCmp(a, m) < 0) return a;
    // 按位长除法求余：不变量 r < m，每次 r = r*2 + bit 后至多减一次 m
    Big r{0};
    for (size_t i = a.size(); i > 0; --i) {
        uint32_t limb = a[i - 1];
        for (int bit = 31; bit >= 0; --bit) {
            shiftLeftOneAdd(r, (limb >> bit) & 1);
            if (bigCmp(r, m) >= 0) r = bigSub(r, m);
        }
    }
    return r;
}

/**
 * @brief 模乘 (a * b) mod m
 * @param a 操作数 a
 * @param b 操作数 b
 * @param m 模数
 */
Big bigModMul(const Big& a, const Big& b, const Big& m) {
    return bigMod(bigMul(a, b), m);
}

/**
 * @brief 模幂 (base ^ exp) mod m（二进制平方-乘，低位到高位）
 * @param base 底数
 * @param exp 指数（小端 limbs）
 * @param m 模数
 * @return base^exp mod m
 *
 * 伪代码：
 *   result = 1; b = base mod m
 *   按 exp 的每个 limb 的每个 bit（低位到高位）:
 *     bit 为 1 -> result = result * b mod m
 *     b = b^2 mod m（最后一位后无需再平方，见下）
 *   return result
 */
Big bigModPow(const Big& base, const Big& exp, const Big& m) {
    Big result{1};
    Big b = bigMod(base, m);
    for (size_t i = 0; i < exp.size(); ++i) {
        uint32_t e = exp[i];
        for (int bit = 0; bit < 32; ++bit) {
            if (e & 1) result = bigModMul(result, b, m);
            e >>= 1;
            // 优化：已是最高有效位时无需再平方 b，省一次模乘
            if (i + 1 < exp.size() || e) b = bigModMul(b, b, m);
        }
    }
    return result;
}

}  // namespace util
