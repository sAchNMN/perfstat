/*
 * ========================================================================
 * 模块：api / server
 * ========================================================================
 * 作用：HTTP 服务（cpp-httplib）：路由、鉴权、SSE 流
 * 依据：docs/DESIGN.md 5（前后端通信）/ 8（API 契约）
 * 说明：
 *   - 只绑 127.0.0.1，避免暴露到局域网
 *   - 端口被占用则自动 +1 避让
 *   - /api/* 全部要 token（header X-Token 或 query ?t=）
 */
#pragma once

#include <memory>
#include <string>
#include <thread>

namespace httplib {
class Server;
class Request;
class Response;
}  // namespace httplib

namespace perfstat::api {

class SnapshotStore;
class ServiceStore;
class SseHub;

}  // namespace perfstat::api

namespace perfstat::analyze {
class RuleDb;
}

namespace perfstat::api {

struct ServerOptions {
    std::string host{"127.0.0.1"};
    int         port{17321};
    std::string token;              // 空串表示不鉴权（仅调试用）
    int         port_scan_limit{20};
};

class HttpServer {
public:
    HttpServer(ServerOptions options, SnapshotStore* store, ServiceStore* service_store,
               SseHub* hub, analyze::RuleDb* rules);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /*
     * ====================================================================
     * 步骤：绑定端口
     * ====================================================================
     * 操作：从 options.port 起逐个尝试，首个可用者胜出
     */
    bool bind(std::string* err);

    // 起后台线程接受连接
    void start();
    void stop();

    int bound_port() const { return bound_port_; }

private:
    void register_routes();
    bool authorized(const httplib::Request& req) const;

    ServerOptions                    options_;
    SnapshotStore*                   store_{nullptr};
    ServiceStore*                    service_store_{nullptr};
    SseHub*                          hub_{nullptr};
    analyze::RuleDb*                 rules_{nullptr};
    std::unique_ptr<httplib::Server> server_;
    std::thread                      thread_;
    int                              bound_port_{0};
    bool                             running_{false};
};

}  // namespace perfstat::api
