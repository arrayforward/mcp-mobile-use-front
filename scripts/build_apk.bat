@echo off
REM 构建 APK：先确保 native 已编译（scripts\build_native.ps1），再打包
REM 需要在 Android Studio 中打开 android\ 目录，或本机已安装 gradle
setlocal

where gradle >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo gradle not found on PATH. Open android\ in Android Studio to build the APK.
    exit /b 1
)

if not exist build-android\libmcp_mobile_use_jni.so (
    echo jni lib missing, building native first...
    powershell -File scripts\build_native.ps1
    if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
)

cd android
gradle assembleDebug
echo APK: android\app\build\outputs\apk\debug\app-debug.apk
