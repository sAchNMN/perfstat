/*
 * ========================================================================
 * 模块：api / server
 * ========================================================================
 * 作用：HTTP 服务实现
 */
#include "api/server.h"

#include <httplib.h>

#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "analyze/rule_db.h"
#include "analyze/usage_types.h"
#include "api/serialize.h"
#include "api/service_store.h"
#include "api/snapshot_store.h"
#include "api/sse_hub.h"
#include "store/db.h"
#include "store/store_types.h"

namespace perfstat::api {
namespace {

// SSE 心跳间隔；超过此时长无帧则发注释行，防中间层掐断空闲连接
constexpr int kSseIdleTimeoutMs = 15000;

// /api/analyze 返回的未命中清单上限（其余由前端按需展示）
constexpr std::size_t kPendingPreviewLimit = 50;

// 读取 query 参数并转 int64；缺失或非法时用 fallback
std::int64_t param_int64(const httplib::Request& req, const char* key, std::int64_t fallback) {
    const std::string value = req.get_param_value(key);
    if (value.empty()) {
        return fallback;
    }
    try {
        return static_cast<std::int64_t>(std::stoll(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

nlohmann::json series_json(const std::vector<store::SeriesPoint>& points) {
    nlohmann::json array = nlohmann::json::array();
    for (const store::SeriesPoint& p : points) {
        array.push_back(nlohmann::json{
            {"ts", p.bucket_ts},
            {"cpu", p.cpu_avg},
            {"cpu_max", p.cpu_max},
            {"mem_kb", p.mem_avg_kb},
            {"mem_max_kb", p.mem_max_kb},
            {"io_r_kb", p.io_read_kb},
            {"io_w_kb", p.io_write_kb},
        });
    }
    return array;
}

// 汇总榜行；rules 非空时附带用途字段，前端可直接显示
nlohmann::json entity_stats_json(const std::vector<store::EntityStatRow>& rows,
                                 const analyze::RuleDb* rules) {
    nlohmann::json array = nlohmann::json::array();
    for (const store::EntityStatRow& r : rows) {
        nlohmann::json item = {
            {"entity_id", r.entity_id},
            {"kind", r.kind},
            {"name", r.name},
            {"path", r.path},
            {"cpu_avg", r.cpu_avg},
            {"cpu_max", r.cpu_max},
            {"mem_avg_kb", r.mem_avg_kb},
            {"mem_max_kb", r.mem_max_kb},
            {"io_r_kb", r.io_read_kb},
            {"io_w_kb", r.io_write_kb},
            {"samples", r.samples},
            {"buckets", r.buckets},
        };
        if (rules != nullptr) {
            const analyze::UsageEntry usage = rules->resolve(r.kind, r.name, r.path);
            item["usage"] = nlohmann::json{
                {"category", usage.category},
                {"purpose", usage.purpose},
                {"suggestion", usage.suggestion},
                {"source", usage.source},
                {"known", usage.known},
            };
        }
        array.push_back(std::move(item));
    }
    return array;
}

}  // namespace

HttpServer::HttpServer(ServerOptions options, SnapshotStore* store, ServiceStore* service_store,
                       SseHub* hub, analyze::RuleDb* rules, store::Database* history_db)
    : options_(std::move(options)), store_(store), service_store_(service_store), hub_(hub),
      rules_(rules), history_db_(history_db) {
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
        res.set_content(serialize_processes(store_->get(), rules_), "application/json");
    });

    /*
     * ========================================================================
     * 步骤2.1：服务列表（含 D18 三态占用）
     * ========================================================================
     */
    server_->Get("/api/services", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(serialize_services(service_store_->get(), rules_), "application/json");
    });

    /*
     * ========================================================================
     * 步骤2.2：单服务详情（按服务名过滤）
     * ========================================================================
     */
    server_->Get("/api/services/(.+)", [this](const httplib::Request& req,
                                              httplib::Response& res) {
        const std::string name = req.matches[1];
        nlohmann::json result = nlohmann::json::array();
        for (const auto& s : service_store_->get().services) {
            if (s.name == name) {
                result.push_back(nlohmann::json{
                    {"name", s.name},
                    {"display", s.display_name},
                    {"state", s.state},
                    {"start_type", s.start_type},
                    {"host_pid", s.host_pid},
                    {"occ", s.occ_state},
                    {"cpu", s.cpu_percent},
                    {"mem_kb", s.working_set_bytes / 1024},
                    {"shared", s.shared_count},
                });
            }
        }
        if (result.empty()) {
            res.status = 404;
            res.set_content("{\"error\":\"service not found\"}", "application/json");
            return;
        }
        res.set_content(result.dump(), "application/json");
    });

    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        (void)this;
        res.set_content("{\"ok\":true}", "application/json");
    });

    /*
     * ========================================================================
     * 步骤2.3：用途库（M5）
     * ========================================================================
     * GET  -> 列出全部人工覆盖条目
     * POST -> 写入人工覆盖，落库并即时生效（source='manual'）
     * 说明：实体定位用 body 里的 kind/name/path，不用路径参数 ——
     *       进程路径含反斜杠与冒号，塞进 URL 既长又易被中间层改写
     */
    server_->Get("/api/usage", [this](const httplib::Request&, httplib::Response& res) {
        if (rules_ == nullptr) {
            res.status = 503;
            res.set_content("{\"error\":\"usage store unavailable\"}", "application/json");
            return;
        }
        nlohmann::json array = nlohmann::json::array();
        for (const store::UsageRow& row : rules_->list_overrides()) {
            array.push_back(nlohmann::json{
                {"kind", row.kind},
                {"name", row.name},
                {"path", row.path},
                {"category", row.category},
                {"purpose", row.purpose},
                {"suggestion", row.suggestion},
                {"source", row.source},
                {"updated_at", row.updated_at},
            });
        }
        res.set_content(array.dump(), "application/json");
    });

