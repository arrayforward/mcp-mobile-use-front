#!/bin/sh
# NDK 交叉编译 mcp_mobile_use（Linux/macOS）
# 用法: sh scripts/build_native.sh [ndk_path] [abi] [platform]
set -e

SDK="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
NDK="${1:-$SDK/ndk/26.1.10909125}"
ABI="${2:-arm64-v8a}"
PLATFORM="${3:-26}"
CMAKE="$SDK/cmake/3.22.1/bin/cmake"

if [ ! -x "$CMAKE" ]; then CMAKE=cmake; fi

"$CMAKE" -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$PLATFORM" \
    -DCMAKE_BUILD_TYPE=Release \
    .
"$CMAKE" --build build-android
