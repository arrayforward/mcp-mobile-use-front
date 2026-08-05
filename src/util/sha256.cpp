/**
 * @file sha256.cpp
 * @brief SHA-256 / HMAC-SHA-256 / Base64URL 实现（FIPS 180-4 / RFC 2104 / RFC 4648）
 *
 * 功能：
 *   实现 sha256.hpp 声明的全部接口。内部以匿名命名空间的 Sha256Ctx 提供
 *   流式 update/final，一次性接口仅为其封装。
 *
 * 开发思路：
 *   1. SHA-256 压缩函数按标准实现：64 轮、消息扩展 w[64]、8 个工作变量，
 *      轮常数 kK 取自前 64 个素数立方根的小数部分。
 *   2. 流式接口用 64 字节缓冲区攒块，满块即压缩，final 时做
 *      0x80 填充 + 8 字节大端位长度（Merkle-Damgard 强化）。
 *   3. HMAC 复用 Sha256Ctx 做内外两次哈希，避免重复实现。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "sha256.hpp"

#include <cstring>

namespace util {

namespace {

// SHA-256 轮常数 K：前 64 个素数立方根小数部分的前 32 位（FIPS 180-4 §4.2.2）
const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

/** @brief 32 位循环右移（SHA-256 基础位运算） */
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

/**
 * @struct Sha256Ctx
 * @brief SHA-256 流式哈希上下文（内部实现）
 *
 * 开发思路：持有 8 个链变量 h、64 字节块缓冲区 buf 与总字节数 totalLen；
 * update 攒满一块即调用 block 压缩，final 负责填充与输出。
 * 供一次性 sha256() 与 HMAC 内外两轮哈希复用。
 *
 * @author hubin
 * @date 2026-08-05
 */
struct Sha256Ctx {
    // 初始链变量 H(0)：前 8 个素数平方根小数部分的前 32 位
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64];
    size_t bufLen = 0;
    uint64_t totalLen = 0;

    /**
     * @brief 压缩函数：处理一个 64 字节块
     * @param p 指向 64 字节块
     *
     * 伪代码（FIPS 180-4 §6.2.2）：
     *   1. 16 个大端字装入 w[0..15]
     *   2. 消息扩展 w[16..63]（σ0/σ1 混合）
     *   3. 64 轮迭代：T1 = h + Σ1(e) + Ch(e,f,g) + K[i] + w[i]
     *                 T2 = Σ0(a) + Maj(a,b,c)
     *      工作变量循环移位，a = T1+T2，e = d+T1
     *   4. 8 个工作变量加回链变量
     */
    void block(const unsigned char* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);  // 大端装入
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;  // 模 2^32 加法天然回绕
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6],
                 hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);      // Σ1
            uint32_t ch = (e & f) ^ (~e & g);                          // 选择函数
            uint32_t t1 = hh + s1 + ch + kK[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);      // Σ0
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);                // 多数函数
            uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;  // 工作变量循环移位
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    /**
     * @brief 流式输入数据
     * @param data 输入字节流
     * @param len 字节长度
     *
     * 实现思路：先填满当前缓冲区再压缩整块，循环直至消费完输入；
     * 未满一块的数据留在缓冲区等待下次 update 或 final。
     */
    void update(const unsigned char* data, size_t len) {
        totalLen += len;
        while (len > 0) {
            size_t take = 64 - bufLen;
            if (take > len) take = len;
            memcpy(buf + bufLen, data, take);
            bufLen += take;
            data += take;
            len -= take;
            if (bufLen == 64) {
                block(buf);   // 满块立即压缩
                bufLen = 0;
            }
        }
    }

    /**
     * @brief 结束并输出 32 字节摘要
     * @param out 输出缓冲（32 字节）
     *
     * 实现思路（Merkle-Damgard 填充）：追加 0x80 后补 0，
     * 直到 bufLen == 56，再追加 8 字节大端位长度，凑成整块；
     * 最后将 8 个链变量按大端输出。
     *
     * 伪代码：
     *   bitLen = totalLen * 8（先记下，填充会改变 totalLen）
     *   update(0x80); update(0x00...) 直到 bufLen==56
     *   update(8 字节大端 bitLen)
     *   h[i] 大端输出到 out
     */
    void final(unsigned char out[32]) {
        uint64_t bitLen = totalLen * 8;  // 须先保存：后续 update 会累加 totalLen
        unsigned char pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (bufLen != 56) update(&pad, 1);  // 补 0 至距块尾 8 字节处
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<unsigned char>(bitLen >> (56 - i * 8));
        update(lenBytes, 8);  // 追加大端位长度后恰好凑满一块并触发压缩
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<unsigned char>(h[i] >> 24);
            out[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16);
            out[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8);
            out[i * 4 + 3] = static_cast<unsigned char>(h[i]);
        }
    }
};

}  // namespace

