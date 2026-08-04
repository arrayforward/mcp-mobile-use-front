#include "util.hpp"

namespace util {

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];
        out += kB64Table[(n >> 18) & 63];
        out += kB64Table[(n >> 12) & 63];
        out += (i + 1 < len) ? kB64Table[(n >> 6) & 63] : '=';
        out += (i + 2 < len) ? kB64Table[n & 63] : '=';
    }
    return out;
}

std::string base64Encode(const std::string& data) {
    return base64Encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

bool base64Decode(const std::string& in, std::string& out) {
    int table[256];
    for (int i = 0; i < 256; ++i) table[i] = -1;
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kB64Table[i])] = i;

    out.clear();
    unsigned int n = 0;
    int bits = 0;
    for (char ch : in) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == '=' || c == '\n' || c == '\r') break;
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

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

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

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace util
