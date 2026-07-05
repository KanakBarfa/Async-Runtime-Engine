#include <async_runtime/io_context.hpp>

#include <liburing.h>
#include <atomic>
#include <stdexcept>
#include <system_error>

namespace async_runtime {

struct io_context::ring_impl {
    io_uring ring{};
    std::atomic<bool> running{false};

    explicit ring_impl(unsigned queue_depth) {
        const int ret = io_uring_queue_init(queue_depth, &ring, 0);
        if (ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init");
        }
    }

    ~ring_impl() {
        io_uring_queue_exit(&ring);
    }

    io_uring_sqe* get_sqe() {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            io_uring_submit(&ring);
            sqe = io_uring_get_sqe(&ring);
        }
        return sqe;
    }

    void submit_with_cb(io_uring_sqe* sqe, io_callback cb) {
        auto* heap_cb = new io_callback{std::move(cb)};
        io_uring_sqe_set_data(sqe, heap_cb);
    }
};

io_context::io_context(unsigned queue_depth)
    : ring_(std::make_unique<ring_impl>(queue_depth)) {}

io_context::~io_context() = default;

void io_context::run() {
    ring_->running.store(true, std::memory_order_release);
    while (ring_->running.load(std::memory_order_acquire)) {
        io_uring_submit_and_wait(&ring_->ring, 1);
        io_uring_cqe* cqe = nullptr;
        unsigned head = 0;
        unsigned count = 0;
        io_uring_for_each_cqe(&ring_->ring, head, cqe) {
            auto* cb = static_cast<io_callback*>(io_uring_cqe_get_data(cqe));
            if (cb) {
                (*cb)({cqe->res, cqe->flags});
                delete cb;
            }
            ++count;
        }
        io_uring_cq_advance(&ring_->ring, count);
    }
}

void io_context::stop() {
    ring_->running.store(false, std::memory_order_release);
}

void io_context::submit_read(int fd, std::span<std::byte> buf, std::uint64_t offset,
                             io_callback cb) {
    auto* sqe = ring_->get_sqe();
    io_uring_prep_read(sqe, fd, buf.data(), static_cast<unsigned>(buf.size()), offset);
    ring_->submit_with_cb(sqe, std::move(cb));
}

void io_context::submit_write(int fd, std::span<const std::byte> buf, std::uint64_t offset,
                              io_callback cb) {
    auto* sqe = ring_->get_sqe();
    io_uring_prep_write(sqe, fd, buf.data(), static_cast<unsigned>(buf.size()), offset);
    ring_->submit_with_cb(sqe, std::move(cb));
}

void io_context::submit_accept(int fd, io_callback cb) {
    auto* sqe = ring_->get_sqe();
    io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
    ring_->submit_with_cb(sqe, std::move(cb));
}

void io_context::submit_recv(int fd, std::span<std::byte> buf, int flags, io_callback cb) {
    auto* sqe = ring_->get_sqe();
    io_uring_prep_recv(sqe, fd, buf.data(), buf.size(), flags);
    ring_->submit_with_cb(sqe, std::move(cb));
}

void io_context::submit_send(int fd, std::span<const std::byte> buf, int flags, io_callback cb) {
    auto* sqe = ring_->get_sqe();
    io_uring_prep_send(sqe, fd, buf.data(), buf.size(), flags);
    ring_->submit_with_cb(sqe, std::move(cb));
}

io_context::read_sender io_context::async_read(int fd, std::span<std::byte> buf,
                                               std::uint64_t offset) noexcept {
    return {this, fd, buf, offset};
}

io_context::write_sender io_context::async_write(int fd, std::span<const std::byte> buf,
                                                 std::uint64_t offset) noexcept {
    return {this, fd, buf, offset};
}

io_context::accept_sender io_context::async_accept(int fd) noexcept {
    return {this, fd};
}

io_context::recv_sender io_context::async_recv(int fd, std::span<std::byte> buf,
                                               int flags) noexcept {
    return {this, fd, buf, flags};
}

io_context::send_sender io_context::async_send(int fd, std::span<const std::byte> buf,
                                               int flags) noexcept {
    return {this, fd, buf, flags};
}

} // namespace async_runtime
