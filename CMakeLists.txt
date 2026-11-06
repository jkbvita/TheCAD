cmake_minimum_required(VERSION 3.24)
project(TheCAD VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This initial prototype supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

add_subdirectory(third_party/planegcs)

option(THECAD_BUILD_GUI "Build the Qt Widgets desktop application" ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
set(thecad_occt_components FoundationClasses ModelingData ModelingAlgorithms)
if(THECAD_BUILD_GUI)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
    list(APPEND thecad_occt_components Visualization ApplicationFramework DataExchange)
endif()
find_package(OpenCASCADE 7.9.3 EXACT REQUIRED COMPONENTS ${thecad_occt_components})

# Keep the imported OCCT SDK separate from application targets.
add_library(thecad_occt INTERFACE)
target_include_directories(thecad_occt SYSTEM INTERFACE "${OpenCASCADE_INCLUDE_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

add_subdirectory(src/modeling)
add_subdirectory(src/document)
add_subdirectory(src/application)
if(THECAD_BUILD_GUI)
    add_subdirectory(src/exchange)
    add_subdirectory(src/presentation)
    add_subdirectory(src/viewport)
    add_subdirectory(src/ui)
    qt_add_executable(TheCAD WIN32 src/main.cpp)
    target_link_libraries(TheCAD PRIVATE thecad_ui)
endif()

foreach(target thecad_modeling thecad_document thecad_application thecad_presentation thecad_graphics thecad_exchange thecad_widgets thecad_viewport_widget thecad_ui TheCAD)
    if(NOT TARGET ${target})
        continue()
    endif()
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endif()
endforeach()

include(cmake/Architecture.cmake)

include(CTest)
option(THECAD_GUI_TESTS "Run viewport tests (requires a desktop and OpenGL)" OFF)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
