#pragma once

#include "protocol.hpp"

namespace mcp {

class StdioServer {
public:
    int run();

private:
    Protocol protocol_;
};

}  // namespace mcp
