#pragma once
#include "miniaccel/task.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
namespace miniaccel::internal {
class TokenAllocator {
public:
    TokenAllocator(uint64_t context, uint32_t generation, uint64_t first = 1) : context_(context), generation_(generation), next_(first) {}
    Result<TaskToken> allocate();
private:
    std::mutex mutex_;
    uint64_t context_;
    uint32_t generation_;
    uint64_t next_;
};
class TaskRecord {
public:
    explicit TaskRecord(TaskToken token) : result_{token, TaskState::Created, {}} {}
    Status transition(TaskState next, Status status = {}, bool backend_cancel_confirmed = false);
    TaskResult query() const;
    Result<TaskResult> wait(uint32_t timeout_ms);
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    TaskResult result_;
};
}
