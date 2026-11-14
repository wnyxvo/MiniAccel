#include "backends/cpu/cpu_backend.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <thread>
#include <system_error>
#include <stdexcept>
namespace miniaccel::internal {
struct CpuBackend::Buffer {
    uint64_t context;
    size_t size;
    size_t maps{0};
    void* data;
    Buffer(uint64_t c, size_t s, void* p) : context(c), size(s), data(p) {}
    ~Buffer() noexcept { std::free(data); }
};
CpuBackend::CpuBackend(size_t workers,size_t queue_capacity,size_t completion_capacity)
 : worker_count_(workers),queue_capacity_(queue_capacity),slots_(completion_capacity) {
    if (!workers || workers > 1024 || !queue_capacity || !completion_capacity) throw std::invalid_argument("invalid backend capacity");
}
CpuBackend::~CpuBackend() noexcept { shutdown(); }
Status CpuBackend::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || stopped_) return {StatusCode::Closed,"backend closed"};
    if (initialized_) return {};
    try { pool_ = std::make_unique<ThreadPool>(worker_count_,queue_capacity_); }
    catch (const std::bad_alloc&) { return {StatusCode::OutOfMemory,"worker allocation failed"}; }
    catch (const std::system_error&) { return {StatusCode::BackendUnavailable,"worker creation failed"}; }
    initialized_=true; return {};
}
BackendCapabilities CpuBackend::capabilities() const { return {(1ull<<5)-1}; }
Result<BackendContextHandle> CpuBackend::createContext(const ContextConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || stopping_) return Status{StatusCode::Closed,"backend not available"};
    if (!config.max_inflight || !config.max_pending_tasks) return Status{StatusCode::InvalidArgument,"invalid context capacity"};
    if (!next_context_) return Status{StatusCode::InternalError,"context ids exhausted"};
    auto id=next_context_;
    try { contexts_.emplace(id,Context{0,config.max_inflight}); } catch (const std::bad_alloc&) { return Status{StatusCode::OutOfMemory,"context allocation failed"}; }
    next_context_ = id == UINT64_MAX ? 0 : id+1;
    return id;
}
Status CpuBackend::destroyContext(BackendContextHandle id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ctx=contexts_.find(id); if(ctx==contexts_.end()) return {StatusCode::InvalidHandle,"invalid context"};
    if(ctx->second.inflight) return {StatusCode::Busy,"context has running tasks"};
    for (auto& entry:buffers_) if(entry.second->context==id) return {StatusCode::Busy,"context has buffers"};
    contexts_.erase(ctx); return {};
}
Result<BackendBuffer> CpuBackend::allocateBuffer(BackendContextHandle ctx,size_t size,size_t alignment) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || stopping_) return Status{StatusCode::Closed,"backend closed"};
    if (!contexts_.count(ctx)) return Status{StatusCode::InvalidHandle,"invalid context"};
    if (!size || alignment < sizeof(void*) || (alignment & (alignment-1))) return Status{StatusCode::InvalidArgument,"invalid size or alignment"};
    if (!next_buffer_) return Status{StatusCode::InternalError,"buffer ids exhausted"};
    void* data=nullptr; if(posix_memalign(&data,alignment,size)!=0) return Status{StatusCode::OutOfMemory,"buffer allocation failed"};
    std::shared_ptr<Buffer> buffer;
    try { buffer=std::make_shared<Buffer>(ctx,size,data); } catch (const std::bad_alloc&) { std::free(data); return Status{StatusCode::OutOfMemory,"buffer metadata allocation failed"}; }
    auto id=next_buffer_;
    try { buffers_.emplace(id,buffer); } catch (const std::bad_alloc&) { return Status{StatusCode::OutOfMemory,"buffer registry allocation failed"}; }
    next_buffer_=id==UINT64_MAX?0:id+1; return BackendBuffer{id,size};
}
Status CpuBackend::releaseBuffer(BackendBufferHandle id) {
    std::lock_guard<std::mutex> lock(mutex_); auto it=buffers_.find(id);
    if(it==buffers_.end()) return {StatusCode::InvalidHandle,"invalid buffer"};
    if(it->second->maps) return {StatusCode::Busy,"buffer is mapped"};
    buffers_.erase(it); return {};
}
Result<void*> CpuBackend::mapBuffer(BackendBufferHandle id) {
    std::lock_guard<std::mutex> lock(mutex_); if(stopping_) return Status{StatusCode::Closed,"backend closed"};
    auto it=buffers_.find(id); if(it==buffers_.end()) return Status{StatusCode::InvalidHandle,"invalid buffer"};
    if(it->second->maps==SIZE_MAX) return Status{StatusCode::InternalError,"mapping count exhausted"};
    ++it->second->maps; return it->second->data;
}
Status CpuBackend::unmapBuffer(BackendBufferHandle id) {
    std::lock_guard<std::mutex> lock(mutex_); auto it=buffers_.find(id);
    if(it==buffers_.end()) return {StatusCode::InvalidHandle,"invalid buffer"};
    if(!it->second->maps) return {StatusCode::InvalidArgument,"buffer is not mapped"};
    --it->second->maps; return {};
}
Status CpuBackend::submit(BackendContextHandle ctx,const BackendCommand& cmd,BackendTaskId* out) {
    if(!out) return {StatusCode::InvalidArgument,"null output id"};
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || stopping_) return {StatusCode::Closed,"backend closed"};
    auto context=contexts_.find(ctx); if(context==contexts_.end()) return {StatusCode::InvalidHandle,"invalid context"};
    size_t count=0;
    switch(cmd.opcode) {
    case Opcode::Nop: if(cmd.bytes || cmd.delay_ms) return {StatusCode::InvalidArgument,"invalid nop"}; break;
    case Opcode::Delay: if(cmd.delay_ms>1000) return {StatusCode::InvalidArgument,"invalid delay"}; break;
    case Opcode::Fill: count=1; if(cmd.fill_byte>255) return {StatusCode::InvalidArgument,"invalid fill byte"}; break;
    case Opcode::Copy: count=2; break;
    case Opcode::VectorAdd: count=3; if((cmd.src_offset|cmd.dst_offset|cmd.bytes)&3) return {StatusCode::InvalidArgument,"unaligned vector"}; break;
    default: return {StatusCode::Unsupported,"unsupported opcode"};
    }
    if(cmd.buffers.size()!=count) return {StatusCode::InvalidArgument,"wrong buffer count"};
    std::vector<std::shared_ptr<Buffer>> refs;
    try { refs.reserve(count); for(auto id:cmd.buffers) { auto it=buffers_.find(id); if(it==buffers_.end()) return {StatusCode::InvalidHandle,"invalid buffer"}; if(it->second->context!=ctx) return {StatusCode::WrongContext,"wrong context"}; refs.push_back(it->second); } }
    catch(const std::bad_alloc&) { return {StatusCode::OutOfMemory,"command allocation failed"}; }
    auto valid=[&](size_t i,uint64_t off) { auto size=refs[i]->size; return off<=size && cmd.bytes<=size-off; };
    if ((cmd.opcode==Opcode::Fill && !valid(0,cmd.dst_offset)) ||
        (cmd.opcode==Opcode::Copy && (!valid(0,cmd.src_offset)||!valid(1,cmd.dst_offset))) ||
        (cmd.opcode==Opcode::VectorAdd && (!valid(0,cmd.src_offset)||!valid(1,cmd.src_offset)||!valid(2,cmd.dst_offset)))) return {StatusCode::InvalidArgument,"buffer range invalid"};
    if(cmd.opcode==Opcode::VectorAdd && (refs[0]==refs[2] || refs[1]==refs[2])) {
        // Input/output alias is unsupported even for disjoint ranges.
        return {StatusCode::InvalidArgument,"vector input/output alias"};
    }
    if(context->second.inflight>=context->second.limit) return {StatusCode::QueueFull,"context inflight full"};
    auto slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot& s){return !s.used;});
    if(slot==slots_.end()) return {StatusCode::QueueFull,"completion slots full"};
    if(!next_task_) return {StatusCode::InternalError,"task ids exhausted"};
    size_t index=static_cast<size_t>(slot-slots_.begin()); auto id=next_task_;
    bool reserved = false;
    try {
        // All potentially allocating objects are prepared before making the job visible.
        ThreadPool::Job job;
        auto opcode=cmd.opcode; auto src=cmd.src_offset; auto dst=cmd.dst_offset; auto bytes=cmd.bytes; auto fill=cmd.fill_byte; auto delay=cmd.delay_ms;
        job.work=[this,index,ctx,refs=std::move(refs),opcode,src,dst,bytes,fill,delay] {
#ifndef NDEBUG
            if(fail_next_.exchange(false)) throw std::runtime_error("injected CPU execution failure");
#endif
            if(opcode==Opcode::Delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            else if(opcode==Opcode::Fill && bytes) std::memset(static_cast<unsigned char*>(refs[0]->data)+dst,static_cast<int>(fill),bytes);
            else if(opcode==Opcode::Copy && bytes) std::memmove(static_cast<unsigned char*>(refs[1]->data)+dst,static_cast<unsigned char*>(refs[0]->data)+src,bytes);
            else if(opcode==Opcode::VectorAdd) {
                auto a=static_cast<unsigned char*>(refs[0]->data)+src, b=static_cast<unsigned char*>(refs[1]->data)+src, c=static_cast<unsigned char*>(refs[2]->data)+dst;
                for(uint64_t i=0;i<bytes;i+=4) { float x,y,z; std::memcpy(&x,a+i,4); std::memcpy(&y,b+i,4); z=x+y; std::memcpy(c+i,&z,4); }
            }
            complete(index,ctx,{});
        };
        job.failure=[this,index,ctx](Status s) { complete(index,ctx,std::move(s)); };
        slot->used=true; slot->done=false; slot->completion.id=id; ++context->second.inflight; reserved=true;
        auto result=pool_->submit(std::move(job));
        if(!result.ok()) { slot->used=false; --context->second.inflight; return result; }
    } catch(const std::bad_alloc&) { if (reserved) { slot->used=false; --context->second.inflight; } return {StatusCode::OutOfMemory,"work allocation failed"}; }
    next_task_=id==UINT64_MAX?0:id+1; *out=id; return {};
}
void CpuBackend::complete(size_t index,BackendContextHandle ctx,Status status) noexcept {
    std::lock_guard<std::mutex> lock(mutex_); auto& slot=slots_[index];
    if(!slot.used || slot.done) std::terminate();
    slot.completion.state=status.ok()?TaskState::Completed:TaskState::Failed;
    slot.completion.status=std::move(status);
    slot.completion.completed_ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    slot.done=true; --contexts_.at(ctx).inflight; completion_cv_.notify_all();
}
Status CpuBackend::pollCompletions(std::vector<BackendCompletion>& out,int timeout_ms) {
    if(timeout_ms < -1) return {StatusCode::InvalidArgument,"invalid timeout"};
    std::unique_lock<std::mutex> lock(mutex_);
    auto ready=[this] { return stopped_ || std::any_of(slots_.begin(),slots_.end(),[](const Slot& s){return s.done;}); };
    if(timeout_ms<0) completion_cv_.wait(lock,ready);
    else if(!completion_cv_.wait_for(lock,std::chrono::milliseconds(timeout_ms),ready)) return {StatusCode::Timeout,"completion wait timed out"};
    size_t count=0; for(const auto& s:slots_) if(s.done) ++count;
    if(!count) return {StatusCode::Closed,"backend drained"};
    try { out.reserve(out.size()+count); } catch(const std::bad_alloc&) { return {StatusCode::OutOfMemory,"completion output allocation failed"}; }
    for(auto& s:slots_) if(s.done) { out.push_back(std::move(s.completion)); s.done=false; s.used=false; }
    return {};
}
void CpuBackend::shutdown() noexcept {
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    { std::lock_guard<std::mutex> lock(mutex_); if(stopped_) return; stopping_=true; }
    if(pool_) pool_->shutdown();
    { std::lock_guard<std::mutex> lock(mutex_); stopped_=true; completion_cv_.notify_all(); }
}
}
