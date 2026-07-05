#include <async_runtime/io_context.hpp>

#include <exec/start_detached.hpp>
#include <stdexec/execution.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <thread>
#include <unistd.h>

int main() {
    int fds[2];
    const int ret = pipe(fds);
    assert(ret == 0);

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
            assert(r.res == static_cast<int>(message.size()));
            write_done = true;
        })
    );

    exec::start_detached(
        ctx.async_read(fds[0], read_span) | stdexec::then([&](async_runtime::io_result r) {
            assert(r.res == static_cast<int>(message.size()));
            assert(std::memcmp(read_buf.data(), message.data(), message.size()) == 0);
            read_done = true;
            ctx.stop();
        })
    );

    io_thread.join();

    assert(write_done);
    assert(read_done);

    close(fds[0]);
    close(fds[1]);
    return 0;
}
