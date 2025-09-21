cmake_minimum_required(VERSION 3.19.0)

project("WS2812Controller" C CXX ASM)

include(cmake/st-project.cmake)

add_executable(${PROJECT_NAME})
add_st_target_properties(${PROJECT_NAME})