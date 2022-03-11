cmake_minimum_required(VERSION 3.14)

project(pdf CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(fmt)

add_subdirectory(lxd)
add_subdirectory(src)
add_subdirectory(util)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})