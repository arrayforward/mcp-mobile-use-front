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

## 与 mcp-proxy 网关集成（JWT 公钥验签）

部署在 `mcp-proxy`（`D:\agent\mcp-proxy`，云手机 MCP 代理网关）之后时，
Agent 与云机内的 mcp_mobile_use 不直接通信，请求经网关转发，**mcp_mobile_use 收到的
是网关签发的 JWT**，因此必须能验签网关签发的令牌。

### 集成要求：mcp 必须拿到 mcp-proxy 的公钥

> **鉴权前提：mcp_mobile_use（云机内）需要获取 mcp-proxy 的公钥**，用于验签网关下发的
> `Authorization: Bearer <JWT>`，公钥与私钥为 RSA 非对称密钥对（RS256）。

```
Agent ──10s临时token──▶ mcp-proxy 网关
Agent ◀─RS256 JWT(uid+instanceId)──
Agent ──Bearer JWT──▶ mcp-proxy ──转发──▶ mcp_mobile_use（云机内）
                                              │ 用 mcp-proxy 公钥验签 JWT
                                              ▼
                                         Protocol::handleMessage
```

公钥获取与配置方式（任选其一）：

| 方式 | 说明 |
|---|---|
| JWKS 端点 | 网关提供 `GET /jwks` 返回 RSA 公钥（JWK），运维/Agent 拉取后导出 PEM 交给云机 |
| 文件下发 | 将网关导出的 PEM 公钥（`BEGIN PUBLIC KEY`）预置到云机镜像/配置，启动参数指定路径 |

启动方式：

```bash
# 方式一：PEM 公钥文件
mcp_mobile_use -t http -p 8080 \
  --auth-jwt-public-key /data/local/tmp/mcp-proxy-public.pem

# 方式二：x509 证书（网关证书含公钥）
mcp_mobile_use -t http -p 8080 \
  --auth-jwt-public-key /data/local/tmp/mcp-proxy.crt
```

> 说明：`--auth-jwt-public-key` 同时支持 PEM 公钥与 x509 证书文件（自动提取公钥），
> 需要 OpenSSL 构建（`MCP_WITH_OPENSSL=ON`）。

### 网关 JWT 载荷（mcp-proxy `JwtService` 签发）

```json
{
  "sub": "user-10001",
  "uid": "user-10001",
  "instanceId": "Ab3xYz9p",
  "iat": 1722700000,
  "exp": 1722701800,
  "jti": "uuid"
}
```

mcp_mobile_use 验签时自动校验 `exp`/`nbf`；如需校验 `instanceId` 归属
（请求路径与 JWT 归属一致），可由网关层完成（mcp-proxy 已实现双重校验），
云机内仅做签名与有效期校验。

### 网关密钥现状与演进

| 阶段 | 网关签名算法 | 云机 mcp_mobile_use 配置 |
|---|---|---|
| 当前（mcp-proxy v1.2） | HS256 共享密钥（`security.jwt.secret`，默认 `mcp-proxy-dev-secret-key-0123456789abcdef`） | `--auth-jwt-secret <同一密钥>` |
| 目标（推荐） | RS256 非对称密钥对 | `--auth-jwt-public-key <网关公钥>`（需网关改造为 RS256 并暴露公钥/JWKS） |

> 网关未提供公钥端点前，可先用 HS256 共享密钥打通（`--auth-jwt-secret` 与网关
> `security.jwt.secret` 保持一致）；网关支持 RS256 + JWKS 后切换到公钥验签，
> 公钥通过 JWKS 或文件下发获取。

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
