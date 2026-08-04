# NDK 交叉编译 mcp_mobile_use（Windows PowerShell）
# 用法: powershell -File scripts/build_native.ps1 [-NdkVersion 26.1.10909125] [-WithOpenSSL]
param(
    [string]$NdkVersion = "26.1.10909125",
    [string]$Abi = "arm64-v8a",
    [int]$Platform = 26,
    [switch]$WithOpenSSL,
    [string]$OpenSSLRoot = "third_party/openssl/$Abi"
)

$ErrorActionPreference = "Stop"

$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$cmake = "$sdk\cmake\3.22.1\bin\cmake.exe"
$ndk = "$sdk\ndk\$NdkVersion"

if (-not (Test-Path $cmake)) { throw "cmake not found: $cmake" }
if (-not (Test-Path $ndk)) { throw "ndk not found: $ndk" }

$args = @(
    "-B", "build-android",
    "-G", "Ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake",
    "-DANDROID_ABI=$Abi",
    "-DANDROID_PLATFORM=android-$Platform",
    "-DCMAKE_BUILD_TYPE=Release"
)
if ($WithOpenSSL) {
    $args += "-DMCP_WITH_OPENSSL=ON"
    $args += "-DOPENSSL_ROOT_DIR=$OpenSSLRoot"
}

& $cmake @args .
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build build-android
exit $LASTEXITCODE
