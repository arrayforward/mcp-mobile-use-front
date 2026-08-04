# tap

在屏幕指定坐标点击。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `x` | number | 是 | 点击点 x 坐标 |
| `y` | number | 是 | 点击点 y 坐标 |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud`，见[后端说明](../backend.md) |

## 返回

文本结果：`Tap the screen successfully at (x, y)`；失败时 `isError=true` 并返回错误描述。

## 底层实现

- adb 后端：`input tap <x> <y>`
- cloud 后端：同一命令经华为云 CPH `RunSyncCommand` 下发

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "tap", "arguments": {"x": 540, "y": 1200}}
}
```
