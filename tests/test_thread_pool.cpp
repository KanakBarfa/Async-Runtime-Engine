#include <async_runtime/scheduler.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <exec/start_detached.hpp>
#include <stdexec/execution.hpp>
#include <thread>

int main() {
    constexpr std::size_t task_count = 100'000;
    async_runtime::thread_pool pool{std::thread::hardware_concurrency()};
    auto sched = pool.get_scheduler();

    std::atomic<std::size_t> completed{0};

    for (std::size_t i = 0; i < task_count; ++i) {
        exec::start_detached(stdexec::schedule(sched) | stdexec::then([&completed] {
                                 completed.fetch_add(1, std::memory_order_relaxed);
                             }));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (completed.load(std::memory_order_acquire) < task_count) {
        assert(std::chrono::steady_clock::now() < deadline && "thread pool stress test timed out");
        std::this_thread::yield();
    }

    assert(completed.load() == task_count);
    return 0;
}
