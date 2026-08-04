#include <jni.h>

#include <string>

#include "../mcp/http_transport.hpp"
#include "../net/http_server.hpp"
#include "../service/cloud_config.hpp"
#include "../service/provider.hpp"
#include "../tool/base.hpp"

namespace {

net::HttpServer gServer;
mcp::McpHttpTransport gTransport;
bool gStarted = false;

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_com_mcp_mobileuse_JniBridge_nativeStart(JNIEnv* env, jclass,
                                                                    jint port, jstring backend) {
    if (gStarted) return 0;

    const char* backendStr = backend ? env->GetStringUTFChars(backend, nullptr) : nullptr;
    service::Backend b =
        service::backendFromString(backendStr ? backendStr : "adb", service::Backend::Adb);
    if (backendStr) env->ReleaseStringUTFChars(backend, backendStr);

    tool::initProvider(b, service::CloudConfig::fromEnv());
    gTransport.registerRoutes(gServer);
    if (!gServer.start("0.0.0.0", port)) return -1;
    gStarted = true;
    return 0;
}

JNIEXPORT void JNICALL Java_com_mcp_mobileuse_JniBridge_nativeStop(JNIEnv*, jclass) {
    if (!gStarted) return;
    gServer.stop();
    gStarted = false;
}

}  // extern "C"
