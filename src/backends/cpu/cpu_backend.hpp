#pragma once
#include "backends/backend.hpp"
#include "core/thread_pool.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace miniaccel::internal {
class CpuBackend final : public IBackend {
public:
    explicit CpuBackend(size_t workers = ThreadPool::default_workers(), size_t queue_capacity = 256, size_t completion_capacity = 1024);
    ~CpuBackend() noexcept override;
    Status initialize() override;
    BackendCapabilities capabilities() const override;
    Result<BackendContextHandle> createContext(const ContextConfig&) override;
    Status destroyContext(BackendContextHandle) override;
    Result<BackendBuffer> allocateBuffer(BackendContextHandle,size_t,size_t) override;
    Status releaseBuffer(BackendBufferHandle) override;
    Result<void*> mapBuffer(BackendBufferHandle);
    Status unmapBuffer(BackendBufferHandle);
    Status submit(BackendContextHandle,const BackendCommand&,BackendTaskId*) override;
    Status pollCompletions(std::vector<BackendCompletion>&,int timeout_ms) override;
    void shutdown() noexcept override;
#ifndef NDEBUG
    void failNextExecutionForTest() noexcept { fail_next_.store(true); }
#endif
private:
    struct Context { size_t inflight{}; size_t limit{}; };
    struct Buffer;
    struct Slot { bool used{}, done{}; BackendCompletion completion{}; };
    void complete(size_t, BackendContextHandle, Status) noexcept;
    size_t worker_count_, queue_capacity_;
    mutable std::mutex mutex_;
    std::mutex shutdown_mutex_;
    std::condition_variable completion_cv_;
    std::unique_ptr<ThreadPool> pool_;
    std::vector<Slot> slots_;
    std::unordered_map<uint64_t, Context> contexts_;
    std::unordered_map<uint64_t, std::shared_ptr<Buffer>> buffers_;
    uint64_t next_context_{1}, next_buffer_{1}, next_task_{1};
    bool initialized_{false}, stopping_{false}, stopped_{false};
#ifndef NDEBUG
    std::atomic<bool> fail_next_{false};
#endif
};
}
