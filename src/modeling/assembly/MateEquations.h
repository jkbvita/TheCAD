#pragma once
#include "modeling/assembly/MateSolver.h"
namespace thecad::modeling::assembly
{
struct MateResidual
{
    std::vector<double> values, tolerances;
};
bool compatibleMate(MateKind, const MateGeometry&, const MateGeometry&);
MateResidual widthResidual(const ResolvedMate&, const std::vector<gp_Trsf>&, double scale,
                           bool diagnose);
void prepareMate(ResolvedMate&, const std::vector<gp_Trsf>&);
MateResidual mateResidual(const ResolvedMate&, const std::vector<gp_Trsf>&,
                          const std::vector<double>& coordinates, double scale,
                          bool diagnose = false);
void validateMateSolution(const ResolvedMate&, const std::vector<double>&);
} // namespace thecad::modeling::assembly
