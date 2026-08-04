# swipe

在屏幕上从一点滑动到另一点。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `from_x` | number | 是 | 起点 x 坐标 |
| `from_y` | number | 是 | 起点 y 坐标 |
| `to_x` | number | 是 | 终点 x 坐标 |
| `to_y` | number | 是 | 终点 y 坐标 |
| `duration_ms` | number | 否 | 滑动时长（毫秒），默认 300 |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本结果：`Swipe the screen successfully`；失败时 `isError=true`。

## 底层实现

`input swipe <from_x> <from_y> <to_x> <to_y> <duration_ms>`

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "swipe",
    "arguments": {"from_x": 540, "from_y": 1800, "to_x": 540, "to_y": 600, "duration_ms": 300}
  }
}
```
