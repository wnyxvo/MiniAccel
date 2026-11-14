#include "core/task_state.hpp"
#include <chrono>
#include <limits>
namespace miniaccel::internal {
Result<TaskToken> TokenAllocator::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_ || !generation_ || !next_) return Status{StatusCode::InvalidArgument, "invalid or exhausted token allocator"};
    const TaskToken token{context_, generation_, next_};
    next_ = next_ == UINT64_MAX ? 0 : next_ + 1;
    return token;
}
static bool terminal(TaskState s) { return s == TaskState::Completed || s == TaskState::Failed || s == TaskState::Canceled; }
Status TaskRecord::transition(TaskState next, Status status, bool confirmed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto current = result_.state;
    bool valid = (current == TaskState::Created && next == TaskState::WaitingDependencies) ||
        (current == TaskState::WaitingDependencies && (next == TaskState::Ready || next == TaskState::Failed)) ||
        (current == TaskState::Ready && (next == TaskState::Submitted || next == TaskState::Failed)) ||
        (current == TaskState::Submitted && (next == TaskState::Completed || next == TaskState::Failed || (next == TaskState::Canceled && confirmed))) ||
        ((current == TaskState::Created || current == TaskState::WaitingDependencies || current == TaskState::Ready) && next == TaskState::Canceled);
    if (!valid || (next == TaskState::Completed && !status.ok()) || (next == TaskState::Failed && status.ok()) || (next == TaskState::Canceled && status.code != StatusCode::Canceled) || (!terminal(next) && !status.ok()))
        return {StatusCode::InvalidArgument, "invalid task transition or status"};
    result_.state = next;
    result_.status = std::move(status);
    if (terminal(next)) cv_.notify_all();
    return {};
}
TaskResult TaskRecord::query() const { std::lock_guard<std::mutex> lock(mutex_); return result_; }
Result<TaskResult> TaskRecord::wait(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto done = [this] { return terminal(result_.state); };
    if (timeout_ms == UINT32_MAX) cv_.wait(lock, done);
    else if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), done)) return Status{StatusCode::Timeout, "task wait timed out"};
    return result_;
}
}
