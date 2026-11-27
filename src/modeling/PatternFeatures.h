#pragma once
#include "modeling/ReferenceGeometry.h"
#include <gp_Trsf.hxx>

namespace thecad::modeling
{
inline constexpr int maximumPatternInstances = 100;

// Each seed describes the result and its preceding solid in feature history.
// A null previous solid represents a seed that adds the entire result.
struct PatternSeed
{
    std::string id;
    TopoDS_Shape previous, result;
    const TopologyNames* previousNames = nullptr;
    const TopologyNames* resultNames = nullptr;
};

std::vector<gp_Trsf> linearInstances(const gp_Dir& axis, double spacing, int count, bool reverse);
std::vector<gp_Trsf> circularInstances(const gp_Ax1& axis, double angle, int count,
                                       bool equalSpacing, bool reverse);
gp_Trsf mirrorInstance(const gp_Ax3& plane);
TopoDS_Shape applyPattern(const TopoDS_Shape& base, const std::vector<PatternSeed>& seeds,
                          const std::vector<gp_Trsf>& transforms, TopologyNames& names,
                          const std::string& featureId);
}
