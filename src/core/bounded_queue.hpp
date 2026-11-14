#pragma once
#include "miniaccel/status.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace miniaccel::internal {
template<class T> class BoundedQueue {
    static_assert(std::is_nothrow_move_constructible<T>::value, "Queue values must move without throwing");
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) { if (!capacity) throw std::invalid_argument("zero queue capacity"); }
    Status push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return {StatusCode::Closed, "queue closed"};
        if (queue_.size() == capacity_) return {StatusCode::QueueFull, "queue full"};
        try { queue_.push_back(std::move(value)); } catch (const std::bad_alloc&) { return {StatusCode::OutOfMemory, "queue allocation failed"}; }
        cv_.notify_one();
        return {};
    }
    Result<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return Status{StatusCode::Closed, "queue drained"};
        T item = std::move(queue_.front()); queue_.pop_front(); return item;
    }
    void close() noexcept { std::lock_guard<std::mutex> lock(mutex_); closed_ = true; cv_.notify_all(); }
    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.size(); }
private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    bool closed_{false};
};
}
