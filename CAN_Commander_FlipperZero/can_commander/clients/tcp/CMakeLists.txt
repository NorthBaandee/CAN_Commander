cmake_minimum_required(VERSION 3.16)

project(CANCommanderTcpClient LANGUAGES CXX)

find_package(Threads REQUIRED)

add_executable(cancommander-tcp src/main.cpp)
target_compile_features(cancommander-tcp PRIVATE cxx_std_17)
target_link_libraries(cancommander-tcp PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(cancommander-tcp PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(cancommander-tcp PRIVATE /W4 /permissive-)
else()
    target_compile_options(cancommander-tcp PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING AND UNIX)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME guided-capture
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guided_capture.py"
                "$<TARGET_FILE:cancommander-tcp>"
        )
    endif()
endif()
