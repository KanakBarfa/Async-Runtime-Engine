#include <array>
#include <async_runtime/io_context.hpp>
#include <atomic>
#include <exec/start_detached.hpp>
#include <exec/task.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <signal.h>
#include <span>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

class CoroutineServer {
  public:
    explicit CoroutineServer(uint16_t port, size_t num_threads)
        : port_(port), num_threads_(num_threads), running_(false) {}

    ~CoroutineServer() {
        stop();
    }

    void start() {
        running_ = true;
        for (size_t i = 0; i < num_threads_; ++i) {
            threads_.emplace_back([this]() { run_thread(); });
        }
    }

    void stop() {
        if (!running_)
            return;
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (auto *ctx : contexts_) {
                if (ctx)
                    ctx->stop();
            }
            contexts_.clear();
        }
        for (auto &t : threads_) {
            if (t.joinable())
                t.join();
        }
        threads_.clear();
    }

  private:
    void run_thread() {
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            return;
        }
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            close(listen_fd);
            return;
        }
        if (listen(listen_fd, SOMAXCONN) < 0) {
            close(listen_fd);
            return;
        }

        auto io = std::make_unique<async_runtime::io_context>(1024);
        {
            std::lock_guard<std::mutex> lock(mu_);
            contexts_.push_back(io.get());
        }

        exec::start_detached(
            stdexec::starts_on(stdexec::inline_scheduler{}, accept_loop(listen_fd, *io)) |
            stdexec::upon_error([](auto) {}) | stdexec::upon_stopped([]() {}));
        io->run();
        close(listen_fd);
    }

    exec::task<void> handle_session(int client_fd, async_runtime::io_context &io) {
        std::array<std::byte, 4096> buf{};
        while (running_.load(std::memory_order_relaxed)) {
            auto recv_res = co_await io.async_recv(client_fd, buf);
            if (recv_res.res <= 0)
                break;

            size_t bytes_to_send = static_cast<size_t>(recv_res.res);
            size_t bytes_sent = 0;
            while (bytes_sent < bytes_to_send) {
                auto send_res = co_await io.async_send(
                    client_fd, std::span<const std::byte>{buf.data() + bytes_sent,
                                                          bytes_to_send - bytes_sent});
                if (send_res.res <= 0)
                    break;
                bytes_sent += static_cast<size_t>(send_res.res);
            }
            if (bytes_sent < bytes_to_send)
                break;
        }
        close(client_fd);
        co_return;
    }

    exec::task<void> accept_loop(int listen_fd, async_runtime::io_context &io) {
        while (running_.load(std::memory_order_relaxed)) {
            auto accept_res = co_await io.async_accept(listen_fd);
            if (accept_res.res >= 0) {
                exec::start_detached(stdexec::starts_on(stdexec::inline_scheduler{},
                                                        handle_session(accept_res.res, io)) |
                                     stdexec::upon_error([](auto) {}) |
                                     stdexec::upon_stopped([]() {}));
            } else {
                if (accept_res.res == -ECANCELED)
                    break;
                std::this_thread::yield();
            }
        }
        co_return;
    }

    uint16_t port_;
    size_t num_threads_;
    std::atomic<bool> running_;
    std::vector<std::thread> threads_;
    std::vector<async_runtime::io_context *> contexts_;
    std::mutex mu_;
};

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    uint16_t port = 18080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    size_t num_threads = std::thread::hardware_concurrency();
    std::cout << "Coroutine server listening on port " << port << " with " << num_threads
              << " workers" << std::endl;

    CoroutineServer server{port, num_threads};
    server.start();

    // Loop until we are terminated
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
