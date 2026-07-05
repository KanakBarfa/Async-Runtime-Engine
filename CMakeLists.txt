cmake_minimum_required(VERSION 3.30)
project(async_engine LANGUAGES CXX VERSION 0.1.0)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

include(FetchContent)
FetchContent_Declare(
    stdexec
    GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stdexec)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

enable_testing()

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples)

# Package Configuration generation
set(CONFIG_TEMPLATE [=[
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# Locate liburing since async_runtime_impl links to it
find_dependency(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

# Check if stdexec is already defined (e.g. via FetchContent)
if(NOT TARGET STDEXEC::stdexec)
    find_dependency(stdexec CONFIG)
endif()

# Include the generated targets file
include("${CMAKE_CURRENT_LIST_DIR}/async_engineTargets.cmake")

# Transitively link STDEXEC::stdexec to async_runtime
set_property(TARGET async_engine::async_runtime APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES "STDEXEC::stdexec"
)
]=])

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/async_engineConfig.cmake.in" "${CONFIG_TEMPLATE}")

configure_package_config_file(
    "${CMAKE_CURRENT_BINARY_DIR}/async_engineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/async_engineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/async_engine
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/async_engineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT async_engineTargets
    FILE async_engineTargets.cmake
    NAMESPACE async_engine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/async_engine
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/async_engineConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/async_engineConfigVersion.cmake"
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/async_engine
)

