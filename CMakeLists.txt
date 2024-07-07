# Copyright 2024 Gary Huang

cmake_minimum_required(VERSION 3.13)

project(launcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

set(Boost_ROOT "c:/boost_1_83_0")
set(Boost_ARCHITECTURE "-x32")

find_package(Threads REQUIRED)
find_package(Qt5 COMPONENTS Widgets REQUIRED)
find_package(Boost COMPONENTS filesystem REQUIRED)

include_directories(include)
include_directories(${Boost_INCLUDE_DIRS})

MESSAGE(STATUS "Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
MESSAGE(STATUS "Boost_LIBRARIES = ${Boost_LIBRARIES}")
MESSAGE(STATUS "Boost_LIB_VERSION = ${Boost_LIB_VERSION}")

add_executable(launcher
    main.cpp
    mainwindow.cpp)

target_link_libraries(launcher
    Qt5::Widgets
    ${Boost_LIBRARIES}
    ws2_32
)