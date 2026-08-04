# terminate

结束当前 mobile use 会话。

## 参数

无业务参数（仅接受通用 `backend` 参数，忽略）。

## 返回

文本结果：`Session terminated`。

## 说明

与参考项目 `mcp_server_mobile_use` 的 `terminate` 工具对齐，供 Agent 在任务完成时显式结束会话。本服务为无状态实现，调用后服务本身继续运行，仅作为会话语义标记。

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "terminate", "arguments": {}}
}
```
