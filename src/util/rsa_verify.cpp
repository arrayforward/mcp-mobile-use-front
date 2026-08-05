/**
 * @file rsa_verify.cpp
 * @brief RSA PKCS#1 v1.5（SHA-256）验签与 SPKI PEM 公钥解析实现
 *
 * 功能：
 *   实现 rsa_verify.hpp 声明的两个接口：SPKI PEM -> (n, e) 的 DER 解析，
 *   以及基于 bignum 模幂的 PKCS#1 v1.5 验签。
 *
 * 开发思路：
 *   1. DER 解析用手写最简 TLV 读取器（tag/len/dataOff 三元组），
 *      支持短形式与长形式长度，逐层下钻 SPKI 结构；
 *      每一步都校验 tag 与边界，任何不合法即返回 false，不做错误恢复。
 *   2. 验签流程：s^e mod n 得到 EM，再逐字节校验
 *      00 01 FF..FF 00 DigestInfo(SHA-256) || digest 的固定格式；
 *      填充检查用普通比较（内容全为公开常数），
 *      而 DigestInfo 与 digest 的比较用异或累积的常数时间写法，
 *      避免时序侧信道（Bleichenbacher 类攻击的防护要点）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "rsa_verify.hpp"

#include "bignum.hpp"
#include "util.hpp"

namespace util {

namespace {

// 简单 DER TLV 读取器
/**
 * @struct Tlv
 * @brief DER TLV 三元组（tag / 内容长度 / 内容起始偏移）
 *
 * 开发思路：只记录位置不拷贝数据，配合 readTlv 的游标 pos
 * 可在原 DER 缓冲区上逐层下钻，零拷贝解析。
 */
struct Tlv {
    uint8_t tag = 0;
    size_t len = 0;
    size_t dataOff = 0;
};

/**
 * @brief 读取一个 DER TLV 头
 * @param der DER 缓冲区
 * @param pos 游标（读完后推进到内容起始处）
 * @param out 输出的 TLV 三元组
 * @return 成功返回 true；越界或长度非法返回 false
 *
 * 伪代码：
 *   读 tag（1 字节）
 *   读长度首字节：
 *     < 0x80 -> 短形式，长度即该字节
 *     否则    -> 长形式，低 7 位为长度字节数（限 1..4），大端读取
 *   校验 pos + len 不越界
 */
bool readTlv(const std::string& der, size_t& pos, Tlv& out) {
    if (pos >= der.size()) return false;
    out.tag = static_cast<uint8_t>(der[pos++]);
    if (pos >= der.size()) return false;
    uint8_t lenByte = static_cast<uint8_t>(der[pos++]);
    size_t len = 0;
    if (lenByte < 0x80) {
        len = lenByte;  // 短形式长度
    } else {
        size_t numBytes = lenByte & 0x7F;
        // 不定长形式(0)非法；超过 4 字节的长度在本场景不可能出现，拒绝以防溢出
        if (numBytes == 0 || numBytes > 4 || pos + numBytes > der.size()) return false;
        for (size_t i = 0; i < numBytes; ++i) len = (len << 8) | static_cast<uint8_t>(der[pos++]);
    }
    if (pos + len > der.size()) return false;  // 内容越界保护
    out.len = len;
    out.dataOff = pos;
    return true;
}

/**
 * @brief 读取 DER INTEGER 的内容字节
 * @param der DER 缓冲区
 * @param pos 游标（读完后推进到该 INTEGER 之后）
 * @param out 输出内容字节（去掉符号填充 0x00 后）
 * @return 成功返回 true
 *
 * 实现思路：INTEGER 为带符号补码，正数最高位为 1 时 DER 会在前面
 * 补一个 0x00；RSA 的 n/e 均为正数，需剥掉该填充字节，
 * 同时拒绝负数（最高位为 1 且无填充）。
 */
