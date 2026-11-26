add_library(thecad_widgets STATIC
    Theme.cpp Theme.h PreferencesDialog.cpp PropertyManagerPage.cpp RelationIcons.cpp
    input/PointerDevices.cpp input/NavigationInputSettings.cpp input/NavigationInputSettings.h
    widgets/CommandBar.cpp widgets/FeatureCommandBar.cpp widgets/FeatureTreePresenter.cpp widgets/PropertyManagerControls.cpp)
target_include_directories(thecad_widgets PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_widgets PUBLIC Qt6::Widgets thecad_document)
if(WIN32)
    target_link_libraries(thecad_widgets PRIVATE setupapi cfgmgr32)
endif()

add_library(thecad_viewport_widget STATIC
    ViewportWidget.cpp ViewportWidget.h
    CadViewport.cpp CadViewport.h NavigationViewport.cpp input/TouchpadNavigation.cpp
    ReferenceGeometryViewport.cpp SelectionViewport.cpp SketchReferenceViewport.cpp ArcSlotViewport.cpp SplineViewport.cpp MeasureViewport.cpp MeasureOverlay.cpp RelationSymbols.cpp
    assembly/AssemblyViewport.cpp assembly/AssemblyViewport.h)
target_include_directories(thecad_viewport_widget PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_viewport_widget PUBLIC Qt6::Widgets thecad_application thecad_graphics thecad_widgets)

add_library(thecad_ui STATIC
    MainWindow.cpp MainWindow.h EditHistory.cpp UiCommandAdapter.cpp files/NativeFileDialogs.cpp
    startup/WelcomeWindow.cpp startup/WelcomeWindow.h startup/LoadingScreen.cpp startup/WorkspaceLauncher.cpp
    features/ExtrusionOptionsPanel.cpp features/SplineParametersDialog.cpp features/SolidFeaturePanel.cpp features/SolidFeaturePanel.h features/SweepFeatureForm.cpp SolidFeaturesUi.cpp
    SelectionUi.cpp MeasureUi.cpp MeasurePanel.cpp MeasurePanel.h MeasureIcons.cpp ReferenceGeometryUi.cpp SketchConstraintsUi.cpp
    SmartDimensionUi.cpp SketchToolsUi.cpp SketchModifierUi.cpp RelationsContextUi.cpp
    assembly/AssemblyWindow.cpp assembly/AssemblyWindow.h assembly/AssemblyUiCommands.cpp assembly/AssemblyMateEditor.cpp assembly/AssemblyMateEditor.h
    assembly/MateIcons.cpp assembly/MateIcons.h
    assembly/AssemblyMateParameters.cpp assembly/AssemblyMateParameters.h
    assembly/AssemblyQuickMates.cpp assembly/MateIcons.cpp assembly/MateIcons.h
    assembly/AssemblyPropertyEditor.cpp assembly/AssemblyPropertyEditor.h
    assembly/AssemblyBeltEditor.cpp assembly/AssemblyBeltEditor.h
    assembly/AssemblyAssetEditor.cpp assembly/AssemblyAssetEditor.h
    assembly/AssemblyReferenceWorkflowEditors.cpp assembly/AssemblyReferenceWorkflowEditors.h
    assembly/AssemblyMagneticEditor.cpp assembly/AssemblyMagneticEditor.h
    assembly/AssemblyWorkflowUi.cpp
    assembly/AssemblyWorkflowTree.cpp
    assembly/ComponentPlacementEditor.cpp assembly/ComponentPlacementEditor.h)
target_include_directories(thecad_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_ui PUBLIC thecad_viewport_widget thecad_widgets thecad_application thecad_exchange)
qt_add_resources(thecad_ui thecad_branding
    PREFIX "/branding"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/thecad-logo.png"
        "${PROJECT_SOURCE_DIR}/resources/thecad-mark.png")
