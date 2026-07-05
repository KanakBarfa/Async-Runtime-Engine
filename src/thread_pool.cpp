#include <async_runtime/scheduler.hpp>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace async_runtime {

namespace {

class work_stealing_deque {
  public:
    struct circular_array {
        std::int64_t capacity;
        std::int64_t mask;
        std::vector<std::atomic<std::function<void()> *>> buffer;

        explicit circular_array(std::int64_t cap) : capacity(cap), mask(cap - 1), buffer(cap) {
            for (std::int64_t i = 0; i < cap; ++i) {
                buffer[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        circular_array *grow(std::int64_t b, std::int64_t t) {
            auto *new_array = new circular_array(capacity * 2);
            for (std::int64_t i = t; i < b; ++i) {
                new_array->buffer[i & new_array->mask].store(
                    buffer[i & mask].load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return new_array;
        }
    };

    explicit work_stealing_deque(std::int64_t initial_capacity = 1024) : top_(0), bottom_(0) {
        assert((initial_capacity & (initial_capacity - 1)) == 0);
        array_.store(new circular_array(initial_capacity), std::memory_order_relaxed);
    }

    ~work_stealing_deque() {
        circular_array *arr = array_.load(std::memory_order_relaxed);
        std::int64_t b = bottom_.load(std::memory_order_relaxed);
        std::int64_t t = top_.load(std::memory_order_relaxed);
        for (std::int64_t i = t; i < b; ++i) {
            auto *task = arr->buffer[i & arr->mask].load(std::memory_order_relaxed);
            delete task;
        }
        delete arr;
    }

    void push(std::function<void()> *task) {
        std::int64_t b = bottom_.load(std::memory_order_relaxed);
        std::int64_t t = top_.load(std::memory_order_acquire);
        circular_array *a = array_.load(std::memory_order_relaxed);
        std::int64_t size = b - t;
        if (size >= a->capacity - 1) {
            circular_array *new_a = a->grow(b, t);
            history_.push_back(std::unique_ptr<circular_array>(a));
            array_.store(new_a, std::memory_order_release);
            a = new_a;
        }
        a->buffer[b & a->mask].store(task, std::memory_order_relaxed);
        bottom_.store(b + 1, std::memory_order_seq_cst);
    }

    std::function<void()> *pop() {
        std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        circular_array *a = array_.load(std::memory_order_relaxed);
        bottom_.store(b, std::memory_order_seq_cst);
        std::int64_t t = top_.load(std::memory_order_seq_cst);
        std::int64_t size = b - t;
        if (size < 0) {
            bottom_.store(b + 1, std::memory_order_seq_cst);
            return nullptr;
        }
        auto *task = a->buffer[b & a->mask].load(std::memory_order_relaxed);
        if (size == 0) {
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                              std::memory_order_seq_cst)) {
                task = nullptr;
            }
            bottom_.store(b + 1, std::memory_order_seq_cst);
        }
        return task;
    }

    std::function<void()> *steal() {
        std::int64_t t = top_.load(std::memory_order_seq_cst);
        std::int64_t b = bottom_.load(std::memory_order_seq_cst);
        std::int64_t size = b - t;
        if (size <= 0) {
            return nullptr;
        }
        circular_array *a = array_.load(std::memory_order_acquire);
        auto *task = a->buffer[t & a->mask].load(std::memory_order_relaxed);
        if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                          std::memory_order_seq_cst)) {
            return nullptr;
        }
        return task;
    }

    [[nodiscard]] bool empty() const noexcept {
        std::int64_t b = bottom_.load(std::memory_order_acquire);
        std::int64_t t = top_.load(std::memory_order_acquire);
        return b <= t;
    }

  private:
    std::atomic<std::int64_t> top_;
    std::atomic<std::int64_t> bottom_;
    std::atomic<circular_array *> array_;
    std::vector<std::unique_ptr<circular_array>> history_;
};

class global_task_queue {
  public:
    void push(std::function<void()> *task) {
        std::scoped_lock lock{mutex_};
        tasks_.push_back(task);
    }

