#pragma once
#include "core/bounded_queue.hpp"
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
namespace miniaccel::internal {
class ThreadPool {
public:
    using Work = std::function<void()>;
    using Failure = std::function<void(Status)>;
    struct Job { Work work; Failure failure; };
    static unsigned default_workers() noexcept;
    ThreadPool(size_t workers, size_t capacity);
    ~ThreadPool() noexcept;
    Status submit(Job job);
    void shutdown() noexcept;
private:
    BoundedQueue<Job> queue_;
    std::vector<std::thread> workers_;
    std::mutex shutdown_mutex_;
};
}