/**
 * @brief 一次性 SHA-256（流式接口的便捷封装）
 * @param data 输入字节流
 * @param len 字节长度
 * @param out 输出 32 字节摘要
 */
void sha256(const unsigned char* data, size_t len, unsigned char out[32]) {
    Sha256Ctx ctx;
    ctx.update(data, len);
    ctx.final(out);
}

/**
 * @brief SHA-256 十六进制形式
 * @param input 输入数据
 * @return 64 字符小写十六进制字符串
 */
std::string sha256Hex(const std::string& input) {
    unsigned char digest[32];
    sha256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 32; ++i) {
        out += kHex[digest[i] >> 4];
        out += kHex[digest[i] & 0xF];
    }
    return out;
}

/**
 * @brief HMAC-SHA-256（RFC 2104）
 * @param key 密钥（任意长度）
 * @param message 消息
 * @return 32 字节二进制 MAC
 *
 * 伪代码：
 *   密钥 > 64 字节 -> keyBlock = SHA256(key)，否则拷贝后补 0 到 64 字节
 *   ipad = keyBlock ^ 0x36*64; opad = keyBlock ^ 0x5c*64
 *   return SHA256(opad || SHA256(ipad || message))
 */
std::string hmacSha256(const std::string& key, const std::string& message) {
    unsigned char keyBlock[64] = {0};
    std::string k = key;
    if (k.size() > 64) {
        // 密钥长于块长：按 RFC 2104 先取其摘要作为实际密钥
        unsigned char digest[32];
        sha256(reinterpret_cast<const unsigned char*>(k.data()), k.size(), digest);
        memcpy(keyBlock, digest, 32);
    } else {
        memcpy(keyBlock, k.data(), k.size());
    }

    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = keyBlock[i] ^ 0x36;
        opad[i] = keyBlock[i] ^ 0x5c;
    }

    Sha256Ctx inner;
    inner.update(ipad, 64);
    inner.update(reinterpret_cast<const unsigned char*>(message.data()), message.size());
    unsigned char innerOut[32];
    inner.final(innerOut);

    Sha256Ctx outer;
    outer.update(opad, 64);
    outer.update(innerOut, 32);
    unsigned char out[32];
    outer.final(out);
    return std::string(reinterpret_cast<char*>(out), 32);
}

/**
 * @brief Base64URL 编码（无填充）
 * @param data 输入字节流
 * @param len 字节长度
 * @return 编码结果（尾部不足 3 字节的组不输出 '='）
 *
 * 实现思路：与标准 Base64 相同的三字节分组查表，
 * 差别仅在码表（'-'/'_' 替换 '+'/'/'）且末尾组只输出实际存在的字符。
 */
std::string base64UrlEncode(const unsigned char* data, size_t len) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];
        out += kTable[(n >> 18) & 63];
        out += kTable[(n >> 12) & 63];
        if (i + 1 < len) out += kTable[(n >> 6) & 63];  // 无填充：缺字节则不输出对应字符
        if (i + 2 < len) out += kTable[n & 63];
    }
    return out;
}

/**
 * @brief Base64URL 解码
 * @param in Base64URL 字符串
 * @param out 解码输出（先清空）
 * @return 全部字符合法返回 true，否则 false
 *
 * 实现思路：与 base64Decode 相同的流式位累积方案，
 * 仅码表不同（'-'/'_'），遇 '=' 停止。
 */
bool base64UrlDecode(const std::string& in, std::string& out) {
    int table[256];
    for (int i = 0; i < 256; ++i) table[i] = -1;
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kTable[i])] = i;

    out.clear();
    unsigned int n = 0;
    int bits = 0;
    for (char ch : in) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == '=') break;             // JWT 可能带可选填充，遇之即停
        if (table[c] < 0) return false;  // 非法字符判失败
        n = (n << 6) | static_cast<unsigned int>(table[c]);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((n >> bits) & 0xFF);
        }
    }
    return true;
}

}  // namespace util
