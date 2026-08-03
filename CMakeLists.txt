cmake_minimum_required(VERSION 3.20)
project(Shrewd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-falign-functions=64)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-falign-labels=16 -falign-jumps=16)
    endif()
endif()

option(SHREWD_NATIVE "Tune for the build machine (-march=native)" OFF)
if(SHREWD_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-march=native)
endif()

add_subdirectory(src/shrewd)
add_subdirectory(src/savvy)
add_subdirectory(src/net)

add_executable(shrewd_demo src/tools/shrewd_demo.cpp)
target_link_libraries(shrewd_demo PRIVATE shrewd savvy)

add_executable(shrewdc src/tools/shrewdc.cpp)
target_link_libraries(shrewdc PRIVATE shrewd savvy)
target_compile_definitions(shrewd_demo PRIVATE EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")

add_executable(shrewd_bench src/tools/shrewd_bench.cpp)
target_link_libraries(shrewd_bench PRIVATE shrewd savvy)
target_compile_definitions(shrewd_bench PRIVATE EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")

add_executable(shrewd_fuzz src/tools/shrewd_fuzz.cpp)
target_link_libraries(shrewd_fuzz PRIVATE shrewd savvy)

add_executable(shrewdness src/tools/shrewdness.cpp)
target_link_libraries(shrewdness PRIVATE net shrewd savvy)
