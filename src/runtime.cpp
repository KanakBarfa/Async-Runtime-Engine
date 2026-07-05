#include <async_runtime/runtime.hpp>

namespace async_runtime {

runtime::runtime(std::size_t thread_count, unsigned io_queue_depth)
    : pool_(thread_count), io_ctx_(io_queue_depth) {}

runtime::~runtime() {
    stop();
}

thread_pool_scheduler runtime::get_scheduler() noexcept {
    return pool_.get_scheduler();
}

io_context& runtime::get_io_context() noexcept {
    return io_ctx_;
}

void runtime::run() {
    io_ctx_.run();
}

void runtime::stop() {
    io_ctx_.stop();
}

} // namespace async_runtime
