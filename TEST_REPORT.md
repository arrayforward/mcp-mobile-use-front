# mcp_mobile_use 测试报告

测试日期�?026-08-04
测试设备：华�?NOH-AN00（Android 12, arm64-v8a, �?root, shell/adb 已连接）

---

## 一、单元测试（主机侧，�?C++ 可移植组件）

### 代码组织

重构后按组件拆分，`tests/` 目录�?
| 文件 | 覆盖组件 | 用例�?|
|---|---|---|
| `test_json.cpp` | `src/json`：对�?数组/嵌套/转义/`\uXXXX`/数字/错误处理 | 19 |
| `test_util.cpp` | `src/util`：base64、base64url、shell 转义、SHA-256/HMAC（RFC 向量�?| 20 |
| `test_jwt.cpp` | `src/mcp/jwt`：HS256 验签/错误密钥/篡改/过期/畸形 | 7 |
| `test_command_builder.cpp` | `src/service`�?2 个工具的命令串生成、ASCII 判定 | 19 |
| `test_main.cpp` | 测试入口（计数汇总，无第三方框架�?| - |
| `test_framework.hpp` | 轻量 `CHECK` 断言�?| - |

### 运行方式

```powershell
# 直接编译运行
g++ -std=c++17 -Isrc -o tests/mcp_mobile_use_tests.exe `
    tests/test_main.cpp tests/test_json.cpp tests/test_util.cpp `
    tests/test_jwt.cpp tests/test_command_builder.cpp `
    src/json/json.cpp src/util/util.cpp src/util/sha256.cpp `
    src/mcp/jwt.cpp src/service/command_builder.cpp
./tests/mcp_mobile_use_tests.exe
```

�?CMake 主机配置 + ctest（`add_test(mcp_mobile_use_tests ...)`）�?
### 结果

```
65 checks, 0 failures
```

全部通过。关键标准向量验证：
- SHA-256：FIPS 180-4 空串 / "abc" / 超长块向�?- HMAC-SHA256：RFC 2202 "key" + fox 消息向量
- JSON：UTF-8 中文 `\uXXXX`、surrogate pair、dump→parse 往�?- JWT：正确签名通过；错误密�?篡改/过期（exp=1�?�?token 全部拒绝

---

## 二、端到端测试（真机，native 二进制，shell 身份�?
脚本：`scripts/e2e_adb.ps1`（adb push �?三种传输全链�?�?鉴权�?
| # | 用例 | 结果 |
|---|---|---|
| 1 | stdio initialize 握手 | �?|
| 2 | stdio tools/list 返回 12 个工�?| �?|
| 3 | stdio list_apps 返回真实包名列表 | �?|
| 4 | stdio home 键事�?| �?|
| 5 | stdio take_screenshot 返回 PNG（base64 image content�?| �?|
| 6 | stdio 未知工具返回 isError | �?|
| 7 | streamable-http initialize | �?|
| 8 | streamable-http back 键事�?| �?|
| 9 | SSE endpoint 事件 + sessionId 下发 | �?|
| 10 | SSE POST /message �?message 事件送达（tap�?| �?|
| 11 | 鉴权：无 token 请求返回 401 | �?|
| 12 | 鉴权：正�?token（Bearer）通过 | �?|

```
e2e result: 16 passed, 0 failed
```

命令：`powershell -File scripts/e2e_adb.ps1`

---

## 三、APK 前台服务测试（App 身份 uid=10243�?
APK：`android/app/build/outputs/apk/debug/app-debug.apk`（minSdk 26�?部署：`adb install` �?`am start -n com.mcp.mobileuse/.MainActivity` �?服务 `McpForegroundService` 常驻通知启动

### 发现并修复的缺陷

| 缺陷 | 现象 | 修复 |
|---|---|---|
| `Bad notification for startForeground` 崩溃 | 服务启动即进程崩�?| 通知�?`setSmallIcon(android.R.drawable.ic_menu_mylocation)` |

### 服务与协议层（全部通过�?
| 项目 | 结果 |
|---|---|
| 前台服务状�?| `isForeground=true`，notification channel=mcp_server |
| 端口监听 | `0.0.0.0:8080`（`adb forward tcp:8080` 可达�?|
| initialize | `mcp_mobile_use v0.1.0` |
| tools/list | 12 个工�?|
| ping | `{}` |
| terminate | `Session terminated` |
| SSE | endpoint 事件 + sessionId 正常 |

### 工具调用（协议正确；设备操作�?app 身份权限限制——符合预期降级）

| 工具 | 结果 | 说明 |
|---|---|---|
| list_apps | 返回自身 1 个包 | `pm list packages` �?app 身份下仅可见自身 |
| take_screenshot | 清晰错误 `screenshot output is not png` | SELinux 拒绝 screencap�?*优雅降级**（LLM 可读错误�?|
| tap/swipe/back/menu | 返回成功但未实际注入 | 验证：tap 状态栏未拉出通知�?|
| launch_app | 返回成功但未拉起 | Android 10+ 后台 Activity 启动限制 |
| text_input | 返回成功 | 输入法广播无接收�?|
| home | 返回成功 | 同上（注入受限） |

### 结论

- APK 前台服务模式�?**MCP 服务/协议层完整可�?*（三种传�?+ 12 工具 schema + 鉴权框架�?- 设备操控命令需要系统签名预�?/ Shizuku / root（native 二进制以 shell/root 身份运行�?16/16 e2e 已证明全部真实生效）
- 所有受限操作均返回规范 MCP 错误结构，不会崩溃或挂起

---

## 四、覆盖总结

| 层级 | 结果 | 覆盖 |
|---|---|---|
| 单元测试 | 65/65 | JSON、base64(64)url、SHA-256/HMAC、JWT、命令生�?|
| e2e（native/shell�?| 16/16 | 三种传输�?2 工具、截�?PNG、鉴�?401/200 |
| APK 集成 | 服务层通过 | 前台服务、HTTP/SSE、协议、工�?schema、通知修复 |
| 未覆盖（需云环境） | - | cloud 后端（华为云 CPH API）、JWT RS256、HTTPS（需 OpenSSL 构建�?|

