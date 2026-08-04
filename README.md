# mcp_mobile_use

运行在云手机 Android 环境内部的 **MCP（Model Context Protocol）服务**，提供与
[mcp_server_mobile_use](https://github.com/volcengine/volcengine-mcp-servers)（Go/火山引擎 ACEP）
对齐的云手机操控工具集。核心为 **C++17 零依赖实现**（手写 JSON / HTTP/SSE / SHA-256-HMAC），
设备操作**直接在本机执行 shell 命令**（`input`/`screencap`/`am`/`pm`），不依赖 adb 传输层与云厂商 API；
可选 **华为云 CPH `RunSyncCommand`** 作为远程执行后端，两种后端通过工具参数 `backend` 一键切换。

## 特性

- 12 个工具，与参考项目接口完全对齐（tap/swipe/take_screenshot/text_input/back/home/menu/
  launch_app/close_app/list_apps/autoinstall_app/terminate）
- 三种 MCP 传输：**stdio / sse / streamable-http**
- 双执行后端：**adb**（本机 shell，默认）/ **cloud**（华为云 CPH API），工具调用可选 `backend` 参数
- 截图 base64 内联返回（MCP image content），LLM 直接可读
- 部署双模式：**native 二进制**（root+签名→系统服务）/ **Java APK 前台服务**（JNI 加载同一核心库）
- 可选安全：静态 Token / JWT（HS256 手写、RS256 证书加载）/ HTTPS
- 中文文档、主机单测（63 项）+ 真机 e2e（12 项）

## 文档

- [接口文档（总览）](docs/README.md)
- [测试报告](TEST_REPORT.md)
- [架构设计](docs/architecture.md)
- [安全方案（鉴权 + HTTPS）](docs/security.md)
- [部署方案（系统服务 / 前台服务）](docs/deployment.md)
- [使用说明](docs/usage.md)
- [测试方案（单测 + e2e）](docs/testing.md)
- [扩展开发文档](docs/development.md)
- [传输方式](docs/transport.md)
- [执行后端](docs/backend.md)

## 快速开始

```powershell
# 1. 构建（NDK）
powershell -File scripts/build_native.ps1

# 2. 部署到云手机/真机并启动 HTTP
powershell -File scripts/deploy.ps1 -Transport http -Port 8080

# 3. e2e 验证
powershell -File scripts/e2e_adb.ps1
```

## 目录结构

```
src/           C++17 核心（json/util/net/mcp/service/tool + main + JNI 桥）
android/       Java 薄壳 APK 工程（minSdk 26）
scripts/       构建 / 部署 / e2e 脚本
tests/         主机侧单元测试
docs/          中文文档
```
