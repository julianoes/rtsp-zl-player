cmake_minimum_required(VERSION 3.16)
project(rtsp-zl-player CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)

add_executable(rtsp-zl-player main.cpp)
target_link_libraries(rtsp-zl-player PRIVATE PkgConfig::GST)
target_compile_options(rtsp-zl-player PRIVATE -Wall -Wextra -Wpedantic)
