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

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
find_package(OpenCASCADE 7.9.3 EXACT REQUIRED COMPONENTS
    FoundationClasses ModelingData ModelingAlgorithms Visualization)

# Keep the imported OCCT SDK separate from application targets.
add_library(thecad_occt INTERFACE)
target_include_directories(thecad_occt SYSTEM INTERFACE "${OpenCASCADE_INCLUDE_DIR}")

add_library(thecad_modeling STATIC src/modeling/Box.cpp src/modeling/Sketch.cpp src/modeling/PlanarRegions.cpp src/modeling/ReferencePlanes.cpp src/modeling/ConstraintSolver.cpp src/modeling/DimensionInference.cpp src/modeling/DimensionAnnotation.cpp src/modeling/SketchTools.cpp src/modeling/RelationCommands.cpp)
set_target_properties(thecad_modeling PROPERTIES AUTOMOC OFF)
target_sources(thecad_modeling PRIVATE src/modeling/FeaturePreview.cpp)
target_sources(thecad_modeling PRIVATE src/modeling/ReferenceGeometry.cpp)
target_include_directories(thecad_modeling PUBLIC src)
target_link_libraries(thecad_modeling PRIVATE thecad_planegcs PUBLIC thecad_occt TKPrim TKTopAlgo TKBRep TKBO TKBool TKShHealing)

add_library(thecad_document STATIC src/document/PartDocument.cpp)
target_sources(thecad_document PRIVATE src/document/ReferenceGeometryData.cpp)
set_target_properties(thecad_document PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_document PUBLIC src)
target_link_libraries(thecad_document PUBLIC thecad_modeling Qt6::Core)

