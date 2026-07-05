#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

struct ConnectionContext {
    int fd;
    std::vector<char> buffer;
    size_t read_bytes = 0;
    size_t write_bytes = 0;
    bool want_write = false;

    explicit ConnectionContext(int client_fd = -1)
        : fd(client_fd), buffer(), read_bytes(0), write_bytes(0), want_write(false) {}
};

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void run_epoll_reactor(uint16_t port, std::atomic<bool> &running) {
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        close(listen_fd);
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    std::vector<epoll_event> events(1024);
    std::unordered_map<int, ConnectionContext> conns;

    while (running.load(std::memory_order_relaxed)) {
        int n_fds = epoll_wait(epoll_fd, events.data(), events.size(), 100);
        for (int i = 0; i < n_fds; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            if (fd == listen_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept4(listen_fd, reinterpret_cast<sockaddr *>(&client_addr),
                                            &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        break;
                    }

                    ConnectionContext ctx(client_fd);
                    ctx.buffer.resize(4096);
                    conns[client_fd] = std::move(ctx);

                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                    cev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);
                }
            } else {
                if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    conns.erase(fd);
                    continue;
                }

                auto &ctx = conns[fd];
                if (revents & EPOLLIN) {
                    bool closed = false;
                    while (true) {
                        ssize_t n = recv(fd, ctx.buffer.data() + ctx.read_bytes,
                                         ctx.buffer.size() - ctx.read_bytes, 0);
                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                            closed = true;
                            break;
                        }
                        if (n == 0) {
                            closed = true;
                            break;
                        }
                        ctx.read_bytes += n;
                        if (ctx.read_bytes == ctx.buffer.size()) {
                            ctx.buffer.resize(ctx.buffer.size() * 2);
                        }
                    }

                    if (closed) {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        conns.erase(fd);
                        continue;
                    }

                    if (ctx.read_bytes > ctx.write_bytes) {
                        ssize_t n = send(fd, ctx.buffer.data() + ctx.write_bytes,
                                         ctx.read_bytes - ctx.write_bytes, 0);
                        if (n >= 0) {
                            ctx.write_bytes += n;
                        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            closed = true;
                        }

                        if (closed) {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            conns.erase(fd);
                            continue;
                        }

                        if (ctx.write_bytes < ctx.read_bytes) {
                            if (!ctx.want_write) {
                                epoll_event cev{};
                                cev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
                                cev.data.fd = fd;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
                                ctx.want_write = true;
                            }
                        } else {
                            ctx.read_bytes = 0;
                            ctx.write_bytes = 0;
                        }
                    }
                }

                if (revents & EPOLLOUT) {
                    if (ctx.read_bytes > ctx.write_bytes) {
                        ssize_t n = send(fd, ctx.buffer.data() + ctx.write_bytes,
                                         ctx.read_bytes - ctx.write_bytes, 0);
                        if (n >= 0) {
                            ctx.write_bytes += n;
                        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                            close(fd);
                            conns.erase(fd);
                            continue;
                        }
                    }
                    if (ctx.write_bytes == ctx.read_bytes) {
                        epoll_event cev{};
                        cev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                        cev.data.fd = fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &cev);
                        ctx.want_write = false;
                        ctx.read_bytes = 0;
                        ctx.write_bytes = 0;
                    }
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
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

    std::cout << "Epoll server listening on port " << port << " with " << num_threads << " workers"
              << std::endl;

    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(run_epoll_reactor, port, std::ref(running));
    }

    for (auto &t : workers) {
        t.join();
    }
    return 0;
}
