#include <async_runtime/scheduler.hpp>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace async_runtime {

namespace {

class task_queue {
public:
    void push(std::function<void()> fn) {
        {
            std::lock_guard lock{mutex_};
            items_.push_back(std::move(fn));
        }
        cv_.notify_one();
    }

    std::optional<std::function<void()>> pop(std::atomic<bool>& stop) {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [&] { return !items_.empty() || stop.load(std::memory_order_acquire); });
        if (items_.empty()) return std::nullopt;
        auto fn = std::move(items_.front());
        items_.erase(items_.begin());
        return fn;
    }

    void wake_all() {
        cv_.notify_all();
    }

    bool empty() const noexcept {
        std::lock_guard lock{mutex_};
        return items_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::function<void()>> items_;
};

} // namespace

struct thread_pool::impl {
    task_queue queue;
    std::vector<std::thread> workers;
    std::atomic<bool> stop{false};

    explicit impl(std::size_t n) {
        workers.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            workers.emplace_back([this] { worker_loop(); });
        }
    }

    ~impl() {
        stop.store(true, std::memory_order_release);
        queue.wake_all();
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    }

    void enqueue(std::function<void()> fn) {
        queue.push(std::move(fn));
    }

    void worker_loop() {
        while (!stop.load(std::memory_order_acquire)) {
            if (auto task = queue.pop(stop)) {
                (*task)();
            }
        }
    }
};

thread_pool::thread_pool(std::size_t thread_count)
    : impl_(std::make_unique<impl>(thread_count == 0 ? 1 : thread_count)) {}

thread_pool::~thread_pool() = default;

thread_pool_scheduler thread_pool::get_scheduler() noexcept {
    return thread_pool_scheduler{*this};
}

void thread_pool::enqueue(std::function<void()> fn) {
    impl_->enqueue(std::move(fn));
}

void thread_pool::wait() {
    while (!impl_->queue.empty()) {
        std::this_thread::yield();
    }
}

namespace {

} // namespace

} // namespace async_runtime
