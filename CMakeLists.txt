cmake_minimum_required(VERSION 3.7)
project(cjmcu)

set(CMAKE_CXX_STANDARD 14)

add_executable(cjmcu main.cpp CCS811.cpp CCS811.h HDC1080.cpp HDC1080.h BMP280.cpp BMP280.h)
