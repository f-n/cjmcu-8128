cmake_minimum_required(VERSION 3.7)
project(cjmcu)

set(CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCMAKE_BUILD_TYPE=Debug")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCMAKE_BUILD_TYPE=Debug")

add_executable(cjmcu main.cpp CCS811.cpp CCS811.h HDC1080.cpp HDC1080.h BMP280.cpp BMP280.h stateful_number.h)
