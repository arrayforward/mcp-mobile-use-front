# 测试方案

> 最新完整测试结果见 [TEST_REPORT.md](../TEST_REPORT.md)（单元测�?65 �?+ 真机 e2e 12 �?+ APK 集成测试）�?
## 单元测试（主机侧�?
### 范围与实�?
测试文件位于 `tests/`，按组件拆分，不依赖任何第三方测试框架（`test_framework.hpp` 提供轻量 `CHECK` 宏）�?
| 文件 | 覆盖组件 | 用例 |
|---|---|---|
| `test_json.cpp` | `src/json` | 解析/序列�?转义（`\uXXXX` 中文�?数字/错误处理 |
| `test_util.cpp` | `src/util` | base64、base64url、shellQuote、SHA-256/HMAC（RFC 向量�?|
| `test_jwt.cpp` | `src/mcp/jwt` | HS256 验签、错误密�?篡改/过期/畸形拒绝 |
| `test_command_builder.cpp` | `src/service` | 12 个工具的命令串生成、ASCII 判定 |
| `test_main.cpp` | - | 汇总入口与统计输出 |

POSIX 相关组件（`LocalExecutor` fork/exec、`HttpServer` socket）无法在 Windows 主机运行，由 e2e 测试在真机覆盖（见下）�?
### 运行

```powershell
# 直接 g++ 编译运行
g++ -std=c++17 -Isrc -o tests/mcp_mobile_use_tests.exe `
    tests/test_main.cpp tests/test_json.cpp tests/test_util.cpp `
    tests/test_jwt.cpp tests/test_command_builder.cpp `
    src/json/json.cpp src/util/util.cpp src/util/sha256.cpp `
    src/mcp/jwt.cpp src/service/command_builder.cpp
./tests/mcp_mobile_use_tests.exe
# 输出: 65 checks, 0 failures
```

也可通过 CMake 主机目标 + ctest 运行（非 Android 工具链配置时自动包含该目标）�?
## e2e 端到端测试（真机/云手机）

### 实现

脚本：`scripts/e2e_adb.ps1`，通过本机 adb 连接的设备执行全链路验证�?
1. push 二进制到 `/data/local/tmp`
2. **stdio 传输**：管道输�?7 �?JSON-RPC 消息（initialize �?initialized 通知 �?tools/list �?list_apps �?home �?take_screenshot �?未知工具），逐项断言响应
3. **streamable-http**：设备后台启�?`-t http`，`adb forward` �?POST `/mcp` 验证 initialize �?back
4. **SSE**：curl 长连�?`GET /sse` 收取 `endpoint` 事件 �?提取 sessionId �?POST `/message` �?断言响应�?SSE `message` 事件送达
5. **healthz 探活**：GET `/healthz` 返回 ok
6. **keep-alive 复用**：单连接连续 3 个请�?`num_connects` �?1,0,0
7. **多路并发**�? 个并行请求全�?200
8. **鉴权**：以 `--auth-token` 重启，断言�?token 请求返回 401、带 `Bearer` token 返回 200
9. 清理设备进程与临时文�?
�?15 项断言�?
### 运行

```powershell
powershell -File scripts/e2e_adb.ps1
# 输出示例:
#   PASS: stdio initialize
#   PASS: stdio tools/list has 12 tools
#   PASS: stdio list_apps returns packages
#   PASS: stdio key_event_home ok
#   PASS: stdio take_screenshot returns png
#   PASS: stdio unknown tool isError
#   PASS: streamable initialize
#   PASS: streamable key_event_back
#   PASS: sse endpoint event
#   PASS: sse message delivery (tap)
#   PASS: auth rejects missing token
#   PASS: auth accepts valid token
#   e2e result: 15 passed, 0 failed
```

### 已验证环�?
- 华为 NOH-AN00（Android 12, arm64-v8a, shell 身份�?root）：12/12 通过
- 截图返回真实 PNG（base64 image content），list_apps 返回真实包名列表

## APK 集成测试

- 安装 `app-debug.apk`（minSdk 26）到真机，前台服�?+ JNI 加载核心�?- 协议层（initialize/tools/list/ping/terminate/SSE）全部通过
- 已修复：`Bad notification for startForeground` 崩溃（补 `setSmallIcon`�?- 设备操作命令在普�?app 身份下受权限限制（预期降级，详见 TEST_REPORT.md 第三节）

