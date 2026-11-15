#pragma once
#include "modeling/ReferenceGeometry.h"
#include "modeling/assembly/MateTypes.h"
#include "modeling/assembly/MateGeometry.h"
#include <gp_Trsf.hxx>
#include <string>
#include <vector>
#include <optional>

namespace thecad::modeling::assembly
{
struct ResolvedMateReference
{
    std::string role;
    int body = -1;
    MateGeometry geometry;
};
struct ResolvedMateDefinition
{
    std::vector<ResolvedMateReference> references;
    MateParameters parameters;
    MateMotionState motion;
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
    std::optional<ResolvedMateDefinition> definition;
};
struct MateSolution
{
    std::vector<gp_Trsf> placements;
    std::vector<bool> fullyConstrained;
    int degreesOfFreedom = 0;
    std::vector<MateMotionState> motion;
};
// Transient screen-plane grab target. It never becomes a persisted mate.
struct ComponentDragTarget
{
    int body = -1;
    gp_Pnt localPoint, target;
    gp_Dir viewNormal;
    double tolerance = 0.01;
};
MateSolution dragMates(const std::vector<RigidBody>& bodies, const std::vector<ResolvedMate>& mates,
                       const ComponentDragTarget& target);
bool validMateTypes(MateKind kind, DatumKind first, DatumKind second);
const char* mateName(MateKind kind);
MateSolution solveMates(const std::vector<RigidBody>& bodies,
                        const std::vector<ResolvedMate>& mates);
void validateRigidPlacement(const gp_Trsf& placement);
} // namespace thecad::modeling::assembly
