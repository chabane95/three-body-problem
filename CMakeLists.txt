cmake_minimum_required(VERSION 3.16)
project(three_body_problem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(three-body src/main.cpp)
include(CTest)
if(BUILD_TESTING)
    add_executable(physics-test tests/physics_test.cpp)
    target_include_directories(physics-test PRIVATE src)
    add_test(NAME physics COMMAND physics-test)
endif()
