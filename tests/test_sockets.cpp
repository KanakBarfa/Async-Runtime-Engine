#include <async_runtime/io_context.hpp>

#include <arpa/inet.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <exec/start_detached.hpp>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexec/execution.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

struct test_receiver_env {
    stdexec::inplace_stop_token token;
    [[nodiscard]] auto query(stdexec::get_stop_token_t /*unused*/) const noexcept {
        return token;
    }
};

struct socket_test_receiver {
    using receiver_concept = stdexec::receiver_t;
    stdexec::inplace_stop_token token;
    bool *stopped = nullptr;
    bool *completed = nullptr;
    bool *error_called = nullptr;
    std::error_code *err_code = nullptr;
    async_runtime::io_result *val_result = nullptr;

    void set_value(async_runtime::io_result r) const && noexcept {
        if (val_result != nullptr) {
            *val_result = r;
        }
        if (completed != nullptr) {
            *completed = true;
        }
    }
    void set_error(std::error_code ec) const && noexcept {
        if (error_called != nullptr) {
            *error_called = true;
        }
        if (err_code != nullptr) {
            *err_code = ec;
        }
        if (completed != nullptr) {
            *completed = true;
        }
    }
    void set_stopped() const && noexcept {
        if (stopped != nullptr) {
            *stopped = true;
        }
        if (completed != nullptr) {
            *completed = true;
        }
    }

    [[nodiscard]] auto get_env() const noexcept {
        return test_receiver_env{token};
    }
};

void test_tcp_loopback() {
    async_runtime::io_context ctx;
    std::thread io_thread{[&ctx] { ctx.run(); }};

    std::uint16_t port = 0;
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_fd >= 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int ret = bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    assert(ret >= 0);
    ret = listen(listen_fd, SOMAXCONN);
    assert(ret >= 0);

    socklen_t addr_len = sizeof(addr);
    ret = getsockname(listen_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len);
    assert(ret >= 0);
    port = ntohs(addr.sin_port);

    bool accept_completed = false;
    async_runtime::io_result accept_res{};
    stdexec::inplace_stop_source accept_stop;
    auto accept_op = stdexec::connect(ctx.async_accept(listen_fd),
                                      socket_test_receiver{.token = accept_stop.get_token(),
                                                           .completed = &accept_completed,
                                                           .val_result = &accept_res});
    stdexec::start(accept_op);

    int client_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(client_fd >= 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    ret = connect(client_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
    assert(ret == 0 || errno == EINPROGRESS);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!accept_completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    assert(accept_res.res >= 0);
    int accepted_fd = accept_res.res;

    constexpr std::string_view msg = "hello_sockets";
    std::array<std::byte, 64> send_buf{};
    std::memcpy(send_buf.data(), msg.data(), msg.size());

    std::array<std::byte, 64> recv_buf{};
    bool recv_completed = false;
    async_runtime::io_result recv_res{};
    stdexec::inplace_stop_source recv_stop;
    auto recv_op = stdexec::connect(
        ctx.async_recv(accepted_fd, std::span<std::byte>{recv_buf.data(), msg.size()}),
        socket_test_receiver{
            .token = recv_stop.get_token(), .completed = &recv_completed, .val_result = &recv_res});
    stdexec::start(recv_op);

    bool send_completed = false;
    async_runtime::io_result send_res{};
    stdexec::inplace_stop_source send_stop;
    auto send_op = stdexec::connect(
        ctx.async_send(client_fd, std::span<const std::byte>{send_buf.data(), msg.size()}),
        socket_test_receiver{
            .token = send_stop.get_token(), .completed = &send_completed, .val_result = &send_res});
    stdexec::start(send_op);

    while (!recv_completed || !send_completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    assert(send_res.res >= 0 && static_cast<std::size_t>(send_res.res) == msg.size());
    assert(recv_res.res >= 0 && static_cast<std::size_t>(recv_res.res) == msg.size());
    assert(std::memcmp(recv_buf.data(), msg.data(), msg.size()) == 0);

    close(accepted_fd);
    close(client_fd);
    close(listen_fd);

    ctx.stop();
    io_thread.join();
}

void test_socket_cancellation() {
    async_runtime::io_context ctx;
    std::thread io_thread{[&ctx] { ctx.run(); }};

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_fd >= 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ret = bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    assert(ret >= 0);
    ret = listen(listen_fd, SOMAXCONN);
    assert(ret >= 0);

    stdexec::inplace_stop_source stop_src;
    bool stopped = false;
    bool completed = false;

    auto op = stdexec::connect(ctx.async_accept(listen_fd),
                               socket_test_receiver{.token = stop_src.get_token(),
                                                    .stopped = &stopped,
                                                    .completed = &completed});
    stdexec::start(op);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stop_src.request_stop();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    assert(stopped);

    close(listen_fd);
    ctx.stop();
    io_thread.join();
}

void test_recv_cancellation() {
    async_runtime::io_context ctx;
    std::thread io_thread{[&ctx] { ctx.run(); }};

    std::uint16_t port = 0;
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_fd >= 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ret = bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    assert(ret >= 0);
    ret = listen(listen_fd, SOMAXCONN);
    assert(ret >= 0);
    socklen_t addr_len = sizeof(addr);
    ret = getsockname(listen_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len);
    assert(ret >= 0);
    port = ntohs(addr.sin_port);

    bool accept_completed = false;
    async_runtime::io_result accept_res{};
    stdexec::inplace_stop_source accept_stop;
    auto accept_op = stdexec::connect(ctx.async_accept(listen_fd),
                                      socket_test_receiver{.token = accept_stop.get_token(),
                                                           .completed = &accept_completed,
                                                           .val_result = &accept_res});
    stdexec::start(accept_op);

    int client_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(client_fd >= 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ret = connect(client_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
    assert(ret == 0 || errno == EINPROGRESS);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!accept_completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    int accepted_fd = accept_res.res;
    assert(accepted_fd >= 0);

    std::array<std::byte, 64> recv_buf{};
    bool recv_stopped = false;
    bool recv_completed = false;
    stdexec::inplace_stop_source recv_stop_src;

    auto recv_op = stdexec::connect(ctx.async_recv(accepted_fd, recv_buf),
                                    socket_test_receiver{.token = recv_stop_src.get_token(),
                                                         .stopped = &recv_stopped,
                                                         .completed = &recv_completed});
    stdexec::start(recv_op);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    recv_stop_src.request_stop();

    while (!recv_completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    assert(recv_stopped);

    close(accepted_fd);
    close(client_fd);
    close(listen_fd);
    ctx.stop();
    io_thread.join();
}

void test_error_handling() {
    async_runtime::io_context ctx;
    std::thread io_thread{[&ctx] { ctx.run(); }};

    std::array<std::byte, 64> buf{};
    bool completed = false;
    bool error_called = false;
    std::error_code err_code{};

    stdexec::inplace_stop_source stop_src;
    auto op = stdexec::connect(ctx.async_read(-1, buf),
                               socket_test_receiver{.token = stop_src.get_token(),
                                                    .completed = &completed,
                                                    .error_called = &error_called,
                                                    .err_code = &err_code});
    stdexec::start(op);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    assert(error_called);
    assert(err_code.value() == EBADF);
    assert(err_code.category() == std::generic_category());

    ctx.stop();
    io_thread.join();
}

int main() {
    test_tcp_loopback();
    test_socket_cancellation();
    test_recv_cancellation();
    test_error_handling();
    return 0;
}
