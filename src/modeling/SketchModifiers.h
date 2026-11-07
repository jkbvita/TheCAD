#pragma once

#include "modeling/Sketch.h"

namespace thecad::modeling
{
enum class SketchModifier { Convert, Mirror, LinearPattern, CircularPattern };

struct SketchPattern
{
    SketchModifier kind = SketchModifier::LinearPattern;
    std::string mirrorLine;
    Point2 center;
    double angle = 0; // Linear heading or circular span, in degrees.
    double spacing = 10;
    int count = 2;
    bool reverse = false;
};

struct SketchProjection
{
    Sketch::Projection link;
    SketchEntity geometry;
};

const Sketch::Projection* sketchProjection(const Sketch& sketch, const std::string& entityId);
std::string useSketchProjection(Sketch& sketch, const SketchProjection& option, bool construction);
void validateSketchModifiers(const Sketch& sketch);
Sketch replicateSketchEntities(const Sketch& sketch, const std::vector<std::string>& seeds,
                               const SketchPattern& parameters);
}
