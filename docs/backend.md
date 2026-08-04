# 执行后端（backend 参数）

每个工具调用都可以通过 `arguments.backend` 选择命令的执行通道：

| backend | 说明 |
|---|---|
| `adb` | **默认**。在服务所在设备本机直接执行 shell 命令（等价于 `adb shell` 里的命令，但不经过 adb 传输层） |
| `cloud` | 通过华为云 CPH 的[同步执行 adb 命令 - RunSyncCommand](https://support.huaweicloud.com/api-cph/cph_api_0537.html) API 下发命令 |

不传入 `backend` 时使用服务端启动参数 `--backend`（或 `-b`）指定的默认值，默认为 `adb`。

## adb 后端

- 零配置，开箱即用
- 命令在设备本机 fork/exec 执行，支持二进制输出（截图原始 PNG）
- 权限取决于服务进程的运行身份：
  - root（系统服务模式）：全部命令可用
  - shell（`adb shell` 直跑）：`input`、`screencap`、`pm`、`am`、`monkey`、`wm` 均可用
  - 普通应用（APK 前台服务模式）：大部分 shell 命令不可用，建议以 shell/root 身份部署

## cloud 后端

通过 HTTPS 调用华为云 CPH API：

```
POST https://{CPH_ENDPOINT}/v1/{CPH_PROJECT_ID}/cloud-phone/phones/sync-commands
{"command": "shell", "content": "<shell命令>", "phone_ids": ["<CPH_PHONE_ID>"]}
```

### 环境变量配置（必需）

| 变量 | 说明 |
|---|---|
| `CPH_ENDPOINT` | CPH 服务地址，如 `cph.cn-north-4.myhuaweicloud.com` |
| `CPH_PROJECT_ID` | 华为云项目 ID |
| `CPH_PHONE_ID` | 云手机 ID |
| `CPH_TOKEN` | 方式一：IAM 用户 Token（`X-Auth-Token`） |
| `CPH_AK` / `CPH_SK` | 方式二：AK/SK，服务内使用 `SDK-HMAC-SHA256` 签名（OpenSSL HMAC） |

两种方式任选其一，同时存在时优先 `CPH_TOKEN`。

> cloud 后端需要编译时开启 OpenSSL 支持：
> `scripts/build_openssl.sh` 编译 OpenSSL 后，以 `MCP_WITH_OPENSSL=ON` 重新构建。
> 未开启时使用 cloud 后端会返回明确错误提示。

### 限制（来自华为云 API 约束）

- `execute_msg` 最长 1024 字节 → **cloud 后端的 `take_screenshot` 不返回内联图片**，截图保存到设备 `/sdcard/.mcp_mobile_use_shot.png` 并返回路径与分辨率
- 命令 content 最大 2048 字节，且字符集受限（不支持中文等非 ASCII 字符）→ `text_input` 传入中文时 cloud 后端可能失败
- 每个云手机命令执行超时约 2 秒，接口整体不超 30 秒 → `install_app` 等长耗时操作建议用 adb 后端
- 1 分钟内每用户限调用 6 次
