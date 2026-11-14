#include "core/thread_pool.hpp"
#include <algorithm>
#include <type_traits>
namespace miniaccel::internal {
static_assert(std::is_nothrow_move_constructible<Status>::value,
              "Failure delivery must not allocate when moving Status");
unsigned ThreadPool::default_workers() noexcept { return std::max(1u, std::min(4u, std::thread::hardware_concurrency())); }
ThreadPool::ThreadPool(size_t workers, size_t capacity) : queue_(capacity) {
    if (!workers || workers > 1024) throw std::invalid_argument("invalid worker count");
    try {
        workers_.reserve(workers);
        for (size_t i = 0; i < workers; ++i) workers_.emplace_back([this] {
            for (;;) {
                auto item = queue_.pop();
                if (!item.ok()) break;
                auto job = std::move(item).value();
                try { job.work(); }
                catch (...) {
                    try { job.failure(std::move(job.failure_status)); }
                    catch (...) { failure_callback_errors_.fetch_add(1); }
                }
            }
        });
    } catch (...) { queue_.close(); for (auto& worker : workers_) worker.join(); throw; }
}
ThreadPool::~ThreadPool() noexcept { shutdown(); }
Status ThreadPool::submit(Job job) {
    if (!job.work || !job.failure) return {StatusCode::InvalidArgument, "work and failure handler required"};
    return queue_.push(std::move(job));
}
void ThreadPool::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    queue_.close();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}
}
