#include <array>
#include <async_runtime/io_context.hpp>
#include <atomic>
#include <exec/start_detached.hpp>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <span>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct Connection : std::enable_shared_from_this<Connection> {
    int fd;
    async_runtime::io_context *io;
    std::array<std::byte, 4096> buffer;

    Connection(int client_fd, async_runtime::io_context *io_ctx)
        : fd(client_fd), io(io_ctx), buffer{} {}

    ~Connection() {
        close(fd);
    }

    void start_session() {
        read_loop();
    }

    void read_loop() {
        auto self = shared_from_this();
        auto read_sender = io->async_recv(fd, std::span<std::byte>{buffer});

        auto handle_read = stdexec::then(std::move(read_sender),
                                         [self](async_runtime::io_result res) {
                                             if (res.res <= 0) {
                                                 return;
                                             }
                                             self->write_loop(static_cast<size_t>(res.res), 0);
                                         }) |
                           stdexec::upon_error([](auto) {}) | stdexec::upon_stopped([]() {});

        exec::start_detached(std::move(handle_read));
    }

    void write_loop(size_t total_bytes, size_t bytes_sent) {
        auto self = shared_from_this();
        auto write_sender =
            io->async_send(fd, std::span<const std::byte>{self->buffer.data() + bytes_sent,
                                                          total_bytes - bytes_sent});

        auto handle_write =
            stdexec::then(std::move(write_sender),
                          [self, total_bytes, bytes_sent](async_runtime::io_result res) {
                              if (res.res <= 0) {
                                  return;
                              }
                              size_t sent_now = static_cast<size_t>(res.res);
                              if (bytes_sent + sent_now < total_bytes) {
                                  self->write_loop(total_bytes, bytes_sent + sent_now);
                              } else {
                                  self->read_loop();
                              }
                          }) |
            stdexec::upon_error([](auto) {}) | stdexec::upon_stopped([]() {});

        exec::start_detached(std::move(handle_write));
    }
};

void run_accept_loop(int listen_fd, async_runtime::io_context &io, std::atomic<bool> &running) {
    auto accept_sender = io.async_accept(listen_fd);

    auto handle_accept = stdexec::then(std::move(accept_sender),
                                       [listen_fd, &io, &running](async_runtime::io_result res) {
                                           if (!running.load(std::memory_order_relaxed)) {
                                               return;
                                           }
                                           if (res.res >= 0) {
                                               auto conn =
                                                   std::make_shared<Connection>(res.res, &io);
                                               conn->start_session();
                                           }
                                           run_accept_loop(listen_fd, io, running);
                                       }) |
                         stdexec::upon_error([](auto) {}) | stdexec::upon_stopped([]() {});

    exec::start_detached(std::move(handle_accept));
}

int main(int argc, char *argv[]) {
    uint16_t port = 18080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    size_t num_threads = std::thread::hardware_concurrency();
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;

    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([port, &running]() {
            int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (listen_fd < 0) {
                return;
            }
            int opt = 1;
            setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;

            if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
                close(listen_fd);
                return;
            }
            if (listen(listen_fd, SOMAXCONN) < 0) {
                close(listen_fd);
                return;
            }

            async_runtime::io_context io{1024};
            run_accept_loop(listen_fd, io, running);
            io.run();
            close(listen_fd);
        });
    }

    for (auto &t : workers) {
        t.join();
    }
    return 0;
}
