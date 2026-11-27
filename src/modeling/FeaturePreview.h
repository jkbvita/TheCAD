#pragma once
#include <TopoDS_Shape.hxx>
#include <vector>
namespace thecad::modeling {
struct MaterialChanges {TopoDS_Shape added,removed;};
// Derived display geometry, never persisted and never used to commit a feature.
MaterialChanges materialChanges(const TopoDS_Shape& before,const TopoDS_Shape& after);
// Classify the generated faces directly for interactive edge treatments.
MaterialChanges edgeTreatmentHighlights(const TopoDS_Shape& stock, const std::vector<TopoDS_Shape>& faces);
}
