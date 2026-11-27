add_library(thecad_exchange STATIC ShapeExporter.cpp)
set_target_properties(thecad_exchange PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_exchange PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_exchange PUBLIC Qt6::Core thecad_occt TKDESTEP TKDESTL TKMesh TKTopAlgo)
