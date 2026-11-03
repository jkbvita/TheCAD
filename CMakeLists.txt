cmake_minimum_required(VERSION 3.24)
project(TheCAD VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This initial prototype supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
find_package(OpenCASCADE 7.9.3 EXACT REQUIRED COMPONENTS
    FoundationClasses ModelingData ModelingAlgorithms Visualization)

# Keep the imported OCCT SDK separate from application targets.
add_library(thecad_occt INTERFACE)
target_include_directories(thecad_occt SYSTEM INTERFACE "${OpenCASCADE_INCLUDE_DIR}")

add_library(thecad_modeling STATIC src/modeling/Box.cpp src/modeling/Sketch.cpp)
set_target_properties(thecad_modeling PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_modeling PUBLIC src)
target_link_libraries(thecad_modeling PUBLIC thecad_occt TKPrim TKTopAlgo TKBRep TKBO TKBool TKShHealing)

add_library(thecad_document STATIC src/document/PartDocument.cpp)
set_target_properties(thecad_document PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_document PUBLIC src)
target_link_libraries(thecad_document PUBLIC thecad_modeling Qt6::Core)

add_library(thecad_ui STATIC
    src/ui/CadViewport.cpp src/ui/CadViewport.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h)
target_include_directories(thecad_ui PUBLIC src)
target_link_libraries(thecad_ui PUBLIC Qt6::Widgets thecad_document thecad_occt TKV3d TKOpenGl)

qt_add_executable(TheCAD WIN32 src/main.cpp)
target_link_libraries(TheCAD PRIVATE thecad_ui thecad_modeling)

foreach(target thecad_modeling thecad_document thecad_ui TheCAD)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endif()
endforeach()

include(CTest)
option(THECAD_GUI_TESTS "Run viewport tests (requires a desktop and OpenGL)" OFF)
if(BUILD_TESTING)
    add_executable(thecad_modeling_tests tests/BoxTests.cpp)
    set_target_properties(thecad_modeling_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_modeling_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.box COMMAND thecad_modeling_tests)

    add_executable(thecad_sketch_tests tests/SketchGeometryTests.cpp)
    set_target_properties(thecad_sketch_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_sketch_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.sketch COMMAND thecad_sketch_tests)

    add_executable(thecad_document_tests tests/PartDocumentTests.cpp)
    set_target_properties(thecad_document_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_document_tests PRIVATE thecad_document)
    add_test(NAME document.part COMMAND thecad_document_tests)

    if(THECAD_GUI_TESTS)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_executable(thecad_viewport_tests tests/CadViewportTests.cpp)
        target_compile_definitions(thecad_viewport_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(thecad_viewport_tests PRIVATE
            thecad_ui thecad_modeling Qt6::Test)
        add_test(NAME ui.viewport COMMAND thecad_viewport_tests -o "${CMAKE_BINARY_DIR}/viewport-results.txt,txt")
        add_executable(thecad_mainwindow_tests tests/MainWindowTests.cpp)
        target_link_libraries(thecad_mainwindow_tests PRIVATE thecad_ui Qt6::Test)
        add_test(NAME ui.documents COMMAND thecad_mainwindow_tests -o "${CMAKE_BINARY_DIR}/document-ui-results.txt,txt")
        add_test(NAME ui.viewport.hidpi COMMAND thecad_viewport_tests -o "${CMAKE_BINARY_DIR}/viewport-hidpi-results.txt,txt")
        set_tests_properties(ui.viewport.hidpi PROPERTIES TIMEOUT 60 LABELS "gui;opengl;hidpi"
            ENVIRONMENT "QT_SCALE_FACTOR=1.5;THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts/hidpi")
        set_tests_properties(ui.documents PROPERTIES TIMEOUT 60 LABELS "gui;documents")
        add_executable(thecad_workflow_tests tests/ModelingWorkflowTests.cpp)
        target_link_libraries(thecad_workflow_tests PRIVATE thecad_ui Qt6::Test)
        add_test(NAME ui.modeling-workflow COMMAND thecad_workflow_tests
            -o "${CMAKE_BINARY_DIR}/modeling-workflow-results.txt,txt")
        set_tests_properties(ui.modeling-workflow PROPERTIES TIMEOUT 120 LABELS "gui;modeling" RUN_SERIAL TRUE
            ENVIRONMENT "THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts")
        # Desktop tests must not race for focus or native windows.
        set_tests_properties(ui.viewport ui.viewport.hidpi ui.documents PROPERTIES RUN_SERIAL TRUE)
        set_tests_properties(ui.viewport PROPERTIES
            TIMEOUT 60 LABELS "gui;opengl"
            ENVIRONMENT "THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts")
    endif()
endif()
