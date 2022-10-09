cmake_minimum_required(VERSION 3.20)
project(NXAEncode C)

set(CMAKE_C_STANDARD 99)

add_executable(NXAEncode main.c)

target_link_options(NXAEncode PRIVATE "-lopus")