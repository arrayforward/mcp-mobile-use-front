/**
 * @file jni_bridge.cpp
 * @brief Android JNI 桥接层——在 Android 应用内以 so 形式启动/停止 MCP 服务
 *
 * 功能：
 *   导出 Java_com_mcp_mobileuse_JniBridge_nativeStart / nativeStop 两个
 *   JNI 函数，供 Android 端 JniBridge 类调用，在 App 进程内启动与桌面版
 *   相同的 HTTP MCP 服务（不含 stdio 传输）。
 *
 * 开发思路：
 *   1. 服务器与传输对象用匿名命名空间全局持有，gStarted 保证
 *      nativeStart 幂等（重复调用直接返回成功）；
 *   2. backend 参数经 GetStringUTFChars/ReleaseStringUTFChars 严格配对
 *      转换与释放，避免 JNI 局部引用泄漏；
 *   3. 监听固定 0.0.0.0，鉴权交由 McpHttpTransport 默认配置
 *      （生产部署应通过环境变量开启 auth）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include <jni.h>

#include <string>

#include "../mcp/http_transport.hpp"
#include "../net/http_server.hpp"
#include "../service/cloud_config.hpp"
#include "../service/provider.hpp"
#include "../tool/base.hpp"

namespace {

/** @brief 全局 HTTP 服务器实例（进程内单例） */
net::HttpServer gServer;
/** @brief 全局 MCP HTTP 传输层（路由注册与请求分发） */
mcp::McpHttpTransport gTransport;
/** @brief 服务是否已启动标志（保证 start/stop 幂等） */
bool gStarted = false;

}  // namespace

extern "C" {

/**
 * @brief 启动内嵌 MCP HTTP 服务
 * @param env     JNI 环境
 * @param jclass  Java 类引用（未使用）
 * @param port    监听端口
 * @param backend 默认执行后端字符串（"adb"/"cloud"，可为 nullptr，缺省 adb）
 * @return 0 成功（含已启动的幂等返回）；-1 监听失败
 *
 * 伪代码：
 *   1. 已启动则直接返回 0（幂等）；
 *   2. jstring -> C 字符串（用完即释放）解析后端枚举；
 *   3. 初始化工具层 provider -> 注册 MCP 路由 -> 监听 0.0.0.0:port；
 *   4. 置位 gStarted，返回 0；监听失败返回 -1。
 */
JNIEXPORT jint JNICALL Java_com_mcp_mobileuse_JniBridge_nativeStart(JNIEnv* env, jclass,
                                                                    jint port, jstring backend) {
    // 幂等保护：服务已在运行则直接视为成功
    if (gStarted) return 0;

    // JNI 字符串转换：backend 可为空，缺省按 "adb" 处理
    const char* backendStr = backend ? env->GetStringUTFChars(backend, nullptr) : nullptr;
    service::Backend b =
        service::backendFromString(backendStr ? backendStr : "adb", service::Backend::Adb);
    if (backendStr) env->ReleaseStringUTFChars(backend, backendStr);

    // 初始化工具层全局 provider 并注册路由、启动监听
    tool::initProvider(b, service::CloudConfig::fromEnv());
    gTransport.registerRoutes(gServer);
    if (!gServer.start("0.0.0.0", port)) return -1;
    gStarted = true;
    return 0;
}

/**
 * @brief 停止内嵌 MCP HTTP 服务
 * @param env    JNI 环境（未使用）
 * @param jclass Java 类引用（未使用）
 *
 * 伪代码：未启动直接返回；否则停止 HTTP 服务器并清除启动标志。
 */
JNIEXPORT void JNICALL Java_com_mcp_mobileuse_JniBridge_nativeStop(JNIEnv*, jclass) {
    if (!gStarted) return;
    gServer.stop();
    gStarted = false;
}

}  // extern "C"
