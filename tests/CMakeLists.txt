function(add_engine_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE async_runtime_impl)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_engine_test(test_thread_pool test_thread_pool.cpp)
add_engine_test(test_io_uring test_io_uring.cpp)
add_engine_test(test_sockets test_sockets.cpp)
