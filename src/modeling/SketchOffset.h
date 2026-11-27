#pragma once

#include "modeling/Sketch.h"

namespace thecad::modeling
{
struct SketchOffsetParameters
{
    double distance = 5;
    bool reverse = false;
    bool bothSides = false;
    bool selectChain = false;
    bool construction = false;
};

Sketch offsetSketchEntities(const Sketch& sketch, const std::vector<std::string>& ids,
                            const SketchOffsetParameters& parameters);
void validateSketchOffsets(const Sketch& sketch);
void validateSketchOffsetGeometry(const Sketch& sketch);
}
