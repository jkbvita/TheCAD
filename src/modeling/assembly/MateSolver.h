#pragma once
#include "modeling/ReferenceGeometry.h"
#include <gp_Trsf.hxx>
#include <string>
#include <vector>

namespace thecad::modeling::assembly
{
enum class MateKind
{
    Coincident,
    Concentric,
    Distance,
    Angle
};
struct RigidBody
{
    std::string id;
    gp_Trsf placement;
    bool fixed = false;
    double size = 100;
};
struct ResolvedMate
{
    std::string id;
    MateKind kind = MateKind::Coincident;
    int first = -1, second = -1; // -1 is the assembly frame.
    DatumGeometry a, b;
    double value = 0;
    int alignment = 1;
};
struct MateSolution
{
    std::vector<gp_Trsf> placements;
    std::vector<bool> fullyConstrained;
    int degreesOfFreedom = 0;
};
bool validMateTypes(MateKind kind, DatumKind first, DatumKind second);
const char* mateName(MateKind kind);
MateSolution solveMates(const std::vector<RigidBody>& bodies,
                        const std::vector<ResolvedMate>& mates);
void validateRigidPlacement(const gp_Trsf& placement);
} // namespace thecad::modeling::assembly
