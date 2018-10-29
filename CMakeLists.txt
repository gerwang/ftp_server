cmake_minimum_required(VERSION 3.10)
project(ftp_server C)

set(CMAKE_C_STANDARD 11)

add_executable(server main.c async_util.c async_util.h net_listener.c net_listener.h service_handler.c service_handler.h constants.h)