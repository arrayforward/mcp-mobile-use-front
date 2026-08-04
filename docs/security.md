# 安全方案（鉴权与传输安全）

> 默认**不启用鉴权**，便于本地与测试使用；公网暴露前务必开启。

## 启动配置

| 配置 | 启动参数 | 环境变量 | 说明 |
|---|---|---|---|
| 静态 Token | `--auth-token <token>` | `MCP_AUTH_TOKEN` | 请求头 `Authorization: <token>` 或 `Authorization: Bearer <token>` |
| JWT HS256 | `--auth-jwt-secret <secret>` | `MCP_JWT_SECRET` | 共享密钥验签（HMAC-SHA256 手写实现，无需 OpenSSL） |
| JWT RS256 | `--auth-jwt-public-key <pem>` | `MCP_JWT_PUBLIC_KEY` | 从 PEM 公钥**或 x509 证书文件**加载验签密钥（需 OpenSSL 构建） |
| HTTPS | `--tls-cert <pem> --tls-key <pem>` | - | 服务端 TLS 加密，证书+私钥 PEM 文件（需 OpenSSL 构建） |

优先级：命令行参数 > 环境变量；同时配置 token 与 jwt 时 jwt 优先。默认 `none` 不校验。

## 鉴权方案

实现位于 `src/mcp/auth.hpp`、`src/mcp/jwt.hpp`，结构对齐参考项目
`mcp_server_mobile_use` 的 `authFromEnv`/`authFromRequest` + `CheckAuth` 模式：

```
HTTP 请求 ──▶ McpHttpTransport handler
                ├─ AuthChecker::check(req) ── 401 unauthorized
                ├─（通过后）Protocol::handleMessage
```

| 方案 | 校验逻辑 |
|---|---|
| 静态 Token | 请求头 `Authorization` 与配置 token 严格相等（支持裸 token 或 `Bearer ` 前缀） |
| JWT | 仅接受 `Authorization: Bearer <jwt>`；`alg` 为 `HS256` 或 `RS256`；常数时间比较签名；校验 `exp`/`nbf` 声明（过期/未生效拒绝） |

### JWT 说明

- **HS256**：手写 SHA-256/HMAC（`src/util/sha256.cpp`，单测覆盖 RFC 4231 向量），无第三方依赖
- **RS256**：OpenSSL `EVP_DigestVerify` 验签；`--auth-jwt-public-key` 可加载 PEM 公钥（`BEGIN PUBLIC KEY`）或 x509 证书（`BEGIN CERTIFICATE`，自动 `X509_get_pubkey`）
- 不支持 `alg` 为 `none` 或其他算法

### HTTPS 说明

- 开启后同一端口自动切换 TLS（HTTP/1.1 over TLS）
- 证书为 PEM 格式，支持证书链（`SSL_CTX_use_certificate_chain_file`）
- 未编译 OpenSSL（`MCP_WITH_OPENSSL=OFF`）时 `--tls-*` 参数启动报错退出
- 客户端访问：`https://<host>:<port>/mcp`，自签证书需客户端关闭校验或导入证书

## 安全建议

1. 公网 / 跨网络访问时务必启用 `--auth-token` 或 JWT，并配合 HTTPS
2. 云手机内网部署建议绑定内网地址 `--bind 192.168.x.x`，避免 0.0.0.0 暴露
3. JWT 生产环境建议 RS256 + 短有效期（`exp`），密钥通过密钥管理服务下发
4. 修改默认端口 8080，降低扫描命中率
