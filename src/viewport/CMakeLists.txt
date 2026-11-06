add_library(thecad_graphics STATIC
    GraphicsScene.cpp
    CameraNavigation.cpp
    DimensionPresenter.cpp
    SketchPicker.cpp)
set_target_properties(thecad_graphics PROPERTIES AUTOMOC OFF)
target_include_directories(thecad_graphics PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_graphics PUBLIC Qt6::Gui thecad_modeling thecad_presentation TKOpenGl)
