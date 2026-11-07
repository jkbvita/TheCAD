add_library(thecad_document STATIC
    PartDocument.cpp
    detail/PartValidation.cpp
    storage/PartFileStore.cpp)
target_sources(thecad_document PRIVATE ReferenceGeometryData.cpp SketchProjectionData.cpp)
set_target_properties(thecad_document PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_document PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_document PUBLIC thecad_modeling Qt6::Core)
