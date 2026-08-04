#pragma once

#include <string>

namespace util {

std::string base64Encode(const unsigned char* data, size_t len);
std::string base64Encode(const std::string& data);
bool base64Decode(const std::string& in, std::string& out);

std::string trim(const std::string& s);
std::string shellQuote(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);

}  // namespace util
