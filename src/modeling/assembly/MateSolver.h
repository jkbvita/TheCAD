#pragma once
#include "modeling/ReferenceGeometry.h"
#include "modeling/assembly/MateTypes.h"
#include "modeling/assembly/MateGeometry.h"
#include <gp_Trsf.hxx>
#include <gp_Quaternion.hxx>
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
// Transient grab target. Rotation uses localPoint as its preferred pivot;
// tolerance is radians for rotation, model units for point dragging.
struct ComponentDragTarget
{
    int body = -1;
    gp_Pnt localPoint, target;
    gp_Dir viewNormal;
    double tolerance = 0.01;
    std::optional<gp_Quaternion> rotation;
    // Point dragging may prefer translation within this screen-derived distance.
    // Zero preserves exact point targeting for non-interactive callers.
    double rotationDeadband = 0;
};
MateSolution dragMates(const std::vector<RigidBody>& bodies, const std::vector<ResolvedMate>& mates,
                       const ComponentDragTarget& target);
bool validMateTypes(MateKind kind, DatumKind first, DatumKind second);
const char* mateName(MateKind kind);
MateSolution solveMates(const std::vector<RigidBody>& bodies,
                        const std::vector<ResolvedMate>& mates);
void validateRigidPlacement(const gp_Trsf& placement);
} // namespace thecad::modeling::assembly
