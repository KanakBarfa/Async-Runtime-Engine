# async_engine

A high-performance C++26 asynchronous I/O runtime engine utilizing Linux `io_uring` and C++ standard execution (`std::execution`).

---

## The Landscape: Why `async_engine`?

### Industry Context
Building highly concurrent I/O-bound applications has traditionally forced developers to choose between three suboptimal patterns:
1. **Thread-per-Connection**: Simpler to write, but hits scalability bottlenecks due to severe OS context-switching overhead and large memory footprints per thread under high load.
2. **Event-Driven Callbacks (epoll/kqueue)**: Highly scalable but prone to "callback hell", making control flow and error handling extremely difficult to maintain.
3. **Raw Coroutines (`co_await`)**: Modern but can introduce dynamic heap allocation overhead for coroutine frames on hot paths, and lacks structured concurrency out-of-the-box.

### Our Solution
`async_engine` bridges the gap by combining **structured concurrency** with **kernel-bypass I/O performance**:
* **Structured Concurrency (`std::execution` / P2300)**: We model asynchronous operations as Senders and Receivers. Senders chain deterministically and safely, avoiding raw callback nests and minimizing coroutine allocation overhead.
* **Kernel-Bypass Performance (`io_uring`)**: All asynchronous I/O system calls are offloaded directly to Linux `io_uring` queues, reducing transition overhead between user space and kernel space.
* **Lock-Free CPU Scheduling**: Utilizes a work-stealing scheduler based on a Chase-Lev deque per thread to keep CPU cache lines warm and minimize lock contention.

---

## Onboarding: Getting Started

### Prerequisites
* **Operating System**: Linux (Kernel 5.10+ recommended for complete `io_uring` support)
* **Compiler**: GCC 15+ (supporting C++26 standard features)
* **Build Tools**: CMake (>= 3.30) and Ninja
* **System Dependency**: `liburing` (version >= 2.14)

### Clone & Build
```bash
# Clone the repository
git clone https://github.com/kanak/async_engine.git
cd async_engine

# Configure and compile using Ninja
cmake -B build -S . -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=gcc-15 \
      -DCMAKE_CXX_COMPILER=g++-15

cmake --build build --config Release
```

### Running the Tests
To verify the build, run the test suite:
```bash
cd build && ctest --output-on-failure
```

### Running the Example Echo Server
An asynchronous TCP echo server is compiled during the build. To launch it:
```bash
./examples/echo_server
```

You can send TCP packets to it using `nc` or `telnet`:
```bash
nc localhost 18080
# Type a message, and it will echo back and close the connection
```

---

## Project Documentation

Detailed technical breakdowns and developer guidelines are split into dedicated documents:
* [**Architecture & Concurrency Design**](docs/architecture.md): Explore the lock-free Chase-Lev deque scheduler, Pimpl encapsulated `io_context`, zero-copy registered buffers, and P2300 stop-token cancellation.
* [**Contributing Guidelines**](docs/contributing.md): Read about formatting guidelines (`clang-format`), static analysis rules (`clang-tidy`), and compiler database setups used in our CI pipeline.
