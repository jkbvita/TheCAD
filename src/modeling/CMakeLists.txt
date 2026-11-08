add_library(thecad_modeling STATIC
    Box.cpp
    Sketch.cpp
    PlanarRegions.cpp
    ReferencePlanes.cpp
    ConstraintSolver.cpp
    DimensionInference.cpp
    DimensionAnnotation.cpp
    SketchTools.cpp
    SketchArcs.cpp
    SketchModifiers.cpp
    SketchContourEdits.cpp
    SketchOffset.cpp
    RelationCommands.cpp)
set_target_properties(thecad_modeling PROPERTIES AUTOMOC OFF)
target_sources(thecad_modeling PRIVATE FeaturePreview.cpp Extrusion.cpp SolidFeatures.cpp PlanarEdgeTreatment.cpp PatternFeatures.cpp SweepFeatures.cpp)
target_sources(thecad_modeling PRIVATE ReferenceGeometry.cpp TopologyNames.cpp)
target_sources(thecad_modeling PRIVATE Selection.cpp Measurement.cpp)
target_include_directories(thecad_modeling PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(thecad_modeling PRIVATE thecad_planegcs PUBLIC thecad_occt TKPrim TKTopAlgo TKBRep TKBO TKBool TKShHealing TKFillet TKOffset)
