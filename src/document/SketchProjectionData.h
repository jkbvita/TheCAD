#pragma once

#include "modeling/Sketch.h"

namespace thecad::document::detail
{
void refreshSketchProjections(modeling::Sketch& sketch,
                             const std::map<std::string, modeling::TopologyNames>& namedBodies);
}
