#pragma once

#include <map>
#include <string>

namespace net {

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string error;

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

class HttpsClient {
public:
    explicit HttpsClient(int timeoutMs = 30000) : timeoutMs_(timeoutMs) {}

    HttpResponse post(const std::string& host, int port, const std::string& path,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body);

private:
    int timeoutMs_;
};

bool httpsClientAvailable();

}  // namespace net
