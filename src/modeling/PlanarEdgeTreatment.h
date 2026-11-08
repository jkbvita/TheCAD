#pragma once

#include "modeling/TopologyNames.h"

namespace thecad::modeling
{
// Exact section for convex straight edges between two planar faces. Existing
// faces trim the ends, except when replaying legacy feature history.
TopoDS_Shape cutPlanarEdgeTreatments(const TopoDS_Shape& body,
                                   const std::vector<TopoDS_Shape>& edges,
                                   double size, bool chamfer, TopologyNames& names,
                                   const std::string& featureId, bool legacyEnds);
}
