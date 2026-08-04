#include "https_client.hpp"

#include <string.h>

#include <chrono>

#ifdef MCP_WITH_OPENSSL

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace net {

namespace {

int connectTcp(const std::string& host, int port, int timeoutMs, std::string& err) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        err = "getaddrinfo failed for " + host;
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) err = "connect failed to " + host + ":" + portStr;
    return fd;
}

bool readAll(SSL* ssl, std::string& out) {
    char buf[16384];
    while (true) {
        int r = SSL_read(ssl, buf, sizeof(buf));
        if (r > 0) {
            out.append(buf, static_cast<size_t>(r));
            continue;
        }
        int code = SSL_get_error(ssl, r);
        if (code == SSL_ERROR_ZERO_RETURN) return true;
        if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE) continue;
        return false;
    }
}

}  // namespace

bool httpsClientAvailable() { return true; }

HttpResponse HttpsClient::post(const std::string& host, int port, const std::string& path,
                               const std::map<std::string, std::string>& headers,
                               const std::string& body) {
    HttpResponse resp;

    std::string err;
    int fd = connectTcp(host, port, timeoutMs_, err);
    if (fd < 0) {
        resp.error = err;
        return resp;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        close(fd);
        resp.error = "SSL_CTX_new failed";
        return resp;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host.c_str());

    if (SSL_connect(ssl) != 1) {
        resp.error = "TLS handshake failed with " + host;
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return resp;
    }

    std::string req = "POST " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "Connection: close\r\n";
    for (const auto& h : headers) req += h.first + ": " + h.second + "\r\n";
    req += "\r\n";
    req += body;

    size_t sent = 0;
    while (sent < req.size()) {
        int w = SSL_write(ssl, req.data() + sent, static_cast<int>(req.size() - sent));
        if (w <= 0) {
            resp.error = "SSL_write failed";
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(fd);
            return resp;
        }
        sent += static_cast<size_t>(w);
    }

    std::string raw;
    if (!readAll(ssl, raw)) {
        resp.error = "failed reading response";
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return resp;
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);

    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) {
        resp.error = "malformed http response";
        return resp;
    }
    if (sscanf(raw.c_str(), "HTTP/%*s %d", &resp.status) != 1) {
        resp.error = "malformed status line: " + raw.substr(0, lineEnd);
        return resp;
    }

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        resp.error = "malformed http headers";
        return resp;
    }
    resp.body = raw.substr(headerEnd + 4);
    return resp;
}

}  // namespace net

#else  // !MCP_WITH_OPENSSL

namespace net {

bool httpsClientAvailable() { return false; }

HttpResponse HttpsClient::post(const std::string&, int, const std::string&,
                               const std::map<std::string, std::string>&, const std::string&) {
    HttpResponse resp;
    resp.error = "https client not compiled in (rebuild with MCP_WITH_OPENSSL=ON)";
    return resp;
}

}  // namespace net

#endif