add_library(thecad_ui STATIC
    src/ui/PropertyManagerPage.cpp
    src/ui/ReferenceGeometryViewport.cpp
    src/ui/ReferenceGeometryUi.cpp
    src/ui/CadViewport.cpp src/ui/CadViewport.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h src/ui/SketchConstraintsUi.cpp src/ui/SmartDimensionUi.cpp src/ui/EditHistory.cpp src/ui/SketchToolsUi.cpp src/ui/RelationIcons.cpp src/ui/RelationsContextUi.cpp src/ui/RelationSymbols.cpp)
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
    add_executable(thecad_reference_geometry_tests tests/ReferenceGeometryTests.cpp)
    set_target_properties(thecad_reference_geometry_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_reference_geometry_tests PRIVATE thecad_document)
    add_test(NAME document.reference-geometry COMMAND thecad_reference_geometry_tests)
    add_executable(thecad_feature_preview_tests tests/FeaturePreviewTests.cpp)
    set_target_properties(thecad_feature_preview_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_feature_preview_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.feature-preview COMMAND thecad_feature_preview_tests)
    add_executable(thecad_sketch_definition_tests tests/SketchDefinitionTests.cpp)
    set_target_properties(thecad_sketch_definition_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_sketch_definition_tests PRIVATE thecad_modeling)
    add_test(NAME solver.entity-definition COMMAND thecad_sketch_definition_tests)
    add_executable(thecad_intersection_region_tests tests/IntersectionRegionTests.cpp)
    set_target_properties(thecad_intersection_region_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_intersection_region_tests PRIVATE thecad_document)
    add_test(NAME modeling.intersection-regions COMMAND thecad_intersection_region_tests)
    add_executable(thecad_midpoint_rectangle_tests tests/MidpointRectangleTests.cpp)
    set_target_properties(thecad_midpoint_rectangle_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_midpoint_rectangle_tests PRIVATE thecad_document)
    add_test(NAME sketch.midpoint-rectangle COMMAND thecad_midpoint_rectangle_tests)
    add_executable(thecad_relations_system_tests tests/RelationsSystemTests.cpp)
    set_target_properties(thecad_relations_system_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_relations_system_tests PRIVATE thecad_document)
    add_test(NAME solver.relations-system COMMAND thecad_relations_system_tests)
    add_executable(thecad_basic_sketch_tools_tests tests/BasicSketchToolsTests.cpp)
    set_target_properties(thecad_basic_sketch_tools_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_basic_sketch_tools_tests PRIVATE thecad_document)
    add_test(NAME modeling.basic-sketch-tools COMMAND thecad_basic_sketch_tools_tests)
    add_executable(thecad_edit_history_tests tests/EditHistoryTests.cpp)
    set_target_properties(thecad_edit_history_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_edit_history_tests PRIVATE thecad_modeling)
    add_test(NAME editing.history-delete COMMAND thecad_edit_history_tests)
    add_executable(thecad_dimension_graphics_tests tests/DimensionGraphicsTests.cpp)
    set_target_properties(thecad_dimension_graphics_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_dimension_graphics_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.dimension-graphics COMMAND thecad_dimension_graphics_tests)
    add_executable(thecad_dimension_document_tests tests/DimensionDocumentTests.cpp)
    set_target_properties(thecad_dimension_document_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_dimension_document_tests PRIVATE thecad_document)
    add_test(NAME document.dimensions COMMAND thecad_dimension_document_tests)

    add_executable(thecad_constraint_solver_tests tests/ConstraintSolverTests.cpp)
    set_target_properties(thecad_constraint_solver_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_constraint_solver_tests PRIVATE thecad_modeling)
    add_test(NAME solver.adapter COMMAND thecad_constraint_solver_tests)

    add_executable(thecad_planegcs_prototype_tests tests/PlaneGcsPrototypeTests.cpp)
    set_target_properties(thecad_planegcs_prototype_tests PROPERTIES AUTOMOC OFF CXX_STANDARD 20)
    target_link_libraries(thecad_planegcs_prototype_tests PRIVATE thecad_planegcs)
    add_test(NAME solver.planegcs-prototype COMMAND thecad_planegcs_prototype_tests)

    add_executable(thecad_modeling_tests tests/BoxTests.cpp)
    set_target_properties(thecad_modeling_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_modeling_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.box COMMAND thecad_modeling_tests)

    add_executable(thecad_sketch_tests tests/SketchGeometryTests.cpp)
    set_target_properties(thecad_sketch_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_sketch_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.sketch COMMAND thecad_sketch_tests)

    add_executable(thecad_reference_plane_tests tests/ReferencePlaneTests.cpp)
    set_target_properties(thecad_reference_plane_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_reference_plane_tests PRIVATE thecad_modeling)
    add_test(NAME modeling.reference-planes COMMAND thecad_reference_plane_tests)

    add_executable(thecad_document_tests tests/PartDocumentTests.cpp)
    set_target_properties(thecad_document_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_document_tests PRIVATE thecad_document)
    add_test(NAME document.part COMMAND thecad_document_tests)

    add_executable(thecad_multi_contour_tests tests/MultiContourTests.cpp)
    set_target_properties(thecad_multi_contour_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_multi_contour_tests PRIVATE thecad_document)
    add_test(NAME modeling.multiple-contours COMMAND thecad_multi_contour_tests)

    add_executable(thecad_reference_face_tests tests/ReferenceFaceContourTests.cpp)
    set_target_properties(thecad_reference_face_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(thecad_reference_face_tests PRIVATE thecad_document)
    add_test(NAME modeling.reference-faces-contours COMMAND thecad_reference_face_tests)

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
        add_test(NAME ui.modeling-workflow.hidpi COMMAND thecad_workflow_tests
            -o "${CMAKE_BINARY_DIR}/modeling-workflow-hidpi-results.txt,txt")
        set_tests_properties(ui.modeling-workflow.hidpi PROPERTIES TIMEOUT 120
            LABELS "gui;modeling;hidpi" RUN_SERIAL TRUE
            ENVIRONMENT "QT_SCALE_FACTOR=1.5;THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts/hidpi")
        add_executable(thecad_plane_workflow_tests tests/GraphicalPlaneWorkflowTests.cpp)
        target_link_libraries(thecad_plane_workflow_tests PRIVATE thecad_ui Qt6::Test)
        add_test(NAME ui.graphical-planes COMMAND thecad_plane_workflow_tests
            -o "${CMAKE_BINARY_DIR}/graphical-planes-results.txt,txt")
        set_tests_properties(ui.graphical-planes PROPERTIES TIMEOUT 120 LABELS "gui;planes" RUN_SERIAL TRUE
            ENVIRONMENT "THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts")
        add_test(NAME ui.graphical-planes.hidpi COMMAND thecad_plane_workflow_tests
            -o "${CMAKE_BINARY_DIR}/graphical-planes-hidpi-results.txt,txt")
        set_tests_properties(ui.graphical-planes.hidpi PROPERTIES TIMEOUT 120 LABELS "gui;planes;hidpi" RUN_SERIAL TRUE
            ENVIRONMENT "QT_SCALE_FACTOR=1.5;THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts/hidpi")
        add_executable(thecad_sketch_interaction_tests tests/SketchInteractionTests.cpp)
        target_link_libraries(thecad_sketch_interaction_tests PRIVATE thecad_ui Qt6::Test)
        add_test(NAME ui.sketch-interaction COMMAND thecad_sketch_interaction_tests
            -o "${CMAKE_BINARY_DIR}/sketch-interaction-results.txt,txt")
        add_test(NAME ui.sketch-interaction.hidpi COMMAND thecad_sketch_interaction_tests
            -o "${CMAKE_BINARY_DIR}/sketch-interaction-hidpi-results.txt,txt")
        set_tests_properties(ui.sketch-interaction ui.sketch-interaction.hidpi PROPERTIES
            TIMEOUT 120 LABELS "gui;sketch;snapping" RUN_SERIAL TRUE)
        set_tests_properties(ui.sketch-interaction PROPERTIES
            ENVIRONMENT "THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts")
        set_tests_properties(ui.sketch-interaction.hidpi PROPERTIES
            ENVIRONMENT "QT_SCALE_FACTOR=1.5;THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts/hidpi" LABELS "gui;sketch;snapping;hidpi")
        # Desktop tests must not race for focus or native windows.
        set_tests_properties(ui.viewport ui.viewport.hidpi ui.documents PROPERTIES RUN_SERIAL TRUE)
        set_tests_properties(ui.viewport PROPERTIES
            TIMEOUT 60 LABELS "gui;opengl"
            ENVIRONMENT "THECAD_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts")
    endif()
endif()
