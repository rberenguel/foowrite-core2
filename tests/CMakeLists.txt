cmake_minimum_required(VERSION 3.14)
project(foowrite_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(foowrite_tests
    editor_stubs.cc
    test_text_objects.cc
    test_basic_writing.cc
    test_movements.cc
    ../main/editor.cpp
    ../main/keymap.cpp
)

# stubs/ overrides ESP-IDF headers (e.g. driver/i2c_master.h) for the host build.
target_include_directories(foowrite_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../main
)

target_link_libraries(foowrite_tests GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(foowrite_tests)
