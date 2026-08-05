# 架构设计

## 总体架构

mcp_mobile_use 运行在云手机 Android 环境内部，对外提供标准 MCP（Model Context Protocol）服务。整体分为五层，全部核心逻辑由 C++17 实现（零第三方依赖，仅 cloud 后端可选链接 OpenSSL），Java 仅作为 APK 模式的最薄壳：

```
┌─────────────────────────────────────────────────────────┐
│ MCP Client（Claude / Agent / opencode 等）                │
└──────────────┬──────────────────────────────────────────┘
               │ JSON-RPC 2.0
┌──────────────▼──────────────────────────────────────────┐
│ 传输层 src/mcp/ src/net/                                 │
│  ├─ StdioServer          （stdin/stdout 行分隔 JSON-RPC）│
│  ├─ McpHttpTransport     （/sse + /message，SSE 推送）   │
│  ├─ McpHttpTransport     （/mcp，streamable-http 同步）  │
│  └─ /healthz             （探活接口，不校验鉴权）        │
│        底层为手写 socket HTTP server（thread-per-conn， │
│        keep-alive 复用，单连接上限 100 请求/60s 空闲超时）│
├─────────────────────────────────────────────────────────┤
│ 协议层 src/mcp/protocol.cpp                              │
│  initialize / ping / tools/list / tools/call 分发        │
├─────────────────────────────────────────────────────────┤
│ 工具层 src/tool/（12 个工具，对齐 mcp_server_mobile_use）│
│  schema 定义 + 参数校验 + 结果封装（text / image）       │
├─────────────────────────────────────────────────────────┤
│ 服务层 src/service/                                      │
│  ├─ CommandBuilder：生成 shell 命令串（两后端唯一事实源）│
│  ├─ LocalExecutor ：fork/execvp 本机执行（adb 后端）     │
│  └─ CloudExecutor ：华为云 CPH RunSyncCommand（cloud 后端）│
└─────────────────────────────────────────────────────────┘
```

## 关键设计

### 命令构建与执行分离

所有工具的操作最终都是一条 shell 命令（`input tap`、`screencap -p`、`pm list packages` …）。`CommandBuilder` 统一生成命令串，`Executor` 接口负责执行：

- **adb 后端（默认）**：`LocalExecutor` 在本机 fork + exec `/system/bin/sh -c`，pipe 捕获 stdout/stderr（二进制安全，支持截图原始 PNG），poll 实现超时控制
- **cloud 后端**：`CloudExecutor` 将命令串包装为华为云 CPH `RunSyncCommand` 请求（HTTPS + AK/SK 签名或 X-Auth-Token），解析 `jobs[0].execute_msg`

同一命令两个通道，切换零成本。

### backend 参数路由

每个工具的 `inputSchema` 都自动注入 `backend` 枚举参数（`adb`/`cloud`）。工具 handler 通过 `provider().resolveBackend(args)` 解析：参数未传时使用启动参数 `--backend` 的全局默认值。

### 安全设计（可选鉴权 + TLS）

对齐参考项目 `authFromEnv`/`authFromRequest` + `CheckAuth` 的模式，但**默认不启用**：

```
HTTP 请求 ──▶ McpHttpTransport（/sse、/message、/mcp）
                │
                ├─ AuthChecker::check(req) ── 未通过 ──▶ 401 unauthorized
                │
                ▼（通过后）
            Protocol::handleMessage ──▶ ToolDef.handler（CheckAuth 语义内置于传输层）
```

| 方案 | 实现 | 依赖 |
|---|---|---|
| 静态 Token | `src/mcp/auth.cpp`（Authorization 头比对） | 无 |
| JWT HS256 | `src/mcp/jwt.cpp` + 手写 SHA-256/HMAC | 无 |
| JWT RS256 | OpenSSL `EVP_DigestVerify`，PEM 公钥/x509 证书 | OpenSSL |
| HTTPS | `HttpServer::startTls`（`Io` 抽象统一明文/TLS 读写） | OpenSSL |

