#pragma once
#include "core/bounded_queue.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
namespace miniaccel::internal {
class ThreadPool {
public:
    using Work = std::function<void()>;
    using Failure = std::function<void(Status)>;
    struct Job {
        Work work;
        Failure failure;
        // Constructed before admission; moving it on the error path is noexcept.
        Status failure_status{StatusCode::InternalError, "worker task threw an exception"};
    };
    static unsigned default_workers() noexcept;
    ThreadPool(size_t workers, size_t capacity);
    ~ThreadPool() noexcept;
    Status submit(Job job);
    // A callback exception leaves that job's outcome to the callback's owner.
    // This counter is monotonic and can be checked after shutdown as well.
    uint64_t failureCallbackErrors() const noexcept { return failure_callback_errors_.load(); }
    void shutdown() noexcept;
private:
    BoundedQueue<Job> queue_;
    std::vector<std::thread> workers_;
    std::mutex shutdown_mutex_;
    std::atomic<uint64_t> failure_callback_errors_{0};
};
}
