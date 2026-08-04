#include "stdio_server.hpp"

#include <iostream>

namespace mcp {

int StdioServer::run() {
    std::ios::sync_with_stdio(false);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::string response;
        if (protocol_.handleMessage(line, response) && !response.empty()) {
            std::cout << response << "\n";
            std::cout.flush();
        }
    }
    return 0;
}

}  // namespace mcp
