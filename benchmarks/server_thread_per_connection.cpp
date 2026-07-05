#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

void handle_client(int client_fd) {
    timeval tv{.tv_sec = 10, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::vector<char> buffer(4096);
    while (true) {
        ssize_t n = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (n <= 0) {
            break;
        }

        ssize_t total_sent = 0;
        while (total_sent < n) {
            ssize_t sent = send(client_fd, buffer.data() + total_sent, n - total_sent, 0);
            if (sent <= 0) {
                break;
            }
            total_sent += sent;
        }
        if (total_sent < n) {
            break;
        }
    }
    close(client_fd);
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    uint16_t port = 18080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket" << std::endl;
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        close(listen_fd);
        return 1;
    }

    std::cout << "Thread-per-connection server listening on port " << port << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &len);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd).detach();
        } else {
            break;
        }
    }

    close(listen_fd);
    return 0;
}
