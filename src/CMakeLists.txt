cmake_minimum_required(VERSION 3.16)

project(pdf)

add_executable(${PROJECT_NAME} pdf.cpp ../util/stb_image.h ../util/cxxtimer.hpp)

target_link_libraries(${PROJECT_NAME} fmt::fmt lxd util)
