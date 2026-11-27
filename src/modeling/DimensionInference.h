#pragma once
#include "modeling/Sketch.h"
namespace thecad::modeling {
// Infer supported Smart Dimension geometry from model references and placement.
// Selection and annotation coordinates remain independent of any solver/backend.
SketchDimension inferDimension(const Sketch& sketch,const std::vector<SketchPointRef>& points,
    const std::vector<std::string>& entities,Point2 annotation,bool radius=false);
}
