cmake_minimum_required( VERSION 3.14.0 )

project(lxd CXX)

find_package(fmt CONFIG REQUIRED)

add_library( lxd STATIC
    defines.h
    debug.h
    fileio.h
    fileio.cpp
    str.h
    str.cpp
)

target_compile_options( lxd PRIVATE -Wall )
target_compile_definitions( lxd PUBLIC -DBUILDING_DLL )
target_compile_features( lxd PRIVATE cxx_std_17 )
target_link_libraries( lxd PUBLIC fmt::fmt fmt::fmt-header-only Winhttp Bcrypt)
