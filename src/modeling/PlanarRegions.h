#pragma once
#include "modeling/Sketch.h"
namespace thecad::modeling {
// Exact derived topology only. Source entities, relations and dimensions remain intact.
std::vector<Contour> intersectedContours(const Sketch& sketch);
bool isFragmentReference(const std::string& reference);
}
