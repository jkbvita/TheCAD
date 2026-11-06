add_library(thecad_presentation STATIC
    SelectionAppearance.cpp)
set_target_properties(thecad_presentation PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_presentation PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_presentation PUBLIC thecad_occt TKV3d)
