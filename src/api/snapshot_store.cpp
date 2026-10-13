/*
 * ========================================================================
 * 模块：api / snapshot_store
 * ========================================================================
 * 作用：最新快照的线程安全存取
 */
#include "api/snapshot_store.h"

#include <utility>

namespace perfstat::api {

void SnapshotStore::update(Snapshot snapshot) {
    /*
     * ========================================================================
     * 步骤1：整帧替换
     * ========================================================================
     */
    std::lock_guard<std::mutex> lock(mu_);
    latest_ = std::move(snapshot);
    valid_ = true;
}

Snapshot SnapshotStore::get() const {
    /*
     * ========================================================================
     * 步骤1：返回一份拷贝，读方不持锁使用
     * ========================================================================
     */
    std::lock_guard<std::mutex> lock(mu_);
    return latest_;
}

bool SnapshotStore::has_data() const {
    std::lock_guard<std::mutex> lock(mu_);
    return valid_;
}

}  // namespace perfstat::api
