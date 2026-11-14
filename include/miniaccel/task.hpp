#pragma once
#include "miniaccel/status.hpp"
#include <cstdint>
namespace miniaccel {
enum class TaskState : uint32_t { Created, WaitingDependencies, Ready, Submitted, Completed, Failed, Canceled };
struct TaskToken {
    uint64_t context_id{};
    uint32_t generation{};
    uint64_t sequence{};
    bool operator==(const TaskToken& rhs) const noexcept { return context_id == rhs.context_id && generation == rhs.generation && sequence == rhs.sequence; }
    bool operator!=(const TaskToken& rhs) const noexcept { return !(*this == rhs); }
};
struct TaskResult { TaskToken token{}; TaskState state{}; Status status{}; };
}
