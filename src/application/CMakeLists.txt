add_library(thecad_application STATIC
    DocumentSession.cpp
    FeatureOperation.cpp)
set_target_properties(thecad_application PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_application PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_application PUBLIC thecad_document)
