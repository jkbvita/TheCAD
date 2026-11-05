#pragma once
#include <TopoDS_Shape.hxx>
namespace thecad::modeling {
struct MaterialChanges {TopoDS_Shape added,removed;};
// Derived display geometry, never persisted and never used to commit a feature.
MaterialChanges materialChanges(const TopoDS_Shape& before,const TopoDS_Shape& after);
}
