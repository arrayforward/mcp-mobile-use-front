# launch_app

按包名启动应用。

## 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `package_name` | string | 是 | 应用包名，如 `com.tencent.mm` |
| `backend` | string | 否 | 执行后端：`adb`（默认）/ `cloud` |

## 返回

文本结果：`Launch app <pkg> successfully`；失败时 `isError=true`。

## 底层实现

优先解析 launcher activity 精确启动，失败时用 `monkey` 兜底：

```sh
am start -n "$(cmd package resolve-activity --brief '<pkg>' | tail -n 1)" \
  || monkey -p '<pkg>' -c android.intent.category.LAUNCHER 1
```

## 示例

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "launch_app", "arguments": {"package_name": "com.tencent.mm"}}
}
```
