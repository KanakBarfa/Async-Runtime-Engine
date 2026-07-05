#pragma once

#include <async_runtime/io_context.hpp>
#include <async_runtime/scheduler.hpp>
#include <async_runtime/sender.hpp>

namespace async_runtime {

class runtime {
  public:
    explicit runtime(std::size_t thread_count = std::thread::hardware_concurrency(),
                     unsigned io_queue_depth = 256);
    ~runtime();

    runtime(const runtime &) = delete;
    runtime &operator=(const runtime &) = delete;

    thread_pool_scheduler get_scheduler() noexcept;
    io_context &get_io_context() noexcept;

    void run();
    void stop();

  private:
    thread_pool pool_;
    io_context io_ctx_;
};

} // namespace async_runtime
