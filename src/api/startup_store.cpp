/*
 * ========================================================================
 * 模块：api / startup_store
 * ========================================================================
 * 作用：最新启动项清单的线程安全存取
 */
#include "api/startup_store.h"

#include <utility>

namespace perfstat::api {

void StartupStore::update(StartupSnapshot snapshot) {
    /*
     * ========================================================================
     * 步骤1：整帧替换
     * ========================================================================
     */
    std::lock_guard<std::mutex> lock(mu_);
    latest_ = std::move(snapshot);
    valid_ = true;
}

StartupSnapshot StartupStore::get() const {
    /*
     * ========================================================================
     * 步骤1：返回一份拷贝，读方不持锁使用
     * ========================================================================
     */
    std::lock_guard<std::mutex> lock(mu_);
    return latest_;
}

bool StartupStore::has_data() const {
    std::lock_guard<std::mutex> lock(mu_);
    return valid_;
}

}  // namespace perfstat::api
