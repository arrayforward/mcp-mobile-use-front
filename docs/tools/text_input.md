# text_input

在当前焦点位置输入文本。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | string | 是 | 要输入的文本 |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本结果：`Input text successfully`；失败时 `isError=true`。

## 实现策略（优先广播 + 降级）

1. **纯 ASCII 可打印字符**（含空格）：直接使用 `input text`（空格转义为 `%s`，`%` 转义为 `%25`），所有设备可靠可用
2. **非 ASCII（如中文）**：优先通过云手机输入法广播注入：
   ```
   am broadcast -a device.gameservice.keyevent.value --es value '<text>'
   ```
   该方式与参考项目 mcp_server_mobile_use 一致，要求云手机预装配套输入法（ADBKeyBoard 方案）；广播失败时降级 `input text`

## 注意

- cloud 后端的命令 content 字符集受华为云 API 限制，**中文文本在 cloud 后端可能失败**，建议中文输入走 adb 后端
- 输入前需确保目标输入框已获取焦点（可先 `tap` 点击输入框）

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "text_input", "arguments": {"text": "hello world"}}
}
```
