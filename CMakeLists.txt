cmake_minimum_required(VERSION 3.28.3)
project(DirectFilePushServer)

set(CMAKE_CXX_STANDARD 20)

add_executable(DirectFilePushServer main.cpp
        include/json.hpp
        main.cpp)
