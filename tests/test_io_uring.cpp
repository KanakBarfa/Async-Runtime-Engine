#include <async_runtime/io_context.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <exec/start_detached.hpp>
#include <fcntl.h>
#include <stdexec/execution.hpp>
#include <thread>
#include <unistd.h>

struct test_receiver_env {
    stdexec::inplace_stop_token token;
    [[nodiscard]] auto query(stdexec::get_stop_token_t /*unused*/) const noexcept {
        return token;
    }
};

struct test_receiver {
    using receiver_concept = stdexec::receiver_t;
    stdexec::inplace_stop_token token;
    bool *stopped;
    bool *completed;

    void set_value(async_runtime::io_result /*unused*/) const && noexcept {
        *completed = true;
    }
    void set_error(std::error_code /*unused*/) const && noexcept {
        *completed = true;
    }
    void set_stopped() const && noexcept {
        *stopped = true;
        *completed = true;
    }

    [[nodiscard]] auto get_env() const noexcept {
        return test_receiver_env{token};
    }
};

void test_standard_io() {
    std::array<int, 2> fds{};
    const int ret = pipe(fds.data());
    assert(ret == 0);
    (void)ret;

    async_runtime::io_context ctx;
    std::thread io_thread{[&ctx] { ctx.run(); }};

    constexpr std::string_view message = "hello_io_uring";
    std::array<std::byte, 64> write_buf{};
    std::memcpy(write_buf.data(), message.data(), message.size());

    std::array<std::byte, 64> read_buf{};

    bool write_done = false;
    bool read_done = false;

    auto write_span = std::span<const std::byte>{write_buf.data(), message.size()};
    auto read_span = std::span<std::byte>{read_buf.data(), message.size()};

    exec::start_detached(
        ctx.async_write(fds[1], write_span) | stdexec::then([&](async_runtime::io_result r) {
            assert(r.res >= 0 && static_cast<std::size_t>(r.res) == message.size());
            (void)r;
            write_done = true;
        }));

    exec::start_detached(
        ctx.async_read(fds[0], read_span) | stdexec::then([&](async_runtime::io_result r) {
            assert(r.res >= 0 && static_cast<std::size_t>(r.res) == message.size());
            assert(std::memcmp(read_buf.data(), message.data(), message.size()) == 0);
            (void)r;
            read_done = true;
            ctx.stop();
        }));

    io_thread.join();

    assert(write_done);
    assert(read_done);

    close(fds[0]);
    close(fds[1]);
}

void test_fixed_buffers() {
    constexpr std::string_view message = "hello_io_uring";
    std::array<int, 2> fds_fixed{};
    const int ret_fixed = pipe(fds_fixed.data());
    assert(ret_fixed == 0);
    (void)ret_fixed;

    async_runtime::io_context ctx_fixed;

    std::array<std::byte, 64> write_buf_fixed{};
    std::memcpy(write_buf_fixed.data(), message.data(), message.size());
    std::array<std::byte, 64> read_buf_fixed{};

    std::span<std::byte> w_span{write_buf_fixed.data(), write_buf_fixed.size()};
    std::span<std::byte> r_span{read_buf_fixed.data(), read_buf_fixed.size()};

    std::array<std::span<std::byte>, 2> bufs{w_span, r_span};
    auto reg_res = ctx_fixed.register_buffers(bufs);
    assert(reg_res.has_value());
    (void)reg_res;

    std::thread io_thread_fixed{[&ctx_fixed] { ctx_fixed.run(); }};

    bool write_done_fixed = false;
    bool read_done_fixed = false;

    auto w_slice = std::span<const std::byte>{write_buf_fixed.data(), message.size()};
    auto r_slice = std::span<std::byte>{read_buf_fixed.data(), message.size()};

    exec::start_detached(ctx_fixed.async_write_fixed(fds_fixed[1], 0, w_slice) |
                         stdexec::then([&](async_runtime::io_result r) {
                             assert(r.res >= 0 &&
                                    static_cast<std::size_t>(r.res) == message.size());
                             (void)r;
                             write_done_fixed = true;
                         }));

    exec::start_detached(
        ctx_fixed.async_read_fixed(fds_fixed[0], 1, r_slice) |
        stdexec::then([&](async_runtime::io_result r) {
            assert(r.res >= 0 && static_cast<std::size_t>(r.res) == message.size());
            assert(std::memcmp(read_buf_fixed.data(), message.data(), message.size()) == 0);
            (void)r;
            read_done_fixed = true;
            ctx_fixed.stop();
        }));

    io_thread_fixed.join();

    assert(write_done_fixed);
    assert(read_done_fixed);

    auto unreg_res = ctx_fixed.unregister_buffers();
    assert(unreg_res.has_value());
    (void)unreg_res;

    close(fds_fixed[0]);
    close(fds_fixed[1]);
}

void test_io_cancellation() {
    std::array<int, 2> fds_cancel{};
    const int ret_cancel = pipe(fds_cancel.data());
    assert(ret_cancel == 0);
    (void)ret_cancel;
    assert(fcntl(fds_cancel[0], F_SETFL, O_NONBLOCK) == 0);
    assert(fcntl(fds_cancel[1], F_SETFL, O_NONBLOCK) == 0);

    async_runtime::io_context ctx_cancel;
    std::thread io_thread_cancel{[&ctx_cancel] { ctx_cancel.run(); }};

    stdexec::inplace_stop_source stop_src;
    bool stopped = false;
    bool completed = false;

    std::array<std::byte, 64> read_buf_cancel{};
    auto r_span = std::span<std::byte>{read_buf_cancel.data(), read_buf_cancel.size()};

    auto op = stdexec::connect(
        ctx_cancel.async_read(fds_cancel[0], r_span),
        test_receiver{.token = stop_src.get_token(), .stopped = &stopped, .completed = &completed});
    stdexec::start(op);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stop_src.request_stop();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    (void)deadline;
    while (!completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    ctx_cancel.stop();
    io_thread_cancel.join();

    assert(stopped);

    close(fds_cancel[0]);
    close(fds_cancel[1]);
}

int main() {
    test_standard_io();
    test_fixed_buffers();
    test_io_cancellation();
    return 0;
}
