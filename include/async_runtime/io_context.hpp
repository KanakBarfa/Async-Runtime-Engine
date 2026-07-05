#pragma once

#include <async_runtime/sender.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <system_error>

namespace async_runtime {

struct io_result {
    std::int32_t res;
    std::uint32_t flags;
};

using io_callback = std::function<void(io_result)>;

namespace detail {

using io_sigs = stdexec::completion_signatures<stdexec::set_value_t(io_result),
                                               stdexec::set_error_t(std::error_code),
                                               stdexec::set_stopped_t()>;

template <stdexec::receiver Receiver>
io_callback make_io_callback(Receiver r) {
    return [r = std::move(r)](io_result result) mutable {
        if (result.res < 0) {
            stdexec::set_error(std::move(r),
                               std::error_code(-result.res, std::generic_category()));
        } else {
            stdexec::set_value(std::move(r), result);
        }
    };
}

} // namespace detail

class io_context {
public:
    explicit io_context(unsigned queue_depth = 256);
    ~io_context();

    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;

    void run();
    void stop();

    struct read_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context* ctx;
        int fd;
        std::span<std::byte> buf;
        std::uint64_t offset;

        template <stdexec::receiver Receiver>
        struct operation {
            io_context* ctx;
            int fd;
            std::span<std::byte> buf;
            std::uint64_t offset;
            Receiver receiver;
            void start() & noexcept {
                ctx->submit_read(fd, buf, offset, detail::make_io_callback(std::move(receiver)));
            }
        };

        template <stdexec::receiver Receiver>
        auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, offset, std::move(r)};
        }
    };

    struct write_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context* ctx;
        int fd;
        std::span<const std::byte> buf;
        std::uint64_t offset;

        template <stdexec::receiver Receiver>
        struct operation {
            io_context* ctx;
            int fd;
            std::span<const std::byte> buf;
            std::uint64_t offset;
            Receiver receiver;
            void start() & noexcept {
                ctx->submit_write(fd, buf, offset, detail::make_io_callback(std::move(receiver)));
            }
        };

        template <stdexec::receiver Receiver>
        auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, offset, std::move(r)};
        }
    };

    struct accept_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context* ctx;
        int fd;

        template <stdexec::receiver Receiver>
        struct operation {
            io_context* ctx;
            int fd;
            Receiver receiver;
            void start() & noexcept {
                ctx->submit_accept(fd, detail::make_io_callback(std::move(receiver)));
            }
        };

        template <stdexec::receiver Receiver>
        auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, std::move(r)};
        }
    };

    struct recv_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context* ctx;
        int fd;
        std::span<std::byte> buf;
        int flags;

        template <stdexec::receiver Receiver>
        struct operation {
            io_context* ctx;
            int fd;
            std::span<std::byte> buf;
            int flags;
            Receiver receiver;
            void start() & noexcept {
                ctx->submit_recv(fd, buf, flags, detail::make_io_callback(std::move(receiver)));
            }
        };

        template <stdexec::receiver Receiver>
        auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, flags, std::move(r)};
        }
    };

    struct send_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context* ctx;
        int fd;
        std::span<const std::byte> buf;
        int flags;

        template <stdexec::receiver Receiver>
        struct operation {
            io_context* ctx;
            int fd;
            std::span<const std::byte> buf;
            int flags;
            Receiver receiver;
            void start() & noexcept {
                ctx->submit_send(fd, buf, flags, detail::make_io_callback(std::move(receiver)));
            }
        };

        template <stdexec::receiver Receiver>
        auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, flags, std::move(r)};
        }
    };

    read_sender async_read(int fd, std::span<std::byte> buf, std::uint64_t offset = 0) noexcept;
    write_sender async_write(int fd, std::span<const std::byte> buf,
                             std::uint64_t offset = 0) noexcept;
    accept_sender async_accept(int fd) noexcept;
    recv_sender async_recv(int fd, std::span<std::byte> buf, int flags = 0) noexcept;
    send_sender async_send(int fd, std::span<const std::byte> buf, int flags = 0) noexcept;

private:
    void submit_read(int fd, std::span<std::byte> buf, std::uint64_t offset, io_callback cb);
    void submit_write(int fd, std::span<const std::byte> buf, std::uint64_t offset, io_callback cb);
    void submit_accept(int fd, io_callback cb);
    void submit_recv(int fd, std::span<std::byte> buf, int flags, io_callback cb);
    void submit_send(int fd, std::span<const std::byte> buf, int flags, io_callback cb);

    struct ring_impl;
    std::unique_ptr<ring_impl> ring_;
};

} // namespace async_runtime
