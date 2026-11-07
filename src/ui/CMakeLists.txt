add_library(thecad_widgets STATIC
    Theme.cpp Theme.h PropertyManagerPage.cpp RelationIcons.cpp
    widgets/CommandBar.cpp widgets/FeatureTreePresenter.cpp)
target_include_directories(thecad_widgets PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_widgets PUBLIC Qt6::Widgets thecad_document)

add_library(thecad_viewport_widget STATIC
    CadViewport.cpp CadViewport.h NavigationViewport.cpp
    ReferenceGeometryViewport.cpp SelectionViewport.cpp SketchReferenceViewport.cpp RelationSymbols.cpp)
target_include_directories(thecad_viewport_widget PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_viewport_widget PUBLIC Qt6::Widgets thecad_application thecad_graphics thecad_widgets)

add_library(thecad_ui STATIC
    MainWindow.cpp MainWindow.h EditHistory.cpp files/NativeFileDialogs.cpp
    features/SolidFeaturePanel.cpp features/SolidFeaturePanel.h features/SweepFeatureForm.cpp SolidFeaturesUi.cpp
    SelectionUi.cpp ReferenceGeometryUi.cpp SketchConstraintsUi.cpp
    SmartDimensionUi.cpp SketchToolsUi.cpp SketchModifierUi.cpp RelationsContextUi.cpp)
target_include_directories(thecad_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_ui PUBLIC thecad_viewport_widget thecad_widgets thecad_application thecad_exchange)
