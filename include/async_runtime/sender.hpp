#pragma once

#include <stdexec/execution.hpp>
#include <expected>
#include <system_error>

namespace async_runtime {

namespace ex = stdexec;

template <typename T>
using Result = std::expected<T, std::error_code>;

template <typename Sender>
concept AsyncSender = ex::sender<Sender>;

template <typename Receiver>
concept AsyncReceiver = ex::receiver<Receiver>;

template <typename Scheduler>
concept AsyncScheduler = ex::scheduler<Scheduler>;

} // namespace async_runtime
