#pragma once

#include <async_runtime/sender.hpp>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <thread>

namespace async_runtime {

class thread_pool_scheduler;

class thread_pool {
  public:
    explicit thread_pool(std::size_t thread_count = std::thread::hardware_concurrency());
    ~thread_pool();

    thread_pool(const thread_pool &) = delete;
    thread_pool &operator=(const thread_pool &) = delete;

    thread_pool_scheduler get_scheduler() noexcept;

    void enqueue(std::function<void()> fn);
    void wait();

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

struct schedule_sender {
    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr), stdexec::set_stopped_t()>;

    thread_pool *pool;

    template <stdexec::receiver Receiver> struct operation {
        thread_pool *pool;
        Receiver receiver;

        void start() & noexcept {
            pool->enqueue(
                [r = std::move(receiver)]() mutable { stdexec::set_value(std::move(r)); });
        }
    };

    template <stdexec::receiver Receiver> [[nodiscard]] auto connect(Receiver r) const noexcept {
        return operation<Receiver>{pool, std::move(r)};
    }
};

class thread_pool_scheduler {
  public:
    explicit thread_pool_scheduler(thread_pool &pool) noexcept : pool_{&pool} {}

    [[nodiscard]] schedule_sender schedule() const noexcept {
        return schedule_sender{pool_};
    }

    bool operator==(const thread_pool_scheduler &) const noexcept = default;

  private:
    thread_pool *pool_;
};

} // namespace async_runtime
