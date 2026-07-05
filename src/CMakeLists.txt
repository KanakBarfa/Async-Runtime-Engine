add_library(async_runtime_impl STATIC
    runtime.cpp
    thread_pool.cpp
    io_uring_ctx.cpp
)

target_link_libraries(async_runtime_impl
    PUBLIC async_runtime
    PRIVATE ${LIBURING_LIBRARIES}
)

target_include_directories(async_runtime_impl
    PRIVATE ${LIBURING_INCLUDE_DIRS}
)

target_compile_options(async_runtime_impl PRIVATE ${LIBURING_CFLAGS_OTHER})

install(TARGETS async_runtime_impl
    EXPORT async_engineTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

