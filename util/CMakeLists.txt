cmake_minimum_required(VERSION 3.16)

project(util CXX)

add_library(${PROJECT_NAME} STATIC stb_image.h cxxtimer.hpp)

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
target_compile_definitions(${PROJECT_NAME} PUBLIC -DBUILDING_DLL)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