// 从 DER 读取 INTEGER 值（跳过符号填充 0x00），返回内容字节
bool readDerInteger(const std::string& der, size_t& pos, std::string& out) {
    Tlv tlv;
    if (!readTlv(der, pos, tlv) || tlv.tag != 0x02) return false;  // 0x02 = INTEGER
    size_t data = tlv.dataOff;
    size_t len = tlv.len;
    if (len == 0) return false;
    if (static_cast<uint8_t>(der[data]) & 0x80) return false;  // 负数不支持
    if (static_cast<uint8_t>(der[data]) == 0x00) {             // 去掉符号填充
        data++;
        len--;
        if (len == 0) return false;
    }
    out = der.substr(data, len);
    pos = data + len;
    return true;
}

}  // namespace

/**
 * @brief 解析 SPKI PEM 公钥，提取 RSA n/e
 * @param pem PEM 文本
 * @param nBytes 输出：模数 n（大端字节）
 * @param eBytes 输出：指数 e（大端字节）
 * @return 解析成功返回 true，任何一步不合法返回 false
 *
 * 伪代码：
 *   1. 剥离 PEM：跳过空白与 -----BEGIN/END----- 行，余下为 Base64
 *   2. Base64 解码得 DER
 *   3. SPKI ::= SEQUENCE { AlgorithmIdentifier, BIT STRING }
 *      读外层 SEQUENCE -> 读 AlgorithmIdentifier（SEQUENCE）
 *      -> 跳过其整个内容 -> 读 BIT STRING
 *   4. BIT STRING 首字节为未使用位数（必须为 0），余下为 RSAPublicKey DER
 *   5. RSAPublicKey ::= SEQUENCE { n INTEGER, e INTEGER }
 */
