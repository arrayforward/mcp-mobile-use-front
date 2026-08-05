# 传输方式

mcp_mobile_use 支持 MCP 标准的三种传输方式，通过启动参数 `--transport`（或 `-t`）选择。

```bash
mcp_mobile_use -t <stdio|sse|streamable-http|http> [-p 8080] [-b adb|cloud] [--bind 0.0.0.0]
```

## stdio

```bash
mcp_mobile_use -t stdio
```

- 通过标准输入/输出通信，每行一个 JSON-RPC 2.0 消息（换行分隔）
- 适合通过 `adb shell` 直接管道调用，或被宿主进程作为子进程拉起
- 日志输出到 stderr，不干扰协议通道

示例：

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"c","version":"1"}}}' \
  | adb shell /data/local/tmp/mcp_mobile_use -t stdio
```

## sse（HTTP + Server-Sent Events）

```bash
mcp_mobile_use -t sse -p 8080
```

- `GET /sse`：建立 SSE 长连接，服务端首先下发 `endpoint` 事件：
  ```
  event: endpoint
  data: /message?sessionId=<32位hex>
  ```
- `POST /message?sessionId=<id>`：客户端将 JSON-RPC 消息 POST 到此端点，立即返回 `202 Accepted`
- 响应通过 SSE 连接的 `message` 事件异步推送：
  ```
  event: message
  data: {"jsonrpc":"2.0","id":1,"result":{...}}
  ```
- 空闲 15 秒发送 `: keepalive` 注释保活

## streamable-http

```bash
mcp_mobile_use -t streamable-http -p 8080
```

- `POST /mcp`：单端点，请求体为 JSON-RPC 消息
- 普通请求直接以 `application/json` 同步返回响应
- 通知类消息（无 `id`）返回 `202` 空响应

## http（sse + streamable-http 混合）

```bash
mcp_mobile_use -t http -p 8080
```

在同一端口同时挂载 `/sse`、`/message`、`/mcp` 三个端点，APK 前台服务模式默认使用此方式。

## 健康检查 /healthz

所有 HTTP 模式（sse / streamable-http / http）均提供探活接口：

```bash
curl http://<host>:8080/healthz
# {"status":"ok","name":"mcp_mobile_use","version":"0.1.0"}
```

- 返回 `200` + JSON，供外部服务（负载均衡、容器探针、监控）判断服务存活
- **不校验鉴权**，始终可访问（仅反映进程与服务是否存活）

## 多路连接

- **并发连接**：thread-per-connection 模型，可同时处理任意数量客户端（SSE 长连接与普通请求互不阻塞）
- **keep-alive**：同一 TCP 连接支持连续多个请求复用（HTTP/1.1，单连接上限 100 次请求，空闲 60 秒超时断开），减少握手开销

## 鉴权与 TLS

HTTP 传输（sse/streamable-http/http）支持可选鉴权与 HTTPS，默认关闭：

- 鉴权：`--auth-token`（静态 token）或 JWT（`--auth-jwt-secret` / `--auth-jwt-public-key`），
  校验失败返回 `401`
- HTTPS：`--tls-cert <pem> --tls-key <pem>` 将同一端口切换为 TLS

详见 [安全方案](security.md)。

## 客户端访问方式

服务监听在设备上，外部访问可选：

1. **adb forward**：`adb forward tcp:8080 tcp:8080`，然后访问 `http://127.0.0.1:8080`
2. **直连**：云手机有可达 IP 时直接访问 `http://<device_ip>:8080`（注意无鉴权，请仅在可信网络使用）
