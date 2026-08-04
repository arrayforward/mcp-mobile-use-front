# list_apps

列出设备上已安装的应用。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `include_system` | boolean | 否 | 是否包含系统应用，默认 `false`（只列第三方应用） |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本 JSON：

```json
{
  "apps": [
    {"package_name": "com.tencent.mm"},
    {"package_name": "com.dianping.v1"}
  ],
  "count": 2
}
```

## 底层实现

- 第三方应用：`pm list packages -3`
- 包含系统应用：`pm list packages`

输出按行解析 `package:<包名>`。

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "list_apps", "arguments": {"include_system": false}}
}
```
