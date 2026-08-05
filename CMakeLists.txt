cmake_minimum_required(VERSION 3.22)
project(mcp_mobile_use CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MCP_WITH_OPENSSL "Enable Huawei cloud backend via OpenSSL (TLS + signing)" OFF)

set(MCP_CORE_SOURCES
    src/json/json.cpp
    src/util/util.cpp
    src/util/sha256.cpp
    src/util/bignum.cpp
    src/util/rsa_verify.cpp
    src/service/command_builder.cpp
    src/service/cloud_config.cpp
    src/service/local_executor.cpp
    src/service/cloud_executor.cpp
    src/service/hw_signer.cpp
    src/service/provider.cpp
    src/net/http_server.cpp
    src/net/https_client.cpp
    src/mcp/protocol.cpp
    src/mcp/auth.cpp
    src/mcp/jwt.cpp
    src/mcp/stdio_server.cpp
    src/mcp/http_transport.cpp
    src/tool/base.cpp
    src/tool/registry.cpp
    src/tool/adb_shell.cpp
    src/tool/screen_tap.cpp
    src/tool/screen_swipe.cpp
    src/tool/take_screenshot.cpp
    src/tool/text_input.cpp
    src/tool/key_event.cpp
    src/tool/launch_app.cpp
    src/tool/close_app.cpp
    src/tool/list_app.cpp
    src/tool/install_app.cpp
    src/tool/terminate.cpp
)

add_library(mcp_mobile_use_core STATIC ${MCP_CORE_SOURCES})
target_include_directories(mcp_mobile_use_core PUBLIC src)

if(MCP_WITH_OPENSSL)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(mcp_mobile_use_core PUBLIC MCP_WITH_OPENSSL=1)
    target_link_libraries(mcp_mobile_use_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

if(ANDROID)
    add_library(mcp_mobile_use_jni SHARED src/android/jni_bridge.cpp)
    target_link_libraries(mcp_mobile_use_jni PRIVATE mcp_mobile_use_core log)

    add_executable(mcp_mobile_use src/main.cpp)
    target_link_libraries(mcp_mobile_use PRIVATE mcp_mobile_use_core)
else()
    add_executable(mcp_mobile_use_tests
        tests/test_main.cpp
        tests/test_json.cpp
        tests/test_util.cpp
        tests/test_jwt.cpp
        tests/test_command_builder.cpp
        src/json/json.cpp
        src/util/util.cpp
        src/util/sha256.cpp
        src/util/bignum.cpp
        src/util/rsa_verify.cpp
        src/mcp/jwt.cpp
        src/service/command_builder.cpp
    )
    # URL 公钥拉取的 mock 测试依赖 net/auth 模块（POSIX socket），Windows 主机跳过
    if(NOT WIN32)
        target_sources(mcp_mobile_use_tests PRIVATE
            tests/test_url_fetch.cpp
            src/mcp/auth.cpp
            src/net/http_server.cpp
            src/net/https_client.cpp
        )
        target_compile_definitions(mcp_mobile_use_tests PRIVATE MCP_WITH_MOCK_HTTP_TEST)
        target_link_libraries(mcp_mobile_use_tests PRIVATE pthread)
    else()
        target_sources(mcp_mobile_use_tests PRIVATE tests/test_url_fetch.cpp)
    endif()
    target_include_directories(mcp_mobile_use_tests PRIVATE src)
    enable_testing()
    add_test(NAME mcp_mobile_use_tests COMMAND mcp_mobile_use_tests)
endif()
