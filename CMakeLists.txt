cmake_minimum_required(VERSION 3.24)

project(ef_map_overlay VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The EF-Map overlay currently targets Windows (DirectX 12) only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EF_OVERLAY_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

enable_testing()

if(EF_OVERLAY_ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /permissive- /EHsc)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_subdirectory(src/shared)
add_subdirectory(src/helper)
add_subdirectory(src/overlay)
add_subdirectory(src/injector)
add_subdirectory(tests)
