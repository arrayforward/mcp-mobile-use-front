#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef MCP_WITH_OPENSSL
struct ssl_st;
typedef struct ssl_st SSL;
struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;
#endif

namespace net {

// 统一读写抽象：明文 fd 或 TLS(OpenSSL) SSL*
struct Io {
    int fd = -1;
#ifdef MCP_WITH_OPENSSL
    SSL* ssl = nullptr;
#endif
    int read(void* buf, size_t len);
    bool writeAll(const void* data, size_t len);
    bool writeAll(const std::string& s);
    void close();
};

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    std::string header(const std::string& name) const;
    std::string queryParam(const std::string& name) const;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json";
    std::map<std::string, std::string> extraHeaders;
    std::string body;

    bool sse = false;
    std::function<void(Io&)> sseHandler;

    static Response json(int status, const std::string& body);
    static Response text(int status, const std::string& body);
};

class HttpServer {
public:
    using Handler = std::function<Response(const Request&)>;

    ~HttpServer();

    void addRoute(const std::string& method, const std::string& path, Handler handler);
    bool start(const std::string& addr, int port);
    // 启用 HTTPS：cert/key 为 PEM 文件路径（需 MCP_WITH_OPENSSL 构建）
    bool startTls(const std::string& addr, int port, const std::string& certFile,
                  const std::string& keyFile);
    void stop();
    bool running() const { return running_; }
    int port() const { return port_; }

private:
    void acceptLoop();
    void handleConnection(Io& io);
    bool readRequest(Io& io, std::string& buffer, Request& req);
    const Handler* findRoute(const std::string& method, const std::string& path) const;
    bool shouldStop() const { return stop_.load(); }

    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
#ifdef MCP_WITH_OPENSSL
    SSL_CTX* sslCtx_ = nullptr;
#endif

    struct Route {
        std::string method;
        std::string path;
        Handler handler;
    };
    std::vector<Route> routes_;
};

}  // namespace net
