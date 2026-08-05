# mcp_mobile_use e2e 测试脚本（通过本机 adb 连接的云手机/真机执行）
# 用法: powershell -File scripts/e2e_adb.ps1 [-Port 8080]
param(
    [int]$Port = 8080
)

$ErrorActionPreference = "Stop"
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { $adb = "adb" }
$bin = "build-android/mcp_mobile_use"
$tmp = "$env:TEMP\mcp_mobile_use_e2e"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$global:passed = 0
$global:failed = 0

function Check($name, $cond) {
    if ($cond) { $global:passed++; Write-Output "PASS: $name" }
    else { $global:failed++; Write-Output "FAIL: $name" }
}

Write-Output "==> push binary"
& $adb push $bin /data/local/tmp/mcp_mobile_use | Out-Null
& $adb shell "chmod 755 /data/local/tmp/mcp_mobile_use"

# 停止可能占用端口的 APK 前台服务（com.mcp.mobileuse）
& $adb shell "am force-stop com.mcp.mobileuse" 2>$null

Write-Output "==> stdio transport"
$req = @'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"e2e","version":"1.0"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_apps","arguments":{}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"home","arguments":{}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"take_screenshot","arguments":{}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"unknown_tool","arguments":{}}}
{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"adb_shell","arguments":{"command":"echo hello_mcp && exit 42"}}}
'@
$reqFile = "$tmp/req_stdio.txt"
$req | Out-File -FilePath $reqFile -Encoding ascii -NoNewline
& $adb push $reqFile /data/local/tmp/req_stdio.txt | Out-Null
$resp = & $adb shell "cat /data/local/tmp/req_stdio.txt | /data/local/tmp/mcp_mobile_use -t stdio 2>/dev/null"
$respText = $resp -join "`n"

Check "stdio initialize" ($respText -match '"id":1,"result":\{"protocolVersion"')
Check "stdio tools/list has 13 tools" ($respText -match '"name":"' -or ($respText | Select-String '"name":"').Matches.Count -ge 13 -or $respText -match '"adb_shell"')
Check "stdio list_apps returns packages" ($respText -match '"package_name"')
Check "stdio key_event_home ok" ($respText -match 'Send home key event successfully')
Check "stdio take_screenshot returns png" ($respText -match '"type":"image","data":"iVBORw0KGgo')
Check "stdio unknown tool isError" ($respText -match 'unknown tool')
Check "stdio adb_shell exit_code" ($respText -match 'exit_code\\":42' -and $respText -match 'hello_mcp')

Write-Output "==> http transport (streamable + sse)"
& $adb shell "pkill -f mcp_mobile_use; am force-stop com.mcp.mobileuse" 2>$null
Start-Sleep 1
& $adb shell "sh -c 'nohup /data/local/tmp/mcp_mobile_use -t http -p $Port >/data/local/tmp/mcp.log 2>&1 &'"
Start-Sleep 1
$started = & $adb shell "grep -q listening /data/local/tmp/mcp.log && echo ok || echo fail"
if ($started -notmatch "ok") { Write-Output "FAIL: server failed to start:"; & $adb shell "cat /data/local/tmp/mcp.log" }
& $adb forward tcp:$Port tcp:$Port | Out-Null

$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"e2e","version":"1.0"}}}'
try {
    $r = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType "application/json" -Body $init -TimeoutSec 10
    Check "streamable initialize" ($r.result.serverInfo.name -eq "mcp_mobile_use")
} catch { Check "streamable initialize" $false }

# healthz 探活接口
try {
    $h = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/healthz" -Method Get -TimeoutSec 10
    Check "healthz returns ok" ($h.status -eq "ok" -and $h.name -eq "mcp_mobile_use")
} catch { Check "healthz returns ok" $false }

