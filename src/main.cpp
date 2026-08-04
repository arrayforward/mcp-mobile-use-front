#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <string>
#include <thread>

#include "mcp/auth.hpp"
#include "mcp/http_transport.hpp"
#include "mcp/jwt.hpp"
#include "mcp/stdio_server.hpp"
#include "net/http_server.hpp"
#include "service/cloud_config.hpp"
#include "service/provider.hpp"
#include "tool/base.hpp"

namespace {

std::atomic<bool> gStop{false};

void onSignal(int) {
    gStop = true;
}

void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  -t, --transport <type>   transport type: stdio | sse | streamable-http | http "
            "(default stdio)\n"
            "  -p, --port <port>        listen port for http transports (default 8080)\n"
            "  -b, --backend <backend>  default execution backend: adb | cloud (default adb)\n"
            "      --bind <addr>        bind address for http transports (default 0.0.0.0)\n"
            "      --auth-token <token> static token auth (Authorization: <token>)\n"
            "      --auth-jwt-secret <s> JWT HS256 auth with shared secret\n"
            "      --auth-jwt-public-key <pem> JWT RS256 auth with PEM public key/cert\n"
            "                           (env fallbacks: MCP_AUTH_TOKEN/MCP_JWT_SECRET/\n"
            "                           MCP_JWT_PUBLIC_KEY; default: no auth)\n"
            "      --tls-cert <pem>     enable HTTPS with server cert (requires OpenSSL)\n"
            "      --tls-key <pem>      HTTPS private key\n",
            prog);
}

}  // namespace

int main(int argc, char** argv) {
    std::string transport = "stdio";
    std::string backendStr = "adb";
    std::string bindAddr = "0.0.0.0";
    std::string authToken, jwtSecret, jwtKeyFile, tlsCert, tlsKey;
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", name);
                exit(2);
            }
            return argv[++i];
        };
        if (arg == "-t" || arg == "--transport") {
            transport = next("transport");
        } else if (arg == "-p" || arg == "--port") {
            port = atoi(next("port").c_str());
        } else if (arg == "-b" || arg == "--backend") {
            backendStr = next("backend");
        } else if (arg == "--bind") {
            bindAddr = next("bind");
        } else if (arg == "--auth-token") {
            authToken = next("auth-token");
        } else if (arg == "--auth-jwt-secret") {
            jwtSecret = next("auth-jwt-secret");
        } else if (arg == "--auth-jwt-public-key") {
            jwtKeyFile = next("auth-jwt-public-key");
        } else if (arg == "--tls-cert") {
            tlsCert = next("tls-cert");
        } else if (arg == "--tls-key") {
            tlsKey = next("tls-key");
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    service::Backend backend = service::backendFromString(backendStr, service::Backend::Adb);
    tool::initProvider(backend, service::CloudConfig::fromEnv());

    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "mcp_mobile_use starting: transport=%s backend=%s\n", transport.c_str(),
            service::backendToString(backend).c_str());

    if (transport == "stdio") {
        mcp::StdioServer server;
        return server.run();
    }

    if (transport != "sse" && transport != "streamable-http" && transport != "http") {
        fprintf(stderr, "invalid transport: %s\n", transport.c_str());
        usage(argv[0]);
        return 2;
    }

    net::HttpServer httpServer;
    mcp::McpHttpTransport mcpTransport;
    mcp::AuthChecker auth = mcp::AuthChecker::none();
    if (!jwtSecret.empty() || !jwtKeyFile.empty()) {
        mcp::JwtVerifier verifier;
        if (!jwtSecret.empty()) verifier.setHs256Secret(jwtSecret);
        if (!jwtKeyFile.empty()) verifier.loadRs256KeyFile(jwtKeyFile);
        auth = mcp::AuthChecker::withJwt(verifier);
    } else if (!authToken.empty()) {
        auth = mcp::AuthChecker::withToken(authToken);
    } else {
        auth = mcp::AuthChecker::fromEnv();
    }
    mcpTransport.setAuth(auth);
    mcpTransport.registerRoutes(httpServer);

    bool started = false;
    if (!tlsCert.empty() || !tlsKey.empty()) {
        started = httpServer.startTls(bindAddr, port, tlsCert, tlsKey);
        if (!started) fprintf(stderr, "failed to start https on %s:%d (check cert/key)\n",
                              bindAddr.c_str(), port);
    } else {
        started = httpServer.start(bindAddr, port);
        if (!started) fprintf(stderr, "failed to listen on %s:%d\n", bindAddr.c_str(), port);
    }
    if (!started) return 1;
    fprintf(stderr, "listening on %s:%d (%s; endpoints: GET /sse, POST /message, POST /mcp; "
                    "auth: %s)\n",
            bindAddr.c_str(), port, tlsCert.empty() ? "http" : "https", auth.describe().c_str());

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    while (!gStop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httpServer.stop();
    fprintf(stderr, "mcp_mobile_use stopped\n");
    return 0;
}
