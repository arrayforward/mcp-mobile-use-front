# 部署方案

mcp_mobile_use 支持三种部署方式，按设备权限从低到高排列。所有方式共享同一份 C++ 核心，能力一致。

## 方式一：adb 直跑（开发/调试）

无需安装，push 即用，进程身份为 `shell`：

```powershell
powershell -File scripts/deploy.ps1 -Transport http -Port 8080
```

- `input`/`screencap`/`pm`/`am`/`monkey`/`wm` 在 shell 身份下均可用
- 设备重启或进程被杀后需手动拉起

## 方式二：前台服务（无 root/签名，推荐常规使用）

安装 APK，服务以 Android 前台服务（常驻通知）运行：

```bat
scripts\build_apk.bat
adb install android\app\build\outputs\apk\debug\app-debug.apk
```

- 打开 App，设置端口（默认 8080）与默认后端（adb/cloud），点击 Start
- `McpForegroundService`（Java 薄壳）通过 JNI 加载 `libmcp_mobile_use_jni.so`，启动 HTTP（SSE + streamable-http 同端口）
- minSdk 26（Android 8.0）起
- 保活：前台服务 + START_STICKY；可在系统设置中为应用关闭电池优化进一步提升存活率
- 注意：普通应用身份无法直接执行 `input`/`screencap` 等 shell 命令，建议：
  - 云手机镜像预置时将 APK 加签为系统应用，或
  - 配合 Shizuku / root 提权，或
  - 使用方式三

## 方式三：系统服务（root + 平台签名）

### 3a. init.rc 系统服务（改镜像/刷机场景）

1. 编译后将二进制放入设备 `/system/bin/`（或 vendor 分区）：
   ```bash
   adb root && adb remount
   adb push build-android/mcp_mobile_use /system/bin/mcp_mobile_use
   adb shell chmod 755 /system/bin/mcp_mobile_use
   ```
2. 添加 init.rc 配置（如 `/system/etc/init/mcp_mobile_use.rc`）：
   ```rc
   service mcp_mobile_use /system/bin/mcp_mobile_use -t http -p 8080 -b adb
       class main
       user root
       group root
       seclabel u:r:su:s0
       oneshot disabled
   ```
   或在 `on property:sys.boot_completed=1` 中 `start mcp_mobile_use` 实现开机自启。
3. 按设备 sepolicy 情况可能需要补充 SELinux 规则（云手机镜像可关闭或内置）。

### 3b. Magisk 模块（已 root 设备）

打包为 Magisk 模块，通过 `service.sh` 开机拉起：

```
module/
├── module.prop
├── service.sh          # nohup /data/adb/modules/mcp_mobile_use/mcp_mobile_use -t http -p 8080 &
└── mcp_mobile_use      # 二进制
```

### 3c. 签名系统应用（有平台签名）

将 APK 以平台签名（platform key）签名并声明 `android:sharedUserId="android.uid.system"`，
放入 `/system/priv-app/` 预装。应用获得 system 身份后，前台服务模式即可执行全部 shell 命令。

## 外部访问

| 方式 | 命令 |
|---|---|
| adb forward | `adb forward tcp:8080 tcp:8080` → `http://127.0.0.1:8080` |
| 内网直连 | `http://<云手机内网 IP>:8080` |
| 公网 | 云手机绑定 EIP 后安全组放行 8080（**无鉴权，务必仅在可信网络暴露**） |

## 鉴权说明

默认不启用鉴权（便于测试）。生产暴露公网前务必启用：

```bash
# 静态 token
mcp_mobile_use -t http -p 8080 --auth-token '你的token'
# JWT + HTTPS
mcp_mobile_use -t http -p 8443 --auth-jwt-secret 'hs256密钥' --tls-cert server.crt --tls-key server.key
```

支持静态 Token / JWT（HS256、RS256，可加载 PEM 公钥或 x509 证书）/ HTTPS，
详见 [安全方案](security.md)。客户端请求需携带 `Authorization: <token>` 或 `Authorization: Bearer <jwt>`。
