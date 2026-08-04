#pragma once

#include <cstdint>
#include <string>

namespace util {

std::string sha256Hex(const std::string& input);
void sha256(const unsigned char* data, size_t len, unsigned char out[32]);

std::string hmacSha256(const std::string& key, const std::string& message);

std::string base64UrlEncode(const unsigned char* data, size_t len);
bool base64UrlDecode(const std::string& in, std::string& out);

}  // namespace util
