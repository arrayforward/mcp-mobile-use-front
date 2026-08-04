# 扩展开发文档

面向二次开发：新增工具、新增执行后端、扩展传输、鉴权接入。

## 代码结构速览

```
src/
├── json/        手写 JSON（mj::Value：parse/dump/at/has）
├── util/        base64、shellQuote、SHA-256/HMAC（手写）、base64url
├── net/         HttpServer（手写 socket，支持明文/TLS，Io 抽象读写）
│                HttpsClient（OpenSSL TLS 客户端，cloud 后端用）
├── mcp/         Protocol（JSON-RPC 分发）、stdio_server、
│                http_transport（sse+streamable）、auth、jwt
├── service/     CommandBuilder（生成 shell 命令）
│                Executor 接口 + LocalExecutor/CloudExecutor
│                MobileUseProvider（工具语义封装）
├── tool/        工具定义（ToolDef + handler），base.hpp 提供公共助手
├── main.cpp     native 入口：启动参数解析
└── android/     JNI 桥（APK 模式入口）
```

## 新增一个工具

1. **定义 ToolDef**（如 `src/tool/my_tool.cpp`）：

```cpp
#include "base.hpp"

namespace tool {

ToolDef makeMyToolTool() {
    mj::Value props = mj::Value::object();
    props["arg1"] = propString("参数说明");
    ToolDef def;
    def.name = "my_tool";                       // MCP 工具名
    def.description = "工具描述";
    def.inputSchema = makeSchema(props, {"arg1"});  // 自动注入 backend 参数
    def.handler = [](const mj::Value& args) -> mj::Value {
        std::string v, err;
        if (!getRequiredString(args, "arg1", v, err)) return errorResult(err);
        // 通过 provider() 调用服务层
        if (!provider().someAction(pickBackend(args), v, err)) return errorResult(err);
        return textResult("done");
    };
    return def;
}

}  // namespace tool
```

2. **注册**：在 `src/tool/registry.cpp` 添加 `ToolDef makeMyToolTool();` 并 push 到 `allTools()`。
3. **命令生成**：在 `service/command_builder.cpp` 添加命令串函数（注意用户输入用 `util::shellQuote` 防注入）。
4. **服务方法**：在 `MobileUseProvider` 增加方法并复用 `executor(backend)->runShell(...)`。
5. **文档**：`docs/tools/my_tool.md` + 更新 `docs/README.md` 表格。
6. **测试**：单测加 command_builder 断言；e2e 脚本加一条调用。

## 新增执行后端

实现 `Executor` 接口（`src/service/executor.hpp`）：

```cpp
class MyExecutor : public Executor {
public:
    ExecResult runShell(const std::string& cmd, int timeoutMs) override { ... }
    const char* name() const override { return "my"; }
};
```

- 在 `MobileUseProvider` 中持有实例并按 backend 值路由（参考 `cloud_` 的处理）
- `backendFromString`/`backendToString` 增加枚举值；`makeSchema` 的 backend enum 增加可选项
- 复用 `CommandBuilder` 生成的命令串，做到"命令一处定义，多后端执行"

## 扩展传输

- 新端点：`HttpServer::addRoute(method, path, handler)` 注册（见 `McpHttpTransport::registerRoutes`）
- 新协议：实现 `Protocol::dispatch` 中的新 method 分支；通知类（无 id）返回空响应
- SSE 推送：handler 返回 `Response{sse=true, sseHandler=[&](Io& io){...}}`，自行管理长连接读写

## 鉴权扩展

- 新增鉴权方案：扩展 `AuthChecker::Mode` 与 `AuthChecker::check()`（`src/mcp/auth.cpp`）
- 新增 JWT 算法：`JwtVerifier::verifySignature()` 增加分支（HS512、ES256 等）
- 启动参数：`main.cpp` 参数解析 + `AuthChecker::fromEnv()` 环境变量映射
- 与参考项目对齐点：`authFromEnv`（环境变量注入配置）、`authFromRequest`（HTTP 头注入）、
  工具调用前 `CheckAuth` 统一校验

## 构建与测试

```powershell
# 主机单测（JSON/SHA256/JWT/命令生成）
g++ -std=c++17 -Isrc -o tests/mcp_mobile_use_tests.exe tests/test_main.cpp src/json/json.cpp src/util/util.cpp src/util/sha256.cpp src/mcp/jwt.cpp src/service/command_builder.cpp
./tests/mcp_mobile_use_tests.exe

# Android 构建
powershell -File scripts/build_native.ps1

# 真机 e2e（12 项断言，含鉴权）
powershell -File scripts/e2e_adb.ps1

# 需要 cloud 后端/HTTPS/RS256 时先编译 OpenSSL
sh scripts/build_openssl.sh ~/openssl-3.0.13
powershell -File scripts/build_native.ps1 -WithOpenSSL
```
