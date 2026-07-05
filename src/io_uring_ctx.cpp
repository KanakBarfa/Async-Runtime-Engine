#include <async_runtime/io_context.hpp>

#include <atomic>
#include <liburing.h>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace async_runtime {

struct io_context::ring_impl {
    io_uring ring{};
    std::atomic<bool> running{false};
    std::mutex submission_mutex;

    explicit ring_impl(unsigned queue_depth) {
        const int ret = io_uring_queue_init(queue_depth, &ring, 0);
        if (ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init");
        }
    }

    ~ring_impl() {
        io_uring_queue_exit(&ring);
    }

    io_uring_sqe *get_sqe() {
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (sqe == nullptr) {
            io_uring_submit(&ring);
            sqe = io_uring_get_sqe(&ring);
        }
        return sqe;
    }

    void *submit_with_cb(io_uring_sqe *sqe, io_callback cb) {
        auto *heap_cb = new io_callback{std::move(cb)};
        io_uring_sqe_set_data(sqe, heap_cb);
        io_uring_submit(&ring);
        return heap_cb;
    }
};

io_context::io_context(unsigned queue_depth) : ring_(std::make_unique<ring_impl>(queue_depth)) {}

io_context::~io_context() = default;

void io_context::run() {
    ring_->running.store(true, std::memory_order_release);
    while (ring_->running.load(std::memory_order_acquire)) {
        io_uring_cqe *cqe = nullptr;
        const int ret = io_uring_wait_cqe(&ring_->ring, &cqe);
        if (ret < 0) {
            if (ret == -EINTR) {
                continue;
            }
            break;
        }
        unsigned head = 0;
        unsigned count = 0;
        io_uring_for_each_cqe(&ring_->ring, head, cqe) {
            auto *cb = static_cast<io_callback *>(io_uring_cqe_get_data(cqe));
            if (cb != nullptr) {
                (*cb)({.res = cqe->res, .flags = cqe->flags});
                delete cb;
            }
            ++count;
        }
        io_uring_cq_advance(&ring_->ring, count);
    }
}

void io_context::stop() {
    ring_->running.store(false, std::memory_order_release);
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    if (sqe != nullptr) {
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        io_uring_submit(&ring_->ring);
    }
}

void *io_context::submit_read(int fd, std::span<std::byte> buf, std::uint64_t offset,
                              io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_read(sqe, fd, buf.data(), static_cast<unsigned>(buf.size()), offset);
    return ring_->submit_with_cb(sqe, std::move(cb));
}

void *io_context::submit_write(int fd, std::span<const std::byte> buf, std::uint64_t offset,
                               io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_write(sqe, fd, buf.data(), static_cast<unsigned>(buf.size()), offset);
    return ring_->submit_with_cb(sqe, std::move(cb));
}

void *io_context::submit_accept(int fd, io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
    return ring_->submit_with_cb(sqe, std::move(cb));
}

void *io_context::submit_recv(int fd, std::span<std::byte> buf, int flags, io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_recv(sqe, fd, buf.data(), buf.size(), flags);
    return ring_->submit_with_cb(sqe, std::move(cb));
}

void *io_context::submit_send(int fd, std::span<const std::byte> buf, int flags, io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_send(sqe, fd, buf.data(), buf.size(), flags);
    return ring_->submit_with_cb(sqe, std::move(cb));
}

io_context::read_sender io_context::async_read(int fd, std::span<std::byte> buf,
                                               std::uint64_t offset) noexcept {
    return {.ctx = this, .fd = fd, .buf = buf, .offset = offset};
}

io_context::write_sender io_context::async_write(int fd, std::span<const std::byte> buf,
                                                 std::uint64_t offset) noexcept {
    return {.ctx = this, .fd = fd, .buf = buf, .offset = offset};
}

io_context::accept_sender io_context::async_accept(int fd) noexcept {
    return {.ctx = this, .fd = fd};
}

io_context::recv_sender io_context::async_recv(int fd, std::span<std::byte> buf,
                                               int flags) noexcept {
    return {.ctx = this, .fd = fd, .buf = buf, .flags = flags};
}

io_context::send_sender io_context::async_send(int fd, std::span<const std::byte> buf,
                                               int flags) noexcept {
    return {.ctx = this, .fd = fd, .buf = buf, .flags = flags};
}

std::expected<void, std::error_code>
io_context::register_buffers(std::span<const std::span<std::byte>> buffers) noexcept {
    std::vector<iovec> iovs;
    iovs.reserve(buffers.size());
    for (const auto &buf : buffers) {
        iovs.push_back(iovec{.iov_base = const_cast<void *>(static_cast<const void *>(buf.data())),
                             .iov_len = buf.size()});
    }
    const int ret =
        io_uring_register_buffers(&ring_->ring, iovs.data(), static_cast<unsigned>(iovs.size()));
    if (ret < 0) {
        return std::unexpected(std::error_code(-ret, std::generic_category()));
    }
    return {};
}

std::expected<void, std::error_code> io_context::unregister_buffers() noexcept {
    const int ret = io_uring_unregister_buffers(&ring_->ring);
    if (ret < 0) {
        return std::unexpected(std::error_code(-ret, std::generic_category()));
    }
    return {};
}

void *io_context::submit_read_fixed(int fd, unsigned buf_index, std::span<std::byte> buf,
                                    std::uint64_t offset, io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_read_fixed(sqe, fd, buf.data(), static_cast<unsigned>(buf.size()), offset,
                             static_cast<int>(buf_index));
    return ring_->submit_with_cb(sqe, std::move(cb));
}

void *io_context::submit_write_fixed(int fd, unsigned buf_index, std::span<const std::byte> buf,
                                     std::uint64_t offset, io_callback cb) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_write_fixed(sqe, fd, buf.data(), static_cast<unsigned>(buf.size()), offset,
                              static_cast<int>(buf_index));
    return ring_->submit_with_cb(sqe, std::move(cb));
}

io_context::read_fixed_sender io_context::async_read_fixed(int fd, unsigned buf_index,
                                                           std::span<std::byte> buf,
                                                           std::uint64_t offset) noexcept {
    return {.ctx = this, .fd = fd, .buf_index = buf_index, .buf = buf, .offset = offset};
}

io_context::write_fixed_sender io_context::async_write_fixed(int fd, unsigned buf_index,
                                                             std::span<const std::byte> buf,
                                                             std::uint64_t offset) noexcept {
    return {.ctx = this, .fd = fd, .buf_index = buf_index, .buf = buf, .offset = offset};
}

void io_context::submit_cancel(void *cb_ptr) {
    std::scoped_lock lock{ring_->submission_mutex};
    auto *sqe = ring_->get_sqe();
    io_uring_prep_cancel(sqe, cb_ptr, 0);
    io_uring_sqe_set_data(sqe, nullptr);
    io_uring_submit(&ring_->ring);
}

} // namespace async_runtime
