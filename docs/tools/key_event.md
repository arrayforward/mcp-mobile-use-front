# key_event（back / home / menu）

向云手机发送系统按键事件，共 3 个工具（命名与参考项目 `mcp_server_mobile_use` 对齐）：

| 工具 | 键值 | 说明 |
|---|---|---|
| `back` | 4 (`KEYCODE_BACK`) | 返回键 |
| `home` | 3 (`KEYCODE_HOME`) | HOME 键 |
| `menu` | 82 (`KEYCODE_MENU`) | 菜单键 |

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本结果，如 `Send back key event successfully`；失败时 `isError=true`。

## 底层实现

`input keyevent <keyCode>`，键值表与参考项目 `consts.go` 的 `AndroidKeyEventMap` 一致。

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "back", "arguments": {}}
}
```
