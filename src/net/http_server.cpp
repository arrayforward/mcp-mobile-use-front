#include "http_server.hpp"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef MCP_WITH_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace net {

namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i] == '+' ? ' ' : s[i];
    }
    return out;
}

}  // namespace

int Io::read(void* buf, size_t len) {
#ifdef MCP_WITH_OPENSSL
    if (ssl) return SSL_read(ssl, buf, static_cast<int>(len));
#endif
    return recv(fd, buf, len, 0);
}

bool Io::writeAll(const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
#ifdef MCP_WITH_OPENSSL
        if (ssl) {
            int w = SSL_write(ssl, p + sent, static_cast<int>(len - sent));
            if (w <= 0) return false;
            sent += static_cast<size_t>(w);
            continue;
        }
#endif
        ssize_t w = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (w <= 0) return false;
        sent += static_cast<size_t>(w);
    }
    return true;
}

bool Io::writeAll(const std::string& s) { return writeAll(s.data(), s.size()); }

void Io::close() {
#ifdef MCP_WITH_OPENSSL
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = nullptr;
    }
#endif
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

std::string Request::header(const std::string& name) const {
    std::string lname = lower(name);
    for (const auto& h : headers)
        if (lower(h.first) == lname) return h.second;
    return "";
}

std::string Request::queryParam(const std::string& name) const {
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        std::string pair = query.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
        if (key == name) return eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
        pos = amp + 1;
    }
    return "";
}

Response Response::json(int status, const std::string& body) {
    Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = body;
    return r;
}

Response Response::text(int status, const std::string& body) {
    Response r;
    r.status = status;
    r.contentType = "text/plain";
    r.body = body;
    return r;
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::addRoute(const std::string& method, const std::string& path, Handler handler) {
    routes_.push_back({method, path, std::move(handler)});
}

const HttpServer::Handler* HttpServer::findRoute(const std::string& method,
                                                 const std::string& path) const {
    for (const auto& r : routes_)
        if (r.method == method && r.path == path) return &r.handler;
    return nullptr;
}

bool HttpServer::start(const std::string& addr, int port) {
    return startTls(addr, port, "", "");
}

bool HttpServer::startTls(const std::string& addr, int port, const std::string& certFile,
                          const std::string& keyFile) {
    bool wantTls = !certFile.empty() || !keyFile.empty();
#ifdef MCP_WITH_OPENSSL
    if (wantTls) {
        sslCtx_ = SSL_CTX_new(TLS_server_method());
        if (!sslCtx_) return false;
        SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);
        if (SSL_CTX_use_certificate_chain_file(sslCtx_, certFile.c_str()) != 1 ||
            SSL_CTX_use_PrivateKey_file(sslCtx_, keyFile.c_str(), SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(sslCtx_) != 1) {
            SSL_CTX_free(sslCtx_);
            sslCtx_ = nullptr;
            return false;
        }
    }
#else
    if (wantTls) return false;  // 未编译 OpenSSL，不支持 TLS
#endif

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) return false;
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1 ||
        bind(listenFd_, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) != 0 ||
        listen(listenFd_, 16) != 0) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    port_ = port;
    stop_ = false;
    running_ = true;
    acceptThread_ = std::thread(&HttpServer::acceptLoop, this);
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    stop_ = true;
    if (listenFd_ >= 0) {
        shutdown(listenFd_, SHUT_RDWR);
        close(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
#ifdef MCP_WITH_OPENSSL
    if (sslCtx_) {
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
    }
#endif
}

void HttpServer::acceptLoop() {
    while (!stop_.load()) {
        int fd = accept(listenFd_, nullptr, nullptr);
        if (fd < 0) {
            if (stop_.load()) break;
            continue;
        }
        std::thread([this, fd] {
            Io io;
            io.fd = fd;
#ifdef MCP_WITH_OPENSSL
            if (sslCtx_) {
                io.ssl = SSL_new(sslCtx_);
                if (!io.ssl) {
                    io.close();
                    return;
                }
                SSL_set_fd(io.ssl, fd);
                if (SSL_accept(io.ssl) != 1) {
                    io.close();
                    return;
                }
            }
#endif
            handleConnection(io);
            io.close();
        }).detach();
    }
}

void HttpServer::handleConnection(Io& io) {
    std::string raw;
    raw.reserve(4096);
    char buf[8192];
    size_t headerEnd = std::string::npos;

    while (raw.size() < 1024 * 1024) {
        ssize_t r = io.read(buf, sizeof(buf));
        if (r <= 0) return;
        raw.append(buf, static_cast<size_t>(r));
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    if (headerEnd == std::string::npos) return;

    Request req;
    size_t lineEnd = raw.find("\r\n");
    std::string requestLine = raw.substr(0, lineEnd);
    size_t sp1 = requestLine.find(' ');
    size_t sp2 = requestLine.find(' ', sp1 == std::string::npos ? sp1 : sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return;
    req.method = requestLine.substr(0, sp1);
    std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = target.find('?');
    if (q == std::string::npos) {
        req.path = target;
    } else {
        req.path = target.substr(0, q);
        req.query = target.substr(q + 1);
    }

    size_t pos = lineEnd + 2;
    while (pos < headerEnd) {
        size_t end = raw.find("\r\n", pos);
        if (end == std::string::npos || end > headerEnd) break;
        std::string line = raw.substr(pos, end - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
            req.headers.emplace_back(name, value);
        }
        pos = end + 2;
    }

    size_t contentLength = 0;
    std::string cl = req.header("Content-Length");
    if (!cl.empty()) contentLength = static_cast<size_t>(atol(cl.c_str()));

    req.body = raw.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
        ssize_t r = io.read(buf, sizeof(buf));
        if (r <= 0) break;
        req.body.append(buf, static_cast<size_t>(r));
    }
    if (req.body.size() > contentLength) req.body.resize(contentLength);

    const Handler* handler = findRoute(req.method, req.path);
    if (!handler) {
        Response r = Response::json(404, "{\"error\":\"not found\"}");
        io.writeAll("HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n");
        io.writeAll(r.body);
        return;
    }

    Response resp;
    try {
        resp = (*handler)(req);
    } catch (const std::exception& e) {
        resp = Response::json(500, std::string("{\"error\":\"") + e.what() + "\"}");
    }

    if (resp.sse) {
        io.writeAll("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\nConnection: keep-alive\r\n"
                    "Access-Control-Allow-Origin: *\r\n\r\n");
        if (resp.sseHandler) resp.sseHandler(io);
        return;
    }

    std::string reason = resp.status == 200 ? "OK" : (resp.status == 202 ? "Accepted" : "Error");
    std::string head = "HTTP/1.1 " + std::to_string(resp.status) + " " + reason + "\r\n";
    head += "Content-Type: " + resp.contentType + "\r\n";
    head += "Content-Length: " + std::to_string(resp.body.size()) + "\r\n";
    for (const auto& h : resp.extraHeaders) head += h.first + ": " + h.second + "\r\n";
    head += "Connection: close\r\n\r\n";
    io.writeAll(head);
    if (!resp.body.empty()) io.writeAll(resp.body);
}

}  // namespace net
