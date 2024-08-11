cmake_minimum_required(VERSION 3.27)
project(weighted_blended)

set(CMAKE_CXX_STANDARD 17)

find_package(GLFW3 REQUIRED)

add_executable(weighted_blended main.cpp glad/glad.c)
target_link_libraries(weighted_blended PUBLIC glfw)
