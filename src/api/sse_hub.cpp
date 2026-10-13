/*
 * ========================================================================
 * 模块：api / sse_hub
 * ========================================================================
 * 作用：SSE 订阅者队列与广播实现
 */
#include "api/sse_hub.h"

#include <chrono>
#include <utility>

namespace perfstat::api {

int SseSubscriber::wait_pop(std::string* out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mu_);

    /*
     * ========================================================================
     * 步骤1：等到有帧、被关闭，或超时
     * ========================================================================
     */
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                 [this] { return closed_ || !queue_.empty(); });

    /*
     * ========================================================================
     * 步骤2：按状态返回 —— 1 有帧 / 0 超时 / -1 已关闭
     * ========================================================================
     */
    if (!queue_.empty()) {
        *out = std::move(queue_.front());
        queue_.pop_front();
        return 1;
    }
    return closed_ ? -1 : 0;
}

void SseSubscriber::push(std::string frame) {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) {
        return;
    }

    /*
     * ========================================================================
     * 步骤1：队列限长，慢消费者丢最老帧
     * ========================================================================
     * 说明：丢帧优于阻塞采集线程；前端每帧都是全量快照，丢帧不丢状态
     */
    if (queue_.size() >= kMaxQueuedFrames) {
        queue_.pop_front();
    }
    queue_.push_back(std::move(frame));
    cv_.notify_one();
}

void SseSubscriber::close() {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
    cv_.notify_all();
}

std::shared_ptr<SseSubscriber> SseHub::subscribe() {
    auto sub = std::make_shared<SseSubscriber>();

    std::lock_guard<std::mutex> lock(mu_);
    sub->id = next_id_++;
    subs_[sub->id] = sub;
    return sub;
}

void SseHub::unsubscribe(std::uint64_t id) {
    std::shared_ptr<SseSubscriber> sub;

    /*
     * ========================================================================
     * 步骤1：从表里摘除，再关闭
     * ========================================================================
     * 说明：先摘除避免广播并发访问；close 在锁外做，避免持锁唤醒
     */
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = subs_.find(id);
        if (it == subs_.end()) {
            return;
        }
        sub = it->second;
        subs_.erase(it);
    }
    sub->close();
}

void SseHub::broadcast(const std::string& frame) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& entry : subs_) {
        entry.second->push(frame);
    }
}

std::size_t SseHub::subscriber_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return subs_.size();
}

}  // namespace perfstat::api
