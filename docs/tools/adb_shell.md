# adb_shell

标准的 adb shell 接口：在设备上执行任意 shell 命令并返回输出（对齐 `adb shell <command>`）。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `command` | string | 是 | 要执行的 shell 命令，如 `ls -l /sdcard`、`getprop ro.build.version.release` |
| `timeout_ms` | number | 否 | 命令超时（毫秒），默认 30000 |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本 JSON，包含退出码与完整输出：

```json
{
  "command": "echo hello && getprop ro.build.version.release",
  "exit_code": 0,
  "timed_out": false,
  "stdout": "hello\n12\n",
  "stderr": ""
}
```

- `exit_code`：命令退出码（信号杀死时为 -1）
- `timed_out`：是否超时被杀
- `stdout` / `stderr`：标准输出/错误完整捕获

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "adb_shell",
    "arguments": {"command": "dumpsys window | grep mCurrentFocus"}
  }
}
```

## 说明与安全

- 这是**通用底层接口**，常见操作（点击/滑动/截图/按键/应用管理）应优先使用专用工具（tap、swipe、take_screenshot 等）
- 可执行任意命令（管道、重定向、复合命令均可），**存在任意代码执行风险**：对外暴露时务必启用鉴权（`--auth-token` / JWT，见 [安全方案](../security.md)），并只对可信客户端开放
- 与专用工具共享同一执行后端（本地 shell / 华为云 CPH API）与超时/退出码语义
