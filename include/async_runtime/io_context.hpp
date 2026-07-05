#pragma once

#include <async_runtime/sender.hpp>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace async_runtime {

class io_context;

struct io_result {
    std::int32_t res;
    std::uint32_t flags;
};

using io_callback = std::function<void(io_result)>;

namespace detail {

using io_sigs =
    stdexec::completion_signatures<stdexec::set_value_t(io_result),
                                   stdexec::set_error_t(std::error_code), stdexec::set_stopped_t()>;

template <stdexec::receiver Receiver> io_callback make_io_callback(Receiver r) {
    return [r = std::move(r)](io_result result) mutable {
        if (result.res == -ECANCELED) {
            stdexec::set_stopped(std::move(r));
        } else if (result.res < 0) {
            stdexec::set_error(std::move(r), std::error_code(-result.res, std::generic_category()));
        } else {
            stdexec::set_value(std::move(r), result);
        }
    };
}

template <typename Receiver> struct stop_helper {
    using stop_token_type = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;

    struct stop_callback_fn {
        stop_helper *self;
        void operator()() noexcept;
    };

    io_context *ctx;
    std::atomic<void *> cb_ptr{nullptr};
    std::atomic<bool> stop_called{false};
    std::optional<typename stop_token_type::template callback_type<stop_callback_fn>> stop_cb;

    explicit stop_helper(io_context *c) : ctx(c) {}

    template <typename SubmitFn> void start(Receiver &r, SubmitFn submit);
};

} // namespace detail

class io_context {
  public:
    explicit io_context(unsigned queue_depth = 256);
    ~io_context();

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    void run();
    void stop();

