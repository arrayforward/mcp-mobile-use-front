#!/bin/sh
# 编译 OpenSSL (arm64-v8a) 供 cloud 后端使用，产物安装到 third_party/openssl/arm64-v8a
# 用法: sh scripts/build_openssl.sh [openssl_src_dir] [ndk_path]
# 需要先下载 OpenSSL 源码: https://github.com/openssl/openssl/releases (建议 3.0.x)
set -e

OPENSSL_SRC="${1:-openssl-3.0.13}"
SDK="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
NDK="${2:-$SDK/ndk/26.1.10909125}"
OUT="$(pwd)/third_party/openssl/arm64-v8a"

export ANDROID_NDK_HOME="$NDK"
export PATH="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"

cd "$OPENSSL_SRC"
./Configure android-arm64 -D__ANDROID_API__=26 no-shared no-tests
make -j"$(nproc)" build_libs
mkdir -p "$OUT/lib" "$OUT/include"
cp libssl.a libcrypto.a "$OUT/lib/"
cp -r include/openssl "$OUT/include/"
echo "OpenSSL installed to $OUT"
echo "重新编译: sh scripts/build_native.sh 并加 -DMCP_WITH_OPENSSL=ON -DOPENSSL_ROOT_DIR=$OUT"
