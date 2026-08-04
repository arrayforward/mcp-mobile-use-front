# 测试方案

## 单元测试（主机侧�?
### 范围与实�?
测试文件：`tests/test_main.cpp`，不依赖任何测试框架，自实现 `CHECK` 宏计数断言，覆�?*可移植纯 C++ 组件**�?
| 测试函数 | 覆盖组件 | 用例 |
|---|---|---|
| `testJsonBasic` | `src/json` | 对象/数组/嵌套解析、`at()`/`has()`、缺省�?|
| `testJsonEscapes` | `src/json` | `\n \t \" \\` 转义、`\uXXXX`（中文）、dump→parse 往�?|
| `testJsonNumbers` | `src/json` | 负数/�?浮点/科学计数法、整�?dump 无小数点 |
| `testJsonDumpStructure` | `src/json` | 序列化结构保�?|
| `testJsonErrors` | `src/json` | 非法 JSON、尾部垃圾字符抛 `ParseError` |
| `testBase64` | `src/util` | RFC 4648 标准用例、填充、二进制全字节往�?|
| `testShellQuote` | `src/util` | 单引号转义（�?shell 注入�?|
| `testCommandBuilder` | `src/service` | 12 个工具的命令串生成、ASCII 判定 |

POSIX 相关组件（`LocalExecutor` fork/exec、`HttpServer` socket）无法在 Windows 主机运行，由 e2e 测试在真机覆盖（见下）�?
### 运行

```powershell
# 直接 g++ 编译运行
g++ -std=c++17 -Isrc -o tests/mcp_mobile_use_tests.exe `
    tests/test_main.cpp src/json/json.cpp src/util/util.cpp src/service/command_builder.cpp
./tests/mcp_mobile_use_tests.exe
# 输出: 63 checks, 0 failures
```

也可通过 CMake 主机目标 + ctest 运行（非 Android 工具链配置时自动包含该目标）�?
## e2e 端到端测试（真机/云手机）

### 实现

脚本：`scripts/e2e_adb.ps1`，通过本机 adb 连接的设备执行全链路验证�?
1. push 二进制到 `/data/local/tmp`
2. **stdio 传输**：管道输�?7 �?JSON-RPC 消息（initialize �?initialized 通知 �?tools/list �?list_apps �?key_event_home �?take_screenshot �?未知工具），逐项断言响应
3. **streamable-http**：设备后台启�?`-t http`，`adb forward` �?POST `/mcp` 验证 initialize �?key_event_back
4. **SSE**：curl 长连�?`GET /sse` 收取 `endpoint` 事件 �?提取 sessionId �?POST `/message` �?断言响应�?SSE `message` 事件送达
5. 清理设备进程与临时文�?
�?10 项断言�?
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
#   e2e result: 12 passed, 0 failed
```

### 已验证环�?
- 华为 NOH-AN00（Android 12, arm64-v8a, shell 身份�?root）：10/10 通过
- 截图返回真实 PNG（base64 image content），list_apps 返回真实包名列表