详见 [安全方案](security.md)。传输层与协议层完全解耦：鉴权只挂在 `McpHttpTransport` 入口，
不影响 stdio/协议实现，新增后端或工具无需感知。

### 部署双模式

| 模式 | 形态 | 权限 | 适用 |
|---|---|---|---|
| 系统服务模式 | native 二进制 + init.rc（或 Magisk 模块），或签名系统应用 | root/system | 有 root + 平台签名 |
| 前台服务模式 | APK（Java 壳 + JNI 加载同一核心库 `libmcp_mobile_use_jni.so`） | 应用/shell | 无 root/签名 |

两种模式共享同一份 C++ 核心（`mcp_mobile_use_core` 静态库），仅入口不同（`main.cpp` vs `jni_bridge.cpp`）。

## 主要业务流程

### 1. stdio 调用流程

```
Client ──写一行 JSON-RPC──▶ StdioServer::run（getline 循环）
                              │ Protocol::handleMessage
                              ▼
                         dispatch(method)
                              │ tools/call
                              ▼
                         ToolDef.handler(args)
                              │ provider().tap/swipe/...(resolveBackend)
                              ▼
                         CommandBuilder → LocalExecutor fork/exec
                              │
Client ◀──一行 JSON 响应──── 结果封装（text/image content）
```

### 2. SSE 调用流程

```
Client ──GET /sse──────────▶ 创建 sessionId，SSE 长连接
Client ◀──event: endpoint─── data: /message?sessionId=xxx
Client ──POST /message─────▶ handleMessage → 响应 JSON 入 session 队列 → 202 Accepted
Client ◀──event: message─── SSE 线程从队列取响应推送
```

### 3. streamable-http 调用流程

```
Client ──POST /mcp（JSON-RPC）──▶ handleMessage ──▶ 同步返回 application/json 响应
```

### 4. 工具执行流程（以 tap 为例）

```
tools/call {"name":"tap","arguments":{"x":540,"y":1200,"backend":"adb"}}
  → getInt 校验 x/y
  → resolveBackend(args) → Backend::Adb
  → CommandBuilder::tap → "input tap 540 1200"
  → LocalExecutor::runShell（30s 超时）
  → exitCode==0 → textResult("Tap the screen successfully at (540, 1200)")
```

### 5. 截图流程（adb 后端）

```
take_screenshot
  → "screencap -p" 本机执行，pipe 捕获二进制 stdout
  → 校验 PNG magic（\x89PNG）
  → base64 编码
  → "wm size" 解析分辨率（Physical size: WxH）
  → imageResult(base64) → MCP image content（LLM 可直接读图）
```

### 6. cloud 后端流程

```
工具调用 backend=cloud
  → CloudConfig::fromEnv 校验（CPH_ENDPOINT/PROJECT_ID/PHONE_ID/TOKEN 或 AK+SK）
  → CommandBuilder 命令串 → {"command":"shell","content":<cmd>,"phone_ids":[...]}
  → hw_signer（SDK-HMAC-SHA256）或 X-Auth-Token
  → HttpsClient（OpenSSL TLS）POST /v1/{project}/cloud-phone/phones/sync-commands
  → 解析 jobs[0]：status==2 成功，execute_msg 作为命令输出
```

## 目录结构

```
mcp_mobile_use/
├── CMakeLists.txt              # NDK 交叉编译 + 主机单测
├── src/
│   ├── json/                   # 手写 JSON（Value/parse/dump）
│   ├── util/                   # base64、shell 转义等
│   ├── net/                    # http_server（手写 socket）、https_client（OpenSSL）
│   ├── mcp/                    # protocol、stdio_server、http_transport（sse+streamable）
│   ├── service/                # command_builder、executor、provider、hw_signer
│   ├── tool/                   # 12 个 MCP 工具
│   ├── main.cpp                # native 入口（三种传输）
│   └── android/jni_bridge.cpp  # JNI 入口（APK 模式）
├── android/                    # Java 薄壳 APK 工程
├── scripts/                    # 构建/部署/e2e 脚本
├── tests/                      # 主机侧单元测试
└── docs/                       # 中文接口文档
```
