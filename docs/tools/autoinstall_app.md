# autoinstall_app

从给定 URL 下载 APK 并一步安装。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `download_url` | string | 是 | APK 的 http(s) 下载地址 |
| `app_name` | string | 否 | 应用名（保留参数，与参考项目接口对齐） |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本结果：`Apk is installed successfully`；失败时 `isError=true`。

## 底层实现

```sh
(curl -fSL -o /data/local/tmp/mcp_mobile_use_install.apk '<url>') \
  || (wget -O /data/local/tmp/mcp_mobile_use_install.apk '<url>')
pm install -r /data/local/tmp/mcp_mobile_use_install.apk
rm -f /data/local/tmp/mcp_mobile_use_install.apk
```

- 依赖设备上存在 `curl` 或 `wget`
- `download_url` 必须为 http/https
- 安装结果检查输出中是否包含 `Success`
- 整体超时 300 秒

## 注意

- cloud 后端单命令执行超时约 2 秒，大 APK 不建议走 cloud 后端

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "autoinstall_app", "arguments": {"download_url": "https://example.com/app.apk"}}
}
```
