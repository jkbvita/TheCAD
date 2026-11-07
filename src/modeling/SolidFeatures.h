#pragma once
#include "modeling/Sketch.h"
#include <gp_Ax1.hxx>
namespace thecad::modeling
{
// Pure geometric operations on private copies. Names are assigned by generating
// roles and OCCT history; removed/ambiguous references are never guessed.
TopoDS_Shape makeRevolutions(const TopoDS_Shape& base, const Sketch& sketch,
                             const RegionReferences& regions, const gp_Ax1& axis,
                             double angleDegrees, Direction direction, bool cut,
                             TopologyNames& names, const std::string& featureId);
TopoDS_Shape makeSweep(const TopoDS_Shape& base, const Sketch& profile, const Sketch& path,
                       const std::vector<std::string>& region, bool cut,
                       TopologyNames& names, const std::string& featureKey);
TopoDS_Shape makeEdgeTreatment(const TopoDS_Shape& base, const std::vector<TopoDS_Shape>& edges,
                               double size, bool chamfer, TopologyNames& names,
                               const std::string& featureId);
} // namespace thecad::modeling
