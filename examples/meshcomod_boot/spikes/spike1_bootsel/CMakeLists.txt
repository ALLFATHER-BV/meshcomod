# ESP-IDF project entry point (PlatformIO sets IDF_PATH during the build).
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Pin the app version so ESP-IDF doesn't run `git describe` for it. The fresh repo
# has no commits yet, which otherwise makes that call fail during CMake configure.
set(PROJECT_VER "0.1.0-spike1")
project(spike1_bootsel)
