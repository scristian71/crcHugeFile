cmake_minimum_required(VERSION 2.8)

project(crcHugeFile)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
TARGET_LINK_LIBRARIES( ${PROJECT_NAME} pthread)
