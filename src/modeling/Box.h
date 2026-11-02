#pragma once

#include <TopoDS_Shape.hxx>

namespace thecad::modeling {

// Dimensions are in prototype model units (shown as millimeters in the demo).
// Rejects non-finite lengths and lengths at or below OCCT's linear tolerance.
TopoDS_Shape makeBox(double width, double depth, double height);

} // namespace thecad::modeling
