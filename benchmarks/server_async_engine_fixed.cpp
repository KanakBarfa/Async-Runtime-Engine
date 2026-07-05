#include <async_runtime/io_context.hpp>
#include <exec/start_detached.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <iostream>
#include <array>
#include <span>
#include <signal.h>
#include <stdexcept>
#include <system_error>
#include <optional>

class FixedBufferPool {
public:
    static constexpr size_t BUFFER_SIZE = 128;
    static constexpr size_t NUM_BUFFERS = 64;

    explicit FixedBufferPool(async_runtime::io_context &io) : io_(io), registered_(false) {
        memory_.resize(NUM_BUFFERS * BUFFER_SIZE);

        std::vector<std::span<std::byte>> buffer_spans;
        buffer_spans.reserve(NUM_BUFFERS);
        for (size_t i = 0; i < NUM_BUFFERS; ++i) {
            buffer_spans.emplace_back(memory_.data() + i * BUFFER_SIZE, BUFFER_SIZE);
        }

        auto reg_res = io_.register_buffers(buffer_spans);
        if (reg_res) {
            registered_ = true;
            free_indices_.reserve(NUM_BUFFERS);
            for (size_t i = 0; i < NUM_BUFFERS; ++i) {
                free_indices_.push_back(static_cast<unsigned>(NUM_BUFFERS - 1 - i));
            }
        } else {
            std::cerr << "Warning: register_buffers failed (" << reg_res.error().message()
                      << "). Falling back to unregistered buffers." << std::endl;
        }
    }

    ~FixedBufferPool() {
        if (registered_) {
            (void)io_.unregister_buffers();
        }
    }

    bool is_registered() const noexcept { return registered_; }

    std::optional<unsigned> acquire() {
        if (!registered_ || free_indices_.empty()) {
            return std::nullopt;
        }
        unsigned index = free_indices_.back();
        free_indices_.pop_back();
        return index;
    }

    void release(unsigned index) {
        if (registered_) {
            free_indices_.push_back(index);
        }
    }

    std::span<std::byte> get_span(unsigned index) {
        return {memory_.data() + index * BUFFER_SIZE, BUFFER_SIZE};
    }

private:
    async_runtime::io_context &io_;
    bool registered_;
    std::vector<std::byte> memory_;
    std::vector<unsigned> free_indices_;
};

struct Connection : std::enable_shared_from_this<Connection> {
    int fd;
    async_runtime::io_context *io;
    FixedBufferPool *pool;
    std::optional<unsigned> buf_index;
    std::span<std::byte> buffer;
    std::vector<std::byte> fallback_buffer;

    Connection(int client_fd, async_runtime::io_context *io_ctx, FixedBufferPool *buf_pool)
        : fd(client_fd), io(io_ctx), pool(buf_pool) {
        buf_index = pool->acquire();
        if (buf_index.has_value()) {
            buffer = pool->get_span(*buf_index);
        } else {
            fallback_buffer.resize(128);
            buffer = std::span<std::byte>{fallback_buffer.data(), fallback_buffer.size()};
        }
    }

    ~Connection() {
        if (buf_index.has_value()) {
            pool->release(*buf_index);
        }
        close(fd);
    }

    void start_session() {
        read_loop();
    }

    void read_loop() {
        auto self = shared_from_this();
        if (buf_index.has_value()) {
            auto read_sender = io->async_read_fixed(fd, *buf_index, buffer);
            auto handle_read = stdexec::then(std::move(read_sender), [self](async_runtime::io_result res) {
                if (res.res <= 0) return;
                self->write_loop(static_cast<size_t>(res.res), 0);
            });
            exec::start_detached(std::move(handle_read));
        } else {
            auto read_sender = io->async_recv(fd, buffer);
            auto handle_read = stdexec::then(std::move(read_sender), [self](async_runtime::io_result res) {
                if (res.res <= 0) return;
                self->write_loop(static_cast<size_t>(res.res), 0);
            });
            exec::start_detached(std::move(handle_read));
        }
    }

    void write_loop(size_t total_bytes, size_t bytes_sent) {
        auto self = shared_from_this();
        if (buf_index.has_value()) {
            auto write_sender = io->async_write_fixed(
                fd,
                *buf_index,
                std::span<const std::byte>{self->buffer.data() + bytes_sent, total_bytes - bytes_sent}
            );
            auto handle_write = stdexec::then(
                std::move(write_sender), [self, total_bytes, bytes_sent](async_runtime::io_result res) {
                    if (res.res <= 0) return;
                    size_t sent_now = static_cast<size_t>(res.res);
                    if (bytes_sent + sent_now < total_bytes) {
                        self->write_loop(total_bytes, bytes_sent + sent_now);
                    } else {
                        self->read_loop();
                    }
                });
            exec::start_detached(std::move(handle_write));
        } else {
            auto write_sender = io->async_send(
                fd,
                std::span<const std::byte>{self->buffer.data() + bytes_sent, total_bytes - bytes_sent}
            );
            auto handle_write = stdexec::then(
                std::move(write_sender), [self, total_bytes, bytes_sent](async_runtime::io_result res) {
                    if (res.res <= 0) return;
                    size_t sent_now = static_cast<size_t>(res.res);
                    if (bytes_sent + sent_now < total_bytes) {
                        self->write_loop(total_bytes, bytes_sent + sent_now);
                    } else {
                        self->read_loop();
                    }
                });
            exec::start_detached(std::move(handle_write));
        }
    }
};

void run_accept_loop(int listen_fd, async_runtime::io_context &io, FixedBufferPool &pool, std::atomic<bool> &running) {
    auto accept_sender = io.async_accept(listen_fd);

    auto handle_accept = stdexec::then(
        std::move(accept_sender), [listen_fd, &io, &pool, &running](async_runtime::io_result res) {
            if (!running.load(std::memory_order_relaxed)) {
                return;
            }
            if (res.res >= 0) {
                try {
                    auto conn = std::make_shared<Connection>(res.res, &io, &pool);
                    conn->start_session();
                } catch (const std::exception &e) {
                    std::cerr << "Failed to handle connection: " << e.what() << std::endl;
                    close(res.res);
                }
            }
            run_accept_loop(listen_fd, io, pool, running);
        });

    exec::start_detached(std::move(handle_accept));
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

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
            FixedBufferPool pool{io};
            run_accept_loop(listen_fd, io, pool, running);
            io.run();
            close(listen_fd);
        });
    }

    for (auto &t : workers) {
        t.join();
    }
    return 0;
}
