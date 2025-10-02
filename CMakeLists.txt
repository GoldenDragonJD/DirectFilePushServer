cmake_minimum_required(VERSION 4.0)
project(DirectFilePushServer)

set(CMAKE_CXX_STANDARD 20)

add_executable(DirectFilePushServer main.cpp
        include/json.hpp
        main.cpp)
