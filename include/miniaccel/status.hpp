#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
namespace miniaccel {
enum class StatusCode : uint32_t { Ok=0, InvalidArgument, InvalidHandle, WrongContext, Unsupported, QueueFull, Timeout, Closed, Busy, DependencyFailed, Canceled, BackendUnavailable, IoError, InternalError, OutOfMemory };
struct Status {
    StatusCode code{StatusCode::Ok};
    std::string message;
    bool ok() const noexcept { return code == StatusCode::Ok; }
};
template<class T> class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Status status) : status_(std::move(status)) {
        if (status_.ok()) throw std::invalid_argument("Result error must not be Ok");
    }
    bool ok() const noexcept { return value_.has_value(); }
    const Status& status() const noexcept { return status_; }
    T& value() & { return value_.value(); }
    const T& value() const & { return value_.value(); }
    T&& value() && { return return_value(); }
private:
    T&& return_value() { return std::move(value_.value()); }
    Status status_{};
    std::optional<T> value_;
};
}
