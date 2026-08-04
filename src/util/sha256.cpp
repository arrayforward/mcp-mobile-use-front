#include "sha256.hpp"

#include <cstring>

namespace util {

namespace {

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

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256Ctx {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64];
    size_t bufLen = 0;
    uint64_t totalLen = 0;

    void block(const unsigned char* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6],
                 hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + s1 + ch + kK[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

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
                block(buf);
                bufLen = 0;
            }
        }
    }

    void final(unsigned char out[32]) {
        uint64_t bitLen = totalLen * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (bufLen != 56) update(&pad, 1);
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<unsigned char>(bitLen >> (56 - i * 8));
        update(lenBytes, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<unsigned char>(h[i] >> 24);
            out[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16);
            out[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8);
            out[i * 4 + 3] = static_cast<unsigned char>(h[i]);
        }
    }
};

}  // namespace

void sha256(const unsigned char* data, size_t len, unsigned char out[32]) {
    Sha256Ctx ctx;
    ctx.update(data, len);
    ctx.final(out);
}

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

std::string hmacSha256(const std::string& key, const std::string& message) {
    unsigned char keyBlock[64] = {0};
    std::string k = key;
    if (k.size() > 64) {
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
        if (i + 1 < len) out += kTable[(n >> 6) & 63];
        if (i + 2 < len) out += kTable[n & 63];
    }
    return out;
}

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
        if (c == '=') break;
        if (table[c] < 0) return false;
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
