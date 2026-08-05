# 使用说明

## 构建

### native 二进制 + JNI 库（Windows）

```powershell
powershell -File scripts/build_native.ps1
# 产物:
#   build-android/mcp_mobile_use           (arm64 可执行文件)
#   build-android/libmcp_mobile_use_jni.so (APK 用 JNI 库)
```

可选参数：`-NdkVersion 26.1.10909125`、`-Abi arm64-v8a`、`-WithOpenSSL`（cloud 后端需要）。

Linux/macOS：`sh scripts/build_native.sh`。

### cloud 后端（可选）

```bash
sh scripts/build_openssl.sh ~/openssl-3.0.13   # 编译 OpenSSL 到 third_party/openssl
powershell -File scripts/build_native.ps1 -WithOpenSSL
```

### APK

```bat
scripts\build_apk.bat
:: 或用 Android Studio 打开 android\ 目录构建
:: 产物: android\app\build\outputs\apk\debug\app-debug.apk（minSdk 26）
```

## 运行

### 方式一：adb 直跑（调试用）

```powershell
powershell -File scripts/deploy.ps1 -Transport http -Port 8080
# 等价于:
#   adb push build-android/mcp_mobile_use /data/local/tmp/
#   adb shell /data/local/tmp/mcp_mobile_use -t http -p 8080
#   adb forward tcp:8080 tcp:8080
```

### 方式二：系统服务（root + 签名）

见[部署方案](deployment.md)。

### 方式三：APK 前台服务

安装 APK → 打开 App → 设置端口和后端 → Start。服务以常驻通知的前台服务运行，HTTP 端口对外开放。

## 启动参数

```
mcp_mobile_use [options]
  -t, --transport <type>   stdio | sse | streamable-http | http（默认 stdio）
  -p, --port <port>        HTTP 监听端口（默认 8080）
  -b, --backend <backend>  默认执行后端 adb | cloud（默认 adb）
      --bind <addr>        监听地址（默认 0.0.0.0）
      --auth-token <token>        静态 token 鉴权（默认关闭）
      --auth-jwt-secret <s>       JWT HS256 鉴权
      --auth-jwt-public-key <pem> JWT RS256 鉴权（PEM 公钥/证书）
      --tls-cert <pem>            启用 HTTPS（服务器证书）
      --tls-key <pem>             HTTPS 私钥
```

鉴权与 HTTPS 的完整说明见 [安全方案](security.md)；环境变量（`MCP_AUTH_TOKEN`、
`MCP_JWT_SECRET`、`MCP_JWT_PUBLIC_KEY`）与命令行等价。

## MCP 客户端接入

### stdio（如 Claude Desktop / opencode）

```json
{
  "mcpServers": {
    "mobile_use": {
      "command": "adb",
      "args": ["shell", "/data/local/tmp/mcp_mobile_use", "-t", "stdio"]
    }
  }
}
```

### streamable-http

```
URL: http://127.0.0.1:8080/mcp   （adb forward 后）
```

### sse

```
URL: http://127.0.0.1:8080/sse
```

## 环境变量（cloud 后端）

| 变量 | 说明 |
|---|---|
| `CPH_ENDPOINT` | 如 `cph.cn-north-4.myhuaweicloud.com` |
| `CPH_PROJECT_ID` | 华为云项目 ID |
| `CPH_PHONE_ID` | 云手机 ID |
| `CPH_TOKEN` | IAM Token（优先） |
| `CPH_AK` / `CPH_SK` | AK/SK 签名（Token 缺省时使用） |

鉴权相关环境变量：`MCP_AUTH_TOKEN` / `MCP_JWT_SECRET` / `MCP_JWT_PUBLIC_KEY`
（与命令行参数等价，见[安全方案](security.md)）。

## mcp-proxy 网关集成（公钥验签）

部署在 `D:\agent\mcp-proxy` 网关之后时，**mcp_mobile_use 必须获取 mcp-proxy 的公钥**
来验签网关下发的 JWT：

```bash
# 1) 获取公钥：网关 JWKS 端点或运维下发 PEM 公钥文件
# 2) 配置到云机
mcp_mobile_use -t http -p 8080 --auth-jwt-public-key /data/local/tmp/mcp-proxy-public.pem
```

网关当前为 HS256 共享密钥时：`--auth-jwt-secret <与网关 security.jwt.secret 相同的密钥>`。
详见[安全方案](security.md)「与 mcp-proxy 网关集成」。

## 工具调用示例

```bash
# 截图（返回 base64 图片）
curl -X POST http://127.0.0.1:8080/mcp -H "Content-Type: application/json" -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"take_screenshot","arguments":{}}}'

# 指定 cloud 后端执行
curl -X POST http://127.0.0.1:8080/mcp -H "Content-Type: application/json" -d '{
  "jsonrpc":"2.0","id":2,"method":"tools/call",
  "params":{"name":"list_apps","arguments":{"backend":"cloud"}}}'
```

完整工具列表见 [docs/README.md](README.md)。
