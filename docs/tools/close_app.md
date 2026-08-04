# close_app

按包名强制停止应用。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `package_name` | string | 是 | 应用包名 |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本结果：`Close app <pkg> successfully`；失败时 `isError=true`。

## 底层实现

`am force-stop '<pkg>'`

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "close_app", "arguments": {"package_name": "com.tencent.mm"}}
}
```