# keep-alive: 单连接多请求复用（3 个请求 num_connects 依次为 1,0,0）
# 注意：curl 多 URL 时响应体与 -w 输出交错，num_connects 数字位于每行行尾，取行尾数字
$kaBody = Join-Path $tmp "ka_body.bin"
$kaOut = curl.exe -s -o $kaBody -w "%{num_connects}`n" http://127.0.0.1:$Port/healthz http://127.0.0.1:$Port/healthz http://127.0.0.1:$Port/healthz
$kaSeq = ($kaOut | ForEach-Object { if ($_ -match '(\d+)$') { $matches[1] } }) -join ","
Check "keep-alive reuses connection" ($kaSeq -eq "1,0,0")

# 多路并发: 8 个并行请求
$jobs = 1..8 | ForEach-Object { Start-Job { param($p) curl.exe -s -o NUL -w "%{http_code}" "http://127.0.0.1:$p/healthz" } -ArgumentList $Port }
$codes = $jobs | Wait-Job | Receive-Job
$jobs | Remove-Job
Check "concurrent connections" (($codes | Where-Object { $_ -eq "200" }).Count -eq 8)

$call = '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"back","arguments":{}}}'
try {
    $r = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType "application/json" -Body $call -TimeoutSec 10
    Check "streamable key_event_back" ($r.result.content[0].text -match "successfully")} catch { Check "streamable key_event_back" $false }

$sseFile = "$tmp/sse_out.txt"
Remove-Item $sseFile -ErrorAction SilentlyContinue
$sseJob = Start-Job { param($p, $f) curl.exe -s -N "http://127.0.0.1:$p/sse" 2>$null | Out-File -Encoding ascii $f } -ArgumentList $Port, $sseFile
Start-Sleep 2
$sid = $null
try { $sid = ((Get-Content $sseFile | Select-String "sessionId=").Line -split "=")[1] } catch {}
Check "sse endpoint event" ($null -ne $sid -and $sid.Length -gt 0)

if ($sid) {
    $tap = '{"jsonrpc":"2.0","id":99,"method":"tools/call","params":{"name":"tap","arguments":{"x":600,"y":800}}}'
    try {
        $r = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/message?sessionId=$sid" -Method Post -ContentType "application/json" -Body $tap -TimeoutSec 10
        Start-Sleep 2
        $sseContent = Get-Content $sseFile -Raw
        Check "sse message delivery (tap)" ($sseContent -match 'Tap the screen successfully')
    } catch { Check "sse message delivery (tap)" $false }
}
Stop-Job $sseJob; Remove-Job $sseJob

Write-Output "==> auth (token)"
# 确保旧进程完全退出、端口释放后再启动鉴权服务（避免竞态命中旧无鉴权实例）
& $adb shell "pkill -f mcp_mobile_use; am force-stop com.mcp.mobileuse" 2>$null
Start-Sleep 2
& $adb shell "sh -c 'nohup /data/local/tmp/mcp_mobile_use -t http -p $Port --auth-token e2e-secret >/data/local/tmp/mcp.log 2>&1 &'"
Start-Sleep 2
$authStarted = & $adb shell "grep -q listening /data/local/tmp/mcp.log && echo ok || echo fail"
if ($authStarted -notmatch "ok") { Write-Output "FAIL: auth server failed to start:"; & $adb shell "cat /data/local/tmp/mcp.log" }
$rejected = $false
for ($i = 0; $i -lt 3 -and -not $rejected; $i++) {
    try {
        $r = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType "application/json" -Body $init -TimeoutSec 10
        $rejected = ($r.StatusCode -eq 401)
    } catch {
        $rejected = ($_.Exception.Response -and $_.Exception.Response.StatusCode.value__ -eq 401)
    }
    if (-not $rejected) { Start-Sleep 1 }
}
Check "auth rejects missing token" $rejected
try {
    $r = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType "application/json" -Headers @{Authorization="Bearer e2e-secret"} -Body $init -TimeoutSec 10
    Check "auth accepts valid token" ($r.StatusCode -eq 200 -and $r.Content -match "mcp_mobile_use")
} catch { Check "auth accepts valid token" $false }

& $adb shell "pkill -f mcp_mobile_use; rm -f /data/local/tmp/req_stdio.txt" 2>$null

Write-Output ""
Write-Output "e2e result: $global:passed passed, $global:failed failed"
exit $(if ($global:failed -eq 0) { 0 } else { 1 })