bool parseSpkiPem(const std::string& pem, std::string& nBytes, std::string& eBytes) {
    std::string base64;
    for (size_t i = 0; i < pem.size(); ++i) {
        char c = pem[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') continue;
        if (c == '-') {
            // 跳过 -----BEGIN/END----- 行
            while (i < pem.size() && pem[i] != '\n') ++i;
            continue;
        }
        base64 += c;
    }
    std::string der;
    if (!base64Decode(base64, der)) return false;

    size_t pos = 0;
    Tlv spki;
    if (!readTlv(der, pos, spki) || spki.tag != 0x30) return false;  // SEQUENCE

    size_t p = spki.dataOff;
    Tlv algId;
    if (!readTlv(der, p, algId) || algId.tag != 0x30) return false;  // AlgorithmIdentifier
    // 关键：AlgorithmIdentifier（OID + NULL）长度不固定，
    // 必须按 dataOff + len 整体跳过，不能假定固定偏移，
    // 否则后续 BIT STRING 会读错位置导致解析失败
    p = algId.dataOff + algId.len;  // 跳过 AlgorithmIdentifier 数据（OID + NULL）

    Tlv bitString;
    if (!readTlv(der, p, bitString) || bitString.tag != 0x03) return false;  // BIT STRING
    if (bitString.len == 0 || static_cast<uint8_t>(der[bitString.dataOff]) != 0x00)
        return false;  // 未使用位必须为 0
    std::string rsaDer = der.substr(bitString.dataOff + 1, bitString.len - 1);

    size_t q = 0;
    Tlv rsaSeq;
    if (!readTlv(rsaDer, q, rsaSeq) || rsaSeq.tag != 0x30) return false;
    if (!readDerInteger(rsaDer, q, nBytes)) return false;  // 模数 n
    if (!readDerInteger(rsaDer, q, eBytes)) return false;  // 指数 e
    return true;
}

/**
 * @brief RSA PKCS#1 v1.5（SHA-256）验签
 * @param nBytes RSA 模数 n（大端字节）
 * @param eBytes RSA 公钥指数 e（大端字节）
 * @param signature 签名（大端字节，与 n 等长）
 * @param digest 32 字节 SHA-256 摘要
 * @return 验签通过返回 true
 *
 * 伪代码：
 *   1. s >= n -> 拒绝（RSA 原语定义域检查）
 *   2. m = s^e mod n
 *   3. m 转大端字节，左补 0 到 k = len(n) 字节，得 EM
 *   4. 校验 EM 结构：00 01 FF..FF 00 DigestInfo || digest
 *      - k < tLen + 11（至少 8 字节 FF 填充）-> 拒绝
 *      - 逐字节校验 00 01、FF 填充、00 分隔符
 *      - DigestInfo || digest 用常数时间异或累积比较
 */
bool rsaVerifyPkcs1Sha256(const std::string& nBytes, const std::string& eBytes,
                          const std::string& signature, const std::string& digest) {
    Big n = bigFromBytes(nBytes);
    Big e = bigFromBytes(eBytes);
    Big s = bigFromBytes(signature);

    if (bigCmp(s, n) >= 0) return false;  // 签名必须 < n

    Big m = bigModPow(s, e, n);

    // m → 大端字节，左补齐到 n 的字节长度
    size_t k = nBytes.size();
    std::string em(k, '\0');
    size_t firstLimb = m.size();
    while (firstLimb > 0 && m[firstLimb - 1] == 0) --firstLimb;  // 跳过高位零 limb
    if (firstLimb > 0) {
        size_t limb = firstLimb - 1;
        uint32_t top = m[limb];
        int topBytes = 0;
        for (uint32_t t = top; t; t >>= 8) ++topBytes;  // 最高 limb 实际占用的字节数
        if (topBytes == 0) topBytes = 1;
        size_t mBytes = m.size() * 4;
        if (mBytes - 4 + static_cast<size_t>(topBytes) > k) return false;  // m 超过模数长度，非法
        size_t off = k - (mBytes - 4 + static_cast<size_t>(topBytes));     // 右对齐：左侧补 0
        for (int j = topBytes - 1; j >= 0; --j)
            em[off++] = static_cast<char>((top >> (j * 8)) & 0xFF);        // 最高 limb 大端写出
        for (size_t i = limb; i > 0; --i) {
            uint32_t limbVal = m[i - 1];
            for (int j = 3; j >= 0; --j) em[off++] = static_cast<char>((limbVal >> (j * 8)) & 0xFF);
        }
    }

    // PKCS#1 v1.5 填充检查：EM = 0x00 0x01 FF..FF 0x00 T
    // SHA-256 的 DigestInfo 前缀（RFC 8017 附录，DER 编码的算法标识）
    static const unsigned char kSha256DigestInfo[] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
        0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
    const size_t tLen = sizeof(kSha256DigestInfo) + 32;
    if (k < tLen + 11) return false;  // 至少 8 字节 FF 填充 + 00 01 + 00 分隔，共 11 字节开销

    if (static_cast<unsigned char>(em[0]) != 0x00 || static_cast<unsigned char>(em[1]) != 0x01)
        return false;
    size_t i = 2;
    while (i < k - tLen - 1) {
        if (static_cast<unsigned char>(em[i]) != 0xFF) return false;  // 填充段必须全为 0xFF
        ++i;
    }
    if (static_cast<unsigned char>(em[i]) != 0x00) return false;  // 0x00 分隔符
    ++i;

    // 常数时间比较 DigestInfo || digest：
    // 用异或累积 diff 并循环完整长度，不提前退出，
    // 防止通过响应时间逐字节探测正确前缀的时序侧信道
    unsigned char diff = 0;
    for (size_t j = 0; j < sizeof(kSha256DigestInfo); ++j)
        diff |= static_cast<unsigned char>(em[i + j]) ^ kSha256DigestInfo[j];
    for (size_t j = 0; j < 32; ++j)
        diff |= static_cast<unsigned char>(em[i + sizeof(kSha256DigestInfo) + j]) ^
                static_cast<unsigned char>(digest[j]);
    return diff == 0;
}

}  // namespace util
