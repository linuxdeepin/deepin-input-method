cmake_minimum_required(VERSION 3.0)
project(dim VERSION 0.1.0)

option(BUILD_FRONTEND "build all frontends" ON)
option(DIM_DEBUG "toggle debug mode" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS on)

set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

# common flags used by all targets
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -pipe")
#set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -g -Og -fno-strict-aliasing -pipe")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -g -pipe")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DIM_DEBUG on)
endif()

include(GNUInstallDirs)

set(DIM_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/dim)
if (UNIX)
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        set(DIM_PRIVATE_LIBDIR ${DIM_INSTALL_DATADIR}/l64)
    else()
        set(DIM_PRIVATE_LIBDIR ${DIM_INSTALL_DATADIR}/l32)
    endif()
else()
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        set(DIM_PRIVATE_LIBDIR ${DIM_INSTALL_DATADIR}/w64)
    else()
        set(DIM_PRIVATE_LIBDIR ${DIM_INSTALL_DATADIR}/w32)
    endif()
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

if (DIM_DEBUG)
    add_definitions(-DG_LOG_USE_STRUCTURED="dime")
endif()

find_package(PkgConfig)


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${PROJECT_BINARY_DIR})
configure_file(${PROJECT_SOURCE_DIR}/src/config.h.in ${PROJECT_BINARY_DIR}/config.h @ONLY)


add_subdirectory(src)
