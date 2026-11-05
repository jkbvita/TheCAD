#pragma once
#include "modeling/Sketch.h"
#include <string>
#include <vector>
namespace thecad::modeling {
// Backend-neutral diagnostics. Spatial sketches can use a different adapter later.
enum class SolveState { UnderConstrained, FullyConstrained, Redundant, Conflicting, NonConverged, InvalidInput, SolverFailure, UnsupportedSpace };
struct SolverDiagnostics {
    SolveState state = SolveState::InvalidInput;
    int degreesOfFreedom = -1;
    std::vector<std::string> conflicting, redundant;
    std::string message;
    double maximumResidual = 0;
};
struct PointTarget { SketchPointRef point; Point2 position; bool hard = false; };
struct SolveOptions {
    int maximumIterations = 100;
    double tolerance = 1e-8;
    std::vector<PointTarget> pointTargets;
    std::vector<std::pair<std::string,double>> radiusTargets;
};
struct SketchSolveResult {
    bool success = false;
    Sketch sketch;
    SolverDiagnostics diagnostics;
};
// Owns a candidate copy; failure returns the input unchanged. No GCS/Qt types leak.
SketchSolveResult solvePlanarSketch(const Sketch& sketch,const SolveOptions& options = {});
Sketch requireSolvedSketch(const SketchSolveResult& result);
struct SketchDefinition {
    bool valid=false;
    int degreesOfFreedom=-1;
    std::vector<SketchPointRef> fixedPoints;
    std::vector<std::string> definedCurves; // No remaining size, position or direction freedom.
};
// Read-only local mobility diagnosis. Does not create constraints or fix geometry.
SketchDefinition analyzeSketchDefinition(const Sketch& sketch);
const char* solveStateName(SolveState state);
Sketch setSketchDimension(const Sketch& sketch,const SketchDimension& dimension);
Sketch removeSketchDimension(const Sketch& sketch,const std::string& id);
Sketch setSketchRelation(const Sketch& sketch,const SketchRelation& relation);
Sketch removeSketchRelation(const Sketch& sketch,const std::string& id);
}
