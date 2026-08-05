# mcp_mobile_use 接口文档

`mcp_mobile_use` 是一个运行在云手机 Android 环境内部的 MCP（Model Context Protocol）服务，参考 `mcp_server_mobile_use`（Go 实现，基于火山引擎 ACEP 云 API）封装，提供云手机操控能力（13 个工具）。

与参考实现的关键差异：本服务**运行在设备内部**，设备操作默认直接执行本机 shell 命令（`input`、`screencap`、`am`、`pm` 等），不依赖 adb 传输层，也不依赖云厂商 API。

## 目录

- [架构设计](architecture.md)
- [安全方案（鉴权 + HTTPS）](security.md)
- [部署方案](deployment.md)
- [使用说明](usage.md)
- [测试方案](testing.md)（完整结果见 [TEST_REPORT.md](../TEST_REPORT.md)）
- [扩展开发文档](development.md)
- [传输方式](transport.md)：stdio / sse / streamable-http
- [执行后端（backend 参数）](backend.md)：adb（本机命令，默认）/ cloud（华为云 CPH API）
- 工具接口：
  - [tap](tools/tap.md)：点击屏幕
  - [swipe](tools/swipe.md)：滑动屏幕
  - [take_screenshot](tools/take_screenshot.md)：截图
  - [text_input](tools/text_input.md)：文本输入
  - [key_event](tools/key_event.md)：按键（back / home / menu）
  - [launch_app](tools/launch_app.md)：启动应用
  - [close_app](tools/close_app.md)：关闭应用
  - [list_apps](tools/list_apps.md)：应用列表
  - [autoinstall_app](tools/autoinstall_app.md)：安装应用
  - [adb_shell](tools/adb_shell.md)：标准 adb shell 命令
  - [terminate](tools/terminate.md)：结束会话

## 工具一览

| 工具 | 说明 | 底层命令（adb 后端） |
|---|---|---|
| `tap` | 坐标点击 | `input tap x y` |
| `swipe` | 坐标滑动 | `input swipe x1 y1 x2 y2 duration` |
| `take_screenshot` | 截图（base64 内联返回） | `screencap -p` |
| `text_input` | 文本输入（支持中文） | 输入法广播 / `input text` |
| `back` | 返回键 | `input keyevent 4` |
| `home` | HOME 键 | `input keyevent 3` |
| `menu` | 菜单键 | `input keyevent 82` |
| `launch_app` | 启动应用 | `monkey -p <pkg>` / `am start` |
| `close_app` | 强制停止应用 | `am force-stop <pkg>` |
| `list_apps` | 列出已安装应用 | `pm list packages` |
| `autoinstall_app` | 下载并安装 APK | `curl`/`wget` + `pm install` |
| `adb_shell` | 执行标准 adb shell 命令 | `sh -c <command>`（通用接口） |
| `terminate` | 结束会话 | - |

## 通用参数

所有工具都接受一个可选参数 `backend`：

| 值 | 说明 |
|---|---|
| `adb`（默认） | 在设备本机直接执行 shell 命令 |
| `cloud` | 通过华为云 CPH `RunSyncCommand` API 下发命令执行 |

不传入时使用服务端启动参数 `--backend` 指定的默认值（默认 `adb`）。

## 调用示例（streamable-http）

```bash
curl -X POST http://<device>:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
      "name": "tap",
      "arguments": {"x": 540, "y": 1200, "backend": "adb"}
    }
  }'
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [{"type": "text", "text": "Tap the screen successfully at (540, 1200)"}],
    "isError": false
  }
}
```
