cmake_minimum_required(VERSION 3.16)
project(os-cca-kalman
    VERSION 1.0.0
    DESCRIPTION "Operating Systems CCA (CT-353) - Cross-Platform System Calls & Kalman Filter Benchmarking"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Warning flags configured per compiler
if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /EHsc
        /wd4206  # Disable warning for empty translation units during phased development
    )
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(
        -Wall
        -Wextra
        -pedantic
        -O2
    )
endif()

# Common include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

# Common Library: Kalman Filter and Thread-Safe Logger
add_library(os_common STATIC
    common/kalman.cpp
    common/logger.cpp
)
target_link_libraries(os_common PUBLIC Threads::Threads)
target_include_directories(os_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Standalone Sensor Data Generator
add_executable(sensor_gen
    common/sensor_gen.cpp
)
target_link_libraries(sensor_gen PRIVATE
    os_common
)

# Target for running sensor data generation
add_custom_target(generate_data
    COMMAND sensor_gen "${CMAKE_CURRENT_SOURCE_DIR}/data"
    DEPENDS sensor_gen
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating synthetic sensor and truth datasets..."
)

# Platform-specific builds
if(WIN32)
    add_library(os_platform_win STATIC
        windows/sys_proc_win.cpp
        windows/sys_file_win.cpp
        windows/sys_mem_win.cpp
        windows/sys_ipc_win.cpp
        windows/sys_perm_win.cpp
    )
    target_link_libraries(os_platform_win PUBLIC os_common psapi advapi32)

    add_executable(syscall_bench_win
        windows/syscall_bench_windows.cpp
    )
    target_link_libraries(syscall_bench_win PRIVATE os_platform_win)

    add_executable(kalman_win
        windows/kalman_windows.cpp
    )
    target_link_libraries(kalman_win PRIVATE os_platform_win)

elseif(UNIX AND NOT APPLE)
    add_library(os_platform_linux STATIC
        linux/sys_proc.cpp
        linux/sys_file.cpp
        linux/sys_mem.cpp
        linux/sys_ipc.cpp
        linux/sys_perm.cpp
    )
    target_link_libraries(os_platform_linux PUBLIC os_common)

    add_executable(syscall_bench_linux
        linux/syscall_bench_linux.cpp
    )
    target_link_libraries(syscall_bench_linux PRIVATE os_platform_linux)

    add_executable(kalman_linux
        linux/kalman_linux.cpp
    )
    target_link_libraries(kalman_linux PRIVATE os_platform_linux)
endif()
