add_library(async_runtime INTERFACE)

target_include_directories(async_runtime INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(async_runtime INTERFACE STDEXEC::stdexec)
target_compile_features(async_runtime INTERFACE cxx_std_26)
