#include <async_runtime/runtime.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <exec/start_detached.hpp>
#include <netinet/in.h>
#include <stdexec/execution.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static int make_listen_socket(std::uint16_t port) {
    const auto fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return -1;
    }
    auto opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, SOMAXCONN) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main() {
    constexpr std::uint16_t port = 18080;
    const auto listen_fd = make_listen_socket(port);
    if (listen_fd < 0) {
        return 1;
    }

    async_runtime::runtime rt;
    auto &io = rt.get_io_context();

    std::thread io_thread{[&rt] { rt.run(); }};

    exec::start_detached(
        io.async_accept(listen_fd) | stdexec::then([&io](async_runtime::io_result accept_result) {
            if (accept_result.res < 0) {
                return;
            }
            const auto client_fd = accept_result.res;
            static std::array<std::byte, 4096> buf{};
            exec::start_detached(
                io.async_recv(client_fd, std::span<std::byte>{buf}) |
                stdexec::then([&io, client_fd](async_runtime::io_result recv_result) {
                    if (recv_result.res <= 0) {
                        close(client_fd);
                        return;
                    }
                    const auto n = static_cast<std::size_t>(recv_result.res);
                    exec::start_detached(
                        io.async_send(client_fd, std::span<const std::byte>{buf.data(), n}) |
                        stdexec::then([client_fd](async_runtime::io_result) { close(client_fd); }));
                }));
        }));

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    rt.stop();
    io_thread.join();
    close(listen_fd);
    return 0;
}
