# 推送并在设备上启动 mcp_mobile_use
# 用法: powershell -File scripts/deploy.ps1 [-Transport http] [-Port 8080] [-Backend adb]
param(
    [string]$Transport = "http",
    [int]$Port = 8080,
    [string]$Backend = "adb"
)

$ErrorActionPreference = "Stop"
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { $adb = "adb" }

& $adb push build-android/mcp_mobile_use /data/local/tmp/mcp_mobile_use
& $adb shell "chmod 755 /data/local/tmp/mcp_mobile_use"
& $adb shell "pkill -f mcp_mobile_use" 2>$null

if ($Transport -eq "stdio") {
    Write-Output "binary deployed. run: adb shell /data/local/tmp/mcp_mobile_use -t stdio"
} else {
    & $adb shell "sh -c 'nohup /data/local/tmp/mcp_mobile_use -t $Transport -p $Port -b $Backend >/data/local/tmp/mcp.log 2>&1 &'"
    Start-Sleep 1
    & $adb forward tcp:$Port tcp:$Port | Out-Null
    & $adb shell "cat /data/local/tmp/mcp.log"
    Write-Output "deployed: http://127.0.0.1:$Port (adb forward)"
}
