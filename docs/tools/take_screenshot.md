# take_screenshot

截取云手机当前屏幕画面。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

两种后端返回形式不同：

### adb 后端（推荐）

直接返回 MCP image content，LLM 可直接"看图"：

```json
{
  "content": [{"type": "image", "data": "<base64 PNG>", "mimeType": "image/png"}],
  "isError": false
}
```

### cloud 后端

华为云 `RunSyncCommand` 的输出上限为 1024 字节，无法传回整张截图，
因此截图保存到设备 `/sdcard/.mcp_mobile_use_shot.png`，返回文本 JSON：

```json
{"device_path": "/sdcard/.mcp_mobile_use_shot.png", "width": 1080, "height": 2340}
```

## 底层实现

- adb：`screencap -p`（stdout 捕获原始 PNG）+ `wm size` 解析分辨率
- cloud：`screencap -p /sdcard/.mcp_mobile_use_shot.png && wm size`
