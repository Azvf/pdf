cmake_minimum_required(VERSION 3.14)

project(pdf CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${PROJECT_SOURCE_DIR}/bin/$<CONFIG>>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${PROJECT_SOURCE_DIR}/bin/$<CONFIG>>)

find_package(fmt)

add_subdirectory(./lxd lxd)
add_subdirectory(./src src)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})