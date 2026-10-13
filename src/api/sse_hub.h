/*
 * ========================================================================
 * 模块：api / sse_hub
 * ========================================================================
 * 作用：SSE 订阅者管理与广播
 * 依据：docs/DESIGN.md 5（SSE 通道）/ D14
 * 说明：
 *   - 每个订阅者持有独立阻塞队列，广播非阻塞（队列满丢最老帧）
 *   - 订阅者对象由 shared_ptr 持有：HTTP 回调可能晚于 handler 返回才结束
 */
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace perfstat::api {

class SseSubscriber {
public:
    // 阻塞取一帧；返回 1=取到帧，0=超时，-1=已关闭
    int wait_pop(std::string* out, int timeout_ms);

    // 投递一帧（非阻塞，队列满丢最老）
    void push(std::string frame);

    void close();

    std::uint64_t id{0};

private:
    static constexpr std::size_t kMaxQueuedFrames = 8;

    std::mutex              mu_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    bool                    closed_{false};
};

class SseHub {
public:
    std::shared_ptr<SseSubscriber> subscribe();
    void unsubscribe(std::uint64_t id);

    // 采集线程调用：向全部订阅者投递同一帧
    void broadcast(const std::string& frame);

    std::size_t subscriber_count() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::uint64_t, std::shared_ptr<SseSubscriber>> subs_;
    std::uint64_t      next_id_{1};
};

}  // namespace perfstat::api