    std::function<void()> *pop_nonblocking() {
        std::scoped_lock lock{mutex_};
        if (tasks_.empty()) {
            return nullptr;
        }
        auto *task = tasks_.front();
        tasks_.pop_front();
        return task;
    }

    bool empty_relaxed() const {
        std::scoped_lock lock{mutex_};
        return tasks_.empty();
    }

    ~global_task_queue() {
        for (auto *task : tasks_) {
            delete task;
        }
    }

  private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()> *> tasks_;
};

thread_local work_stealing_deque *local_queue = nullptr;

} // namespace

struct thread_pool::impl {
    global_task_queue global_queue;
    std::vector<std::unique_ptr<work_stealing_deque>> deques;
    std::vector<std::thread> workers;
    std::atomic<bool> stop{false};
    std::atomic<std::int32_t> sleeping_workers{0};
    std::mutex mutex;
    std::condition_variable cv;

    explicit impl(std::size_t n) {
        deques.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            deques.push_back(std::make_unique<work_stealing_deque>());
        }
        workers.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            workers.emplace_back([this, i] { worker_loop(i); });
        }
    }

    ~impl() {
        stop.store(true, std::memory_order_release);
        {
            std::scoped_lock lock{mutex};
            cv.notify_all();
        }
        for (auto &w : workers) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    void enqueue(std::function<void()> fn) {
        auto *task = new std::function<void()>(std::move(fn));
        if (local_queue != nullptr) {
            local_queue->push(task);
        } else {
            global_queue.push(task);
        }

        if (sleeping_workers.load(std::memory_order_relaxed) > 0) {
            std::scoped_lock lock{mutex};
            cv.notify_one();
        }
    }

    [[nodiscard]] bool empty() const {
        if (!global_queue.empty_relaxed()) {
            return false;
        }
        for (const auto &deque : deques) {
            if (!deque->empty()) {
                return false;
            }
        }
        return true;
    }

    std::function<void()> *steal_from_others(std::size_t worker_id) {
        std::size_t num_workers = deques.size();
        if (num_workers <= 1) {
            return nullptr;
        }

        static thread_local std::uint32_t rng_state = static_cast<std::uint32_t>(worker_id) + 1;
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        std::size_t start_index = rng_state % num_workers;

        for (std::size_t i = 0; i < num_workers; ++i) {
            std::size_t target = (start_index + i) % num_workers;
            if (target == worker_id) {
                continue;
            }
            if (auto *task = deques[target]->steal()) {
                return task;
            }
        }
        return nullptr;
    }

    void worker_loop(std::size_t worker_id) {
        local_queue = deques[worker_id].get();

        while (true) {
            std::function<void()> *task = local_queue->pop();

            if (task == nullptr) {
                task = global_queue.pop_nonblocking();
            }

            if (task == nullptr) {
                task = steal_from_others(worker_id);
            }

            if (task != nullptr) {
                std::unique_ptr<std::function<void()>> task_ptr{task};
                (*task_ptr)();
                continue;
            }

            if (stop.load(std::memory_order_acquire)) {
                break;
            }

            bool found = false;
            for (int spin = 0; spin < 32; ++spin) {
                std::this_thread::yield();
                task = global_queue.pop_nonblocking();
                if (task != nullptr) {
                    found = true;
                    break;
                }
                task = steal_from_others(worker_id);
                if (task != nullptr) {
                    found = true;
                    break;
                }
            }

            if (found) {
                std::unique_ptr<std::function<void()>> task_ptr{task};
                (*task_ptr)();
                continue;
            }

            {
                std::unique_lock lock{mutex};
                if (stop.load(std::memory_order_acquire)) {
                    break;
                }
                if (!global_queue.empty_relaxed() || has_local_work()) {
                    continue;
                }
                sleeping_workers.fetch_add(1, std::memory_order_relaxed);

                cv.wait(lock, [this] {
                    return stop.load(std::memory_order_acquire) || !global_queue.empty_relaxed() ||
                           has_local_work();
                });

                sleeping_workers.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool has_local_work() const {
        for (const auto &deque : deques) {
            if (!deque->empty()) {
                return true;
            }
        }
        return false;
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
    while (!impl_->empty()) {
        std::this_thread::yield();
    }
}

} // namespace async_runtime
