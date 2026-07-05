#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct ConnectionState {
    int fd;
    enum State { CONNECTING, SENDING, RECEIVING } state;
    std::vector<char> buffer;
    size_t write_offset = 0;
    size_t read_offset = 0;
    std::chrono::high_resolution_clock::time_point start_time;
};

struct ThreadResult {
    uint64_t total_requests = 0;
    std::vector<uint32_t> latencies_us;
};

void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void run_client_thread(std::string ip, uint16_t port, size_t num_conns, size_t payload_size,
                       size_t duration_sec, ThreadResult &result, std::atomic<bool> &start_flag,
                       std::atomic<bool> &stop_flag) {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        return;
    }
    std::vector<ConnectionState> conns(num_conns);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    for (size_t i = 0; i < num_conns; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        conns[i].fd = fd;
        conns[i].state = ConnectionState::CONNECTING;
        conns[i].buffer.resize(payload_size, 'a');

        epoll_event ev{};
        ev.events = EPOLLOUT | EPOLLET;
        ev.data.ptr = &conns[i];

        connect(fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    }

    std::vector<epoll_event> events(1024);

    while (!start_flag.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
    }

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_sec);

    while (!stop_flag.load(std::memory_order_relaxed) &&
           std::chrono::steady_clock::now() < end_time) {
        int nfds = epoll_wait(epoll_fd, events.data(), events.size(), 10);
        for (int i = 0; i < nfds; ++i) {
            auto *conn = static_cast<ConnectionState *>(events[i].data.ptr);
            uint32_t revents = events[i].events;

            if (conn->state == ConnectionState::CONNECTING) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err == 0) {
                    conn->state = ConnectionState::SENDING;
                    conn->start_time = std::chrono::high_resolution_clock::now();

                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev.data.ptr = conn;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
                } else {
                    close(conn->fd);
                    conn->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
                    connect(conn->fd, reinterpret_cast<sockaddr *>(&server_addr),
                            sizeof(server_addr));
                    epoll_event ev{};
                    ev.events = EPOLLOUT | EPOLLET;
                    ev.data.ptr = conn;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev);
                }
            }

            if (revents & EPOLLOUT && conn->state == ConnectionState::SENDING) {
                while (conn->write_offset < payload_size) {
                    ssize_t n = send(conn->fd, conn->buffer.data() + conn->write_offset,
                                     payload_size - conn->write_offset, 0);
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        break;
                    }
                    conn->write_offset += n;
                }
                if (conn->write_offset == payload_size) {
                    conn->state = ConnectionState::RECEIVING;
                }
            }

            if (revents & EPOLLIN && conn->state == ConnectionState::RECEIVING) {
                while (conn->read_offset < payload_size) {
                    ssize_t n = recv(conn->fd, conn->buffer.data() + conn->read_offset,
                                     payload_size - conn->read_offset, 0);
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        break;
                    }
                    if (n == 0)
                        break;
                    conn->read_offset += n;
                }
                if (conn->read_offset == payload_size) {
                    auto now = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                                       now - conn->start_time)
                                       .count();
                    result.latencies_us.push_back(static_cast<uint32_t>(latency));
                    result.total_requests++;

                    conn->state = ConnectionState::SENDING;
                    conn->read_offset = 0;
                    conn->write_offset = 0;
                    conn->start_time = std::chrono::high_resolution_clock::now();

                    while (conn->write_offset < payload_size) {
                        ssize_t n = send(conn->fd, conn->buffer.data() + conn->write_offset,
                                         payload_size - conn->write_offset, 0);
                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                            break;
                        }
                        conn->write_offset += n;
                    }
                    if (conn->write_offset == payload_size) {
                        conn->state = ConnectionState::RECEIVING;
                    }
                }
            }
        }
    }

    for (auto &c : conns) {
        close(c.fd);
    }
    close(epoll_fd);
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    std::string ip = "127.0.0.1";
    uint16_t port = 18080;
    size_t total_conns = 1000;
    size_t client_threads = 4;
    size_t payload_size = 64;
    size_t duration = 10;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            ip = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--conns") == 0 && i + 1 < argc) {
            total_conns = std::stoul(argv[++i]);
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            client_threads = std::stoul(argv[++i]);
        } else if (std::strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            payload_size = std::stoul(argv[++i]);
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = std::stoul(argv[++i]);
        }
    }

    std::vector<std::thread> workers;
    std::vector<ThreadResult> results(client_threads);
    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};

    size_t conns_per_thread = total_conns / client_threads;

    std::cout << "Benchmarking " << ip << ":" << port << " with " << total_conns
              << " connections across " << client_threads << " threads (payload " << payload_size
              << " bytes, duration " << duration << "s)" << std::endl;

    for (size_t i = 0; i < client_threads; ++i) {
        workers.emplace_back(run_client_thread, ip, port, conns_per_thread, payload_size, duration,
                             std::ref(results[i]), std::ref(start_flag), std::ref(stop_flag));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "Starting load generator..." << std::endl;
    start_flag.store(true);

    std::this_thread::sleep_for(std::chrono::seconds(duration));
    stop_flag.store(true);

    for (auto &t : workers) {
        t.join();
    }

    uint64_t total_requests = 0;
    std::vector<uint32_t> all_latencies;
    for (const auto &res : results) {
        total_requests += res.total_requests;
        all_latencies.insert(all_latencies.end(), res.latencies_us.begin(), res.latencies_us.end());
    }

    double rps = static_cast<double>(total_requests) / duration;
    double throughput_mb =
        (static_cast<double>(total_requests * payload_size * 2) / (1024.0 * 1024.0)) / duration;

    std::sort(all_latencies.begin(), all_latencies.end());

    std::cout << "=== Benchmark Results ===" << std::endl;
    std::cout << "Total Requests: " << total_requests << std::endl;
    std::cout << "Throughput:     " << rps << " req/sec" << std::endl;
    std::cout << "Bandwidth:      " << throughput_mb << " MB/sec (combined R/W)" << std::endl;

    if (!all_latencies.empty()) {
        double avg_latency =
            std::accumulate(all_latencies.begin(), all_latencies.end(), 0.0) / all_latencies.size();
        std::cout << "Latency p50:    " << all_latencies[all_latencies.size() * 0.50] << " us"
                  << std::endl;
        std::cout << "Latency p90:    " << all_latencies[all_latencies.size() * 0.90] << " us"
                  << std::endl;
        std::cout << "Latency p99:    " << all_latencies[all_latencies.size() * 0.99] << " us"
                  << std::endl;
        std::cout << "Latency p99.9:  " << all_latencies[all_latencies.size() * 0.999] << " us"
                  << std::endl;
        std::cout << "Latency Average: " << avg_latency << " us" << std::endl;
    }

    return 0;
}
