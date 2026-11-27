#pragma once
#include "modeling/Sketch.h"
namespace thecad::modeling {
// Presentation only: these functions never change a mathematical constraint.
Point2 dimensionAnnotationPosition(const Sketch&, const SketchDimension&);
void placeDimensionAnnotation(const Sketch&, SketchDimension&, Point2 position);
void attachDimensionAnnotations(Sketch&);
struct DimensionGraphics {
    Point2 text;
    std::vector<std::pair<Point2, Point2>> segments;
};
DimensionGraphics dimensionGraphics(const Sketch&, const SketchDimension&, double unitsPerPixel);
}
