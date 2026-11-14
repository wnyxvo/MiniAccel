#pragma once
#include "miniaccel/task.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace miniaccel::internal {
enum class Opcode : uint32_t { Nop=1, Delay=2, Fill=3, Copy=4, VectorAdd=5 };
using BackendContextHandle = uint64_t;
using BackendBufferHandle = uint64_t;
using BackendTaskId = uint64_t;
// Opcode N occupies bit N-1; unknown values have no defined capability bit.
constexpr uint64_t opcodeCapabilityBit(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::Nop:       return uint64_t{1} << 0;
    case Opcode::Delay:     return uint64_t{1} << 1;
    case Opcode::Fill:      return uint64_t{1} << 2;
    case Opcode::Copy:      return uint64_t{1} << 3;
    case Opcode::VectorAdd: return uint64_t{1} << 4;
    }
    return 0;
}
struct BackendCapabilities {
    uint64_t opcode_bits{};
    constexpr bool supports(Opcode opcode) const noexcept {
        return (opcode_bits & opcodeCapabilityBit(opcode)) != 0;
    }
};
struct ContextConfig { uint32_t max_pending_tasks{256}; uint32_t max_inflight{16}; };
struct BackendBuffer { BackendBufferHandle handle{}; size_t size{}; };
struct BackendCommand { Opcode opcode{Opcode::Nop}; std::vector<BackendBufferHandle> buffers; uint64_t src_offset{}, dst_offset{}, bytes{}; uint32_t delay_ms{}, fill_byte{}; };
struct BackendCompletion { BackendTaskId id{}; TaskState state{}; Status status{}; uint64_t completed_ns{}; };
class IBackend {
public:
    virtual ~IBackend() noexcept = default;
    virtual Status initialize() = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual Result<BackendContextHandle> createContext(const ContextConfig&) = 0;
    virtual Status destroyContext(BackendContextHandle) = 0;
    virtual Result<BackendBuffer> allocateBuffer(BackendContextHandle,size_t,size_t) = 0;
    virtual Status releaseBuffer(BackendBufferHandle) = 0;
    virtual Status submit(BackendContextHandle,const BackendCommand&,BackendTaskId*) = 0;
    virtual Status pollCompletions(std::vector<BackendCompletion>&,int timeout_ms) = 0;
    virtual void shutdown() noexcept = 0;
};
}
