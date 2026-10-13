/*
 * ========================================================================
 * 模块：api / server
 * ========================================================================
 * 作用：HTTP 服务实现
 */
#include "api/server.h"

#include <httplib.h>

#include <cstdio>
#include <utility>

#include "api/serialize.h"
#include "api/snapshot_store.h"
#include "api/sse_hub.h"

namespace perfstat::api {
namespace {

// SSE 心跳间隔；超过此时长无帧则发注释行，防中间层掐断空闲连接
constexpr int kSseIdleTimeoutMs = 15000;

}  // namespace

HttpServer::HttpServer(ServerOptions options, SnapshotStore* store, SseHub* hub)
    : options_(std::move(options)), store_(store), hub_(hub) {
    server_ = std::make_unique<httplib::Server>();
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::register_routes() {
    /*
     * ========================================================================
     * 步骤1：鉴权前置钩子
     * ========================================================================
     * 说明：命中即短路，未授权不进任何路由
     */
    server_->set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
            if (authorized(req)) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            res.status = 401;
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return httplib::Server::HandlerResponse::Handled;
        });

    /*
     * ========================================================================
     * 步骤2：一次性快照接口
     * ========================================================================
     */
    server_->Get("/api/processes", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(serialize_processes(store_->get()), "application/json");
    });

    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        (void)this;
        res.set_content("{\"ok\":true}", "application/json");
    });

    /*
     * ========================================================================
     * 步骤3：SSE 实时流
     * ========================================================================
     * 说明：
     *   1) 订阅得到独立队列，回调里阻塞等待，超时发心跳
     *   2) 订阅者对象由 shared_ptr 捕获，生命周期覆盖整个连接
     */
    server_->Get("/api/realtime", [this](const httplib::Request&, httplib::Response& res) {
        auto sub = hub_->subscribe();

        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider(
            "text/event-stream",
            [sub](std::size_t, httplib::DataSink& sink) -> bool {
                std::string frame;
                const int result = sub->wait_pop(&frame, kSseIdleTimeoutMs);

                // 3.1 有帧 -> 按 SSE 协议包装后写出
                if (result == 1) {
                    // 帧体为单行 JSON（dump 不产生裸换行），可直接作 data 字段
                    std::string chunk;
                    chunk.reserve(frame.size() + 8);
                    chunk += "data: ";
                    chunk += frame;
                    chunk += "\n\n";
                    return sink.write(chunk.data(), chunk.size());
                }
                // 3.2 已关闭 -> 结束流
                if (result < 0) {
                    return false;
                }
                // 3.3 超时 -> 心跳注释行
                static const char kPing[] = ": ping\n\n";
                return sink.write(kPing, sizeof(kPing) - 1);
            },
            [this, sub](bool /*success*/) { hub_->unsubscribe(sub->id); });
    });
}

bool HttpServer::authorized(const httplib::Request& req) const {
    /*
     * ========================================================================
     * 步骤1：token 校验
     * ========================================================================
     * 来源优先级：
     *   1) header X-Token  —— fetch 类请求
     *   2) query   ?t=     —— EventSource 不能自定义 header
     */
    if (options_.token.empty()) {
        return true;
    }

    const auto it = req.headers.find("X-Token");
    if (it != req.headers.end() && it->second == options_.token) {
        return true;
    }
    return req.get_param_value("t") == options_.token;
}

bool HttpServer::bind(std::string* err) {
    /*
     * ========================================================================
     * 步骤1：注册路由
     * ========================================================================
     */
    register_routes();

    /*
     * ========================================================================
     * 步骤2：逐端口尝试绑定
     * ========================================================================
     */
    for (int offset = 0; offset < options_.port_scan_limit; ++offset) {
        const int port = options_.port + offset;
        if (server_->bind_to_port(options_.host.c_str(), port)) {
            bound_port_ = port;
            return true;
        }
    }

    if (err != nullptr) {
        *err = "端口绑定失败：" + options_.host + ":" + std::to_string(options_.port) + " 起 " +
               std::to_string(options_.port_scan_limit) + " 个端口均被占用";
    }
    return false;
}

void HttpServer::start() {
    if (running_) {
        return;
    }
    running_ = true;
    thread_ = std::thread([this] { server_->listen_after_bind(); });

    std::fprintf(stderr, "[PerfStat][api] 监听 http://%s:%d\n", options_.host.c_str(),
                 bound_port_);
}

void HttpServer::stop() {
    if (!running_) {
        return;
    }
    server_->stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_ = false;
}

}  // namespace perfstat::api
