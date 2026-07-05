# Add compilation flags for C++26 standard
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Benchmark Client Target
add_executable(benchmark_client benchmark_client.cpp)
target_link_libraries(benchmark_client PRIVATE Threads::Threads)

# Server 1: async_engine
add_executable(server_async_engine server_async_engine.cpp)
target_link_libraries(server_async_engine PRIVATE async_runtime_impl Threads::Threads)

# Server 2: Thread-per-Connection
add_executable(server_thread_per_connection server_thread_per_connection.cpp)
target_link_libraries(server_thread_per_connection PRIVATE Threads::Threads)

# Server 3: Event-Driven Epoll
add_executable(server_epoll server_epoll.cpp)
target_link_libraries(server_epoll PRIVATE Threads::Threads)

# Server 4: Coroutine Server
add_executable(server_coroutine server_coroutine.cpp)
target_link_libraries(server_coroutine PRIVATE async_runtime_impl Threads::Threads)

# Server 5: async_engine with fixed buffers
add_executable(server_async_engine_fixed server_async_engine_fixed.cpp)
target_link_libraries(server_async_engine_fixed PRIVATE async_runtime_impl Threads::Threads)
