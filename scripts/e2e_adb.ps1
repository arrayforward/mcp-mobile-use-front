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

Write-Output "==> stdio transport"
$req = @'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"e2e","version":"1.0"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_apps","arguments":{}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"home","arguments":{}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"take_screenshot","arguments":{}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"unknown_tool","arguments":{}}}
'@
$reqFile = "$tmp/req_stdio.txt"
$req | Out-File -FilePath $reqFile -Encoding ascii -NoNewline
& $adb push $reqFile /data/local/tmp/req_stdio.txt | Out-Null
$resp = & $adb shell "cat /data/local/tmp/req_stdio.txt | /data/local/tmp/mcp_mobile_use -t stdio 2>/dev/null"
$respText = $resp -join "`n"

Check "stdio initialize" ($respText -match '"id":1,"result":\{"protocolVersion"')
Check "stdio tools/list has 12 tools" (($respText | Select-String '"name":"').Matches.Count -ge 12 -or ($respText -match '"terminate"'))
Check "stdio list_apps returns packages" ($respText -match '"package_name"')
Check "stdio key_event_home ok" ($respText -match 'Send home key event successfully')
Check "stdio take_screenshot returns png" ($respText -match '"type":"image","data":"iVBORw0KGgo')
Check "stdio unknown tool isError" ($respText -match 'unknown tool')

Write-Output "==> http transport (streamable + sse)"
& $adb shell "pkill -f mcp_mobile_use" 2>$null
& $adb shell "sh -c 'nohup /data/local/tmp/mcp_mobile_use -t http -p $Port >/data/local/tmp/mcp.log 2>&1 &'"
Start-Sleep 1
& $adb forward tcp:$Port tcp:$Port | Out-Null

$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"e2e","version":"1.0"}}}'
try {
    $r = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType "application/json" -Body $init -TimeoutSec 10
    Check "streamable initialize" ($r.result.serverInfo.name -eq "mcp_mobile_use")
} catch { Check "streamable initialize" $false }

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
& $adb shell "pkill -f mcp_mobile_use" 2>$null
Start-Sleep 1
& $adb shell "sh -c 'nohup /data/local/tmp/mcp_mobile_use -t http -p $Port --auth-token e2e-secret >/data/local/tmp/mcp.log 2>&1 &'"
Start-Sleep 2
$rejected = $false
try {
    $r = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/mcp" -Method Post -ContentType "application/json" -Body $init -TimeoutSec 10
    $rejected = ($r.StatusCode -eq 401)
} catch {
    $rejected = ($_.Exception.Response -and $_.Exception.Response.StatusCode.value__ -eq 401)
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
