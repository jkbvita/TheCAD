#pragma once

#include "modeling/TopologyNames.h"

namespace thecad::modeling
{
// Exact fallback for convex straight edges between two planar faces.
TopoDS_Shape cutPlanarEdgeTreatments(const TopoDS_Shape& body,
                                   const std::vector<TopoDS_Shape>& edges,
                                   double size, bool chamfer, TopologyNames& names,
                                   const std::string& featureId);
}