    server_->Post("/api/usage", [this](const httplib::Request& req, httplib::Response& res) {
        if (rules_ == nullptr) {
            res.status = 503;
            res.set_content("{\"error\":\"usage store unavailable\"}", "application/json");
            return;
        }

        // 2.3.1 解析并校验入参
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const std::exception&) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid json\"}", "application/json");
            return;
        }
        const std::string kind = body.value("kind", std::string("process"));
        const std::string name = body.value("name", std::string());
        const std::string path = body.value("path", std::string());
        if (name.empty() || (kind != "process" && kind != "service")) {
            res.status = 400;
            res.set_content("{\"error\":\"kind must be process|service, name required\"}",
                            "application/json");
            return;
        }

        // 2.3.2 落库 + 刷新内存
        std::string err;
        if (!rules_->set_manual(kind, name, path, body.value("category", std::string()),
                                body.value("purpose", std::string()),
                                body.value("suggestion", std::string()), &err)) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", err}}.dump(), "application/json");
            return;
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    /*
     * ========================================================================
     * 步骤2.4：批量识别（M5 版只跑本地库）
     * ========================================================================
     * 说明：对当前快照的全部进程与服务做一次本地匹配；
     *       未命中清单回流给前端，供人工补齐
     */
    server_->Post("/api/analyze", [this](const httplib::Request&, httplib::Response& res) {
        if (rules_ == nullptr) {
            res.status = 503;
            res.set_content("{\"error\":\"usage store unavailable\"}", "application/json");
            return;
        }

        // 2.4.1 汇总当前快照里的全部实体
        std::vector<analyze::EntityRef> entities;
        for (const ProcessSample& sample : store_->get().processes) {
            entities.push_back(analyze::EntityRef{"process", sample.name, sample.path});
        }
        for (const ServiceInfo& service : service_store_->get().services) {
            entities.push_back(analyze::EntityRef{"service", service.name, std::string()});
        }

        // 2.4.2 本地识别 + 未命中清单截断
        const analyze::AnalyzeResult result = rules_->analyze(entities);

        nlohmann::json pending = nlohmann::json::array();
        const std::size_t limit = std::min(kPendingPreviewLimit, result.pending.size());
        for (std::size_t i = 0; i < limit; ++i) {
            pending.push_back(nlohmann::json{
                {"kind", result.pending[i].kind},
                {"name", result.pending[i].name},
                {"path", result.pending[i].path},
            });
        }

        res.set_content(nlohmann::json{
                            {"total", result.total},
                            {"builtin", result.builtin_hits},
                            {"manual", result.manual_hits},
                            {"unknown", result.unknown},
                            {"pending_total", result.pending.size()},
                            {"pending", pending},
                        }.dump(),
                        "application/json");
    });

    /*
     * ========================================================================
     * 步骤2.5：时间段统计（M7 历史页）
     * ========================================================================
     * 参数：
     *   from / to  unix 秒；缺省为最近 1 小时
     *   bucket     时间桶秒数，缺省 60（前端按区间长短自动放宽）
     *   top        汇总榜条数，缺省 50
     *   entity_id  给出时只返回该实体的时间序列，汇总榜留空
     */
    server_->Get("/api/stats", [this](const httplib::Request& req, httplib::Response& res) {
        if (history_db_ == nullptr) {
            res.status = 503;
            res.set_content("{\"error\":\"history unavailable in --no-persist mode\"}",
                            "application/json");
            return;
        }

        // 2.5.1 解析区间与粒度
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t from_ts = param_int64(req, "from", now - 3600);
        const std::int64_t to_ts = param_int64(req, "to", now);
        std::int64_t bucket = param_int64(req, "bucket", 60);
        if (bucket < 1) {
            bucket = 60;
        }
        const auto top = static_cast<int>(param_int64(req, "top", 50));
        const std::int64_t entity_id = param_int64(req, "entity_id", 0);

        std::string err;
        nlohmann::json body = {
            {"from", from_ts},
            {"to", to_ts},
            {"bucket", bucket},
            {"entity_id", entity_id},
        };

        // 2.5.2 单实体序列与全系统汇总走两条路
        if (entity_id > 0) {
            std::vector<store::SeriesPoint> series;
            if (!history_db_->query_entity_series(entity_id, from_ts, to_ts, bucket, &series,
                                                  &err)) {
                res.status = 500;
                res.set_content(nlohmann::json{{"error", err}}.dump(), "application/json");
                return;
            }
            body["series"] = series_json(series);
            body["entities"] = nlohmann::json::array();
        } else {
            std::vector<store::EntityStatRow> stats;
            std::vector<store::SeriesPoint> series;
            if (!history_db_->query_entity_stats(from_ts, to_ts, top, &stats, &err) ||
                !history_db_->query_system_series(from_ts, to_ts, bucket, &series, &err)) {
                res.status = 500;
                res.set_content(nlohmann::json{{"error", err}}.dump(), "application/json");
                return;
            }
            body["entities"] = entity_stats_json(stats, rules_);
            body["series"] = series_json(series);
        }

        res.set_content(body.dump(), "application/json");
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
