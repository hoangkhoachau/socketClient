cmake_minimum_required(VERSION 3.1.0)
project(socketClient)
set (CMAKE_CXX_STANDARD 17)
set (CXX_STANDARD_REQUIRED ON)
file(GLOB SOURCES "Source/socket.cpp")
add_executable(socket ${SOURCES})