    struct read_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;
        std::span<std::byte> buf;
        std::uint64_t offset;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            std::span<std::byte> buf;
            std::uint64_t offset;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, std::span<std::byte> b, std::uint64_t o, Receiver r)
                : ctx(c), fd(f), buf(b), offset(o), receiver(std::move(r)), stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_read(fd, buf, offset,
                                            detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, offset, std::move(r)};
        }
    };

    struct write_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;
        std::span<const std::byte> buf;
        std::uint64_t offset;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            std::span<const std::byte> buf;
            std::uint64_t offset;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, std::span<const std::byte> b, std::uint64_t o,
                      Receiver r)
                : ctx(c), fd(f), buf(b), offset(o), receiver(std::move(r)), stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_write(fd, buf, offset,
                                             detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, offset, std::move(r)};
        }
    };

    struct accept_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, Receiver r)
                : ctx(c), fd(f), receiver(std::move(r)), stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_accept(fd, detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, std::move(r)};
        }
    };

    struct recv_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;
        std::span<std::byte> buf;
        int flags;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            std::span<std::byte> buf;
            int flags;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, std::span<std::byte> b, int fl, Receiver r)
                : ctx(c), fd(f), buf(b), flags(fl), receiver(std::move(r)), stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_recv(fd, buf, flags,
                                            detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, flags, std::move(r)};
        }
    };

    struct send_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;
        std::span<const std::byte> buf;
        int flags;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            std::span<const std::byte> buf;
            int flags;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, std::span<const std::byte> b, int fl, Receiver r)
                : ctx(c), fd(f), buf(b), flags(fl), receiver(std::move(r)), stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_send(fd, buf, flags,
                                            detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf, flags, std::move(r)};
        }
    };

    read_sender async_read(int fd, std::span<std::byte> buf, std::uint64_t offset = 0) noexcept;
    write_sender async_write(int fd, std::span<const std::byte> buf,
                             std::uint64_t offset = 0) noexcept;
    accept_sender async_accept(int fd) noexcept;
    recv_sender async_recv(int fd, std::span<std::byte> buf, int flags = 0) noexcept;
    send_sender async_send(int fd, std::span<const std::byte> buf, int flags = 0) noexcept;

    std::expected<void, std::error_code>
    register_buffers(std::span<const std::span<std::byte>> buffers) noexcept;
    std::expected<void, std::error_code> unregister_buffers() noexcept;

    struct read_fixed_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;
        unsigned buf_index;
        std::span<std::byte> buf;
        std::uint64_t offset;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            unsigned buf_index;
            std::span<std::byte> buf;
            std::uint64_t offset;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, unsigned bi, std::span<std::byte> b, std::uint64_t o,
                      Receiver r)
                : ctx(c), fd(f), buf_index(bi), buf(b), offset(o), receiver(std::move(r)),
                  stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_read_fixed(fd, buf_index, buf, offset,
                                                  detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf_index, buf, offset, std::move(r)};
        }
    };

    struct write_fixed_sender {
        using sender_concept = stdexec::sender_t;
        using completion_signatures = detail::io_sigs;
        io_context *ctx;
        int fd;
        unsigned buf_index;
        std::span<const std::byte> buf;
        std::uint64_t offset;

        template <stdexec::receiver Receiver> struct operation {
            io_context *ctx;
            int fd;
            unsigned buf_index;
            std::span<const std::byte> buf;
            std::uint64_t offset;
            Receiver receiver;
            detail::stop_helper<Receiver> stop_helper_;

            operation(io_context *c, int f, unsigned bi, std::span<const std::byte> b,
                      std::uint64_t o, Receiver r)
                : ctx(c), fd(f), buf_index(bi), buf(b), offset(o), receiver(std::move(r)),
                  stop_helper_(c) {}

            void start() & noexcept {
                stop_helper_.start(receiver, [&] {
                    return ctx->submit_write_fixed(fd, buf_index, buf, offset,
                                                   detail::make_io_callback(std::move(receiver)));
                });
            }
        };

        template <stdexec::receiver Receiver> auto connect(Receiver r) const noexcept {
            return operation<Receiver>{ctx, fd, buf_index, buf, offset, std::move(r)};
        }
    };

    read_fixed_sender async_read_fixed(int fd, unsigned buf_index, std::span<std::byte> buf,
                                       std::uint64_t offset = 0) noexcept;
    write_fixed_sender async_write_fixed(int fd, unsigned buf_index, std::span<const std::byte> buf,
                                         std::uint64_t offset = 0) noexcept;

    void submit_cancel(void *cb_ptr);

  private:
    void *submit_read(int fd, std::span<std::byte> buf, std::uint64_t offset, io_callback cb);
    void *submit_write(int fd, std::span<const std::byte> buf, std::uint64_t offset,
                       io_callback cb);
    void *submit_accept(int fd, io_callback cb);
    void *submit_recv(int fd, std::span<std::byte> buf, int flags, io_callback cb);
    void *submit_send(int fd, std::span<const std::byte> buf, int flags, io_callback cb);
    void *submit_read_fixed(int fd, unsigned buf_index, std::span<std::byte> buf,
                            std::uint64_t offset, io_callback cb);
    void *submit_write_fixed(int fd, unsigned buf_index, std::span<const std::byte> buf,
                             std::uint64_t offset, io_callback cb);

    struct ring_impl;
    std::unique_ptr<ring_impl> ring_;
};

namespace detail {

template <typename Receiver> void stop_helper<Receiver>::stop_callback_fn::operator()() noexcept {
    self->stop_called.store(true, std::memory_order_release);
    if (void *ptr = self->cb_ptr.load(std::memory_order_acquire)) {
        self->ctx->submit_cancel(ptr);
    }
}

template <typename Receiver>
template <typename SubmitFn>
void stop_helper<Receiver>::start(Receiver &r, SubmitFn submit) {
    auto stop_token = stdexec::get_stop_token(stdexec::get_env(r));
    if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(r));
        return;
    }
    if (stop_token.stop_possible()) {
        stop_cb.emplace(stop_token, stop_callback_fn{this});
    }
    void *ptr = submit();
    cb_ptr.store(ptr, std::memory_order_release);
    if (stop_called.load(std::memory_order_acquire)) {
        ctx->submit_cancel(ptr);
    }
}

} // namespace detail

} // namespace async_runtime
