# 架构与 API 图表（PNG 导出）

> 本目录收录 [architecture.md](../architecture.md) 与 [api.md](../api.md) 中全部
> mermaid 图表的 PNG 导出版本（白底、2x 缩放），供文档审阅、汇报与离线阅读使用。
> 源图以文档内 mermaid 代码块为准；修改文档后可用 mermaid-cli 重新导出：
> `npx @mermaid-js/mermaid-cli -i <图.mmd> -o <图.png> -b white -s 2`
>
> 作者：hubin　　更新日期：2026-08-05

## 架构设计图（architecture.md，21 张）

| 文件 | 图名 | 章节 |
|---|---|---|
| [arch-01.png](arch-01.png) | 系统总体分层架构总图 | 2.1 架构总图 |
| [arch-02.png](arch-02.png) | JSON 模块类图（mj::Value / Parser） | 3.1 JSON 模块 |
| [arch-03.png](arch-03.png) | 手写密码学栈（sha256/bignum/rsa_verify） | 3.2 密码学工具 |
| [arch-04.png](arch-04.png) | 网络层类图（HttpServer/Io/Request/Response/HttpsClient） | 3.3 网络层 |
| [arch-05.png](arch-05.png) | JSON-RPC 协议分发与错误码流程 | 3.4 协议层 |
| [arch-06.png](arch-06.png) | 服务层调用与后端路由流程 | 3.5 服务层 |
| [arch-07.png](arch-07.png) | 线程模型时序（accept 线程 + 连接线程 + SSE 会话） | 4 线程模型 |
| [arch-08.png](arch-08.png) | stdio 传输时序 | 5.1 stdio 传输 |
| [arch-09.png](arch-09.png) | Streamable HTTP（POST /mcp）时序 | 5.2 Streamable HTTP |
| [arch-10.png](arch-10.png) | SSE 传输（GET /sse + POST /message）时序 | 5.3 SSE 传输 |
| [arch-11.png](arch-11.png) | 鉴权决策流程（None/Token/JWT） | 6.1 鉴权决策 |
| [arch-12.png](arch-12.png) | JWT 验签流程（HS256/RS256 双算法双引擎） | 6.2 JWT 验签 |
| [arch-13.png](arch-13.png) | RS256 公钥三种来源与 JWKS URL 拉取时序 | 6.3 公钥来源 |
| [arch-14.png](arch-14.png) | mcp-proxy 网关集成信任模型 | 6.3 网关集成 |
| [arch-15.png](arch-15.png) | LocalExecutor 进程模型（fork/pipe/poll/waitpid） | 7.1 LocalExecutor |
| [arch-16.png](arch-16.png) | CloudExecutor 调用流程（华为云签名下发） | 7.2 CloudExecutor |
| [arch-17.png](arch-17.png) | tap 工具端到端调用链时序 | 8 端到端流程 |
| [arch-18.png](arch-18.png) | 截图数据流（screencap → base64 → image content） | 8 截图数据流 |
| [arch-19.png](arch-19.png) | JNI 与 Android APK 架构 | 9 JNI/APK |
| [arch-20.png](arch-20.png) | 构建系统（CMake targets 与产物） | 10 构建系统 |
| [arch-21.png](arch-21.png) | 部署架构（开发调试 / 生产拓扑） | 11 部署架构 |

## API 设计图（api.md，10 张）

| 文件 | 图名 | 章节 |
|---|---|---|
| [api-01.png](api-01.png) | API 三个层次总览 | 1 API 总览 |
| [api-02.png](api-02.png) | Streamable HTTP 请求流程（含 401 分支） | 2.2 POST /mcp |
| [api-03.png](api-03.png) | SSE 传输流程（endpoint/message 事件） | 2.3 SSE |
| [api-04.png](api-04.png) | initialize 协商时序 | 4.1 initialize |
| [api-05.png](api-05.png) | 鉴权三种模式配置流程 | 5.1 鉴权模式 |
| [api-06.png](api-06.png) | 工具返回结构（成功/失败） | 6.2 返回结构 |
| [api-07.png](api-07.png) | 13 工具分类总览树 | 7.1 工具总览 |
| [api-08.png](api-08.png) | take_screenshot 截图与 LLM 视觉回路 | 7.4 截图工具 |
| [api-09.png](api-09.png) | 错误处理全景（传输/鉴权/协议/工具四层） | 8 错误处理 |
| [api-10.png](api-10.png) | 典型 Agent 操作循环 | 9.2 Agent 循环 |
