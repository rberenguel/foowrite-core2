cmake_minimum_required(VERSION 3.16)

# Board selection — pass -DFOOWRITE_BOARD=waveshare349 for the Waveshare build.
# Default: core2
if(NOT DEFINED FOOWRITE_BOARD)
    set(FOOWRITE_BOARD "core2" CACHE STRING "Target board: core2 | waveshare349")
endif()
message(STATUS "foowrite target board: ${FOOWRITE_BOARD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(foowrite_core2)
