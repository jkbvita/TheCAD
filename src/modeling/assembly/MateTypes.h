#pragma once
#include <gp_Trsf.hxx>
#include <string>
#include <vector>
namespace thecad::modeling::assembly
{
// Values 0..3 are the schema-1 kinds. Persist new kinds by stable string key.
enum class MateKind
{
    Coincident,
    Concentric,
    Distance,
    Angle,
    Parallel,
    Perpendicular,
    Tangent,
    Lock,
    ProfileCenter,
    Symmetric,
    Width,
    Path,
    LinearCoupler,
    LimitDistance,
    LimitAngle,
    Cam,
    Slot,
    Hinge,
    Gear,
    RackPinion,
    Screw,
    UniversalJoint,
    Magnetic,
    Belt,
    LockToSketch,
    InPlace
};
enum class MateFamily
{
    Standard,
    Advanced,
    Mechanical,
    Generated
};
enum class MateStatus
{
    Active,
    Suppressed,
    Unsolved
};
enum class PositionMode
{
    Free,
    Centered,
    Distance,
    Percent
};
enum class ArcCondition
{
    Center,
    Minimum,
    Maximum
};
struct StandardMateOptions
{
    bool lockRotation = false, alignAxes = false;
    ArcCondition firstArc = ArcCondition::Center, secondArc = ArcCondition::Center;
    bool internalTangency = false;
};
struct LimitMateOptions
{
    double minimum = 0, maximum = 100;
};
struct PositionMateOptions
{
    PositionMode mode = PositionMode::Centered;
    double distance = 0, percent = 50;
    bool flip = false;
};
struct ProfileMateOptions
{
    double offset = 0, rotation = 0;
    bool clockwise = false;
};
struct PathMateOptions
{
    int followAxis = -1, upAxis = -1;
    bool flipTangent = false, flipUp = false;
};
struct CouplerMateOptions
{
    double firstRatio = 1, secondRatio = 1, pitch = 1;
    bool reverse = false;
    bool useGeometryRatio = false;
};
struct HingeMateOptions
{
    bool limits = false;
};
struct BeltMateOptions
{
    bool engaged = true, driving = false;
    double length = 100, thickness = 0;
    std::vector<double> diameters;
    std::vector<int> sides;
};
struct MagneticMateOptions
{
    bool locked = false;
};
struct MateParameters
{
    StandardMateOptions standard;
    LimitMateOptions limit;
    PositionMateOptions position;
    ProfileMateOptions profile;
    PathMateOptions path;
    CouplerMateOptions coupler;
    HingeMateOptions hinge;
    BeltMateOptions belt;
    MagneticMateOptions magnetic;
};
// Anchors and continuous coordinates are document state, not tessellation or IDs.
struct MateMotionState
{
    bool initialized = false;
    std::vector<gp_Trsf> anchors;
    std::vector<double> coordinates;
};
struct MateRole
{
    std::string key, label;
    int minimum = 1, maximum = 1;
};
const char* mateName(MateKind);
const char* mateKey(MateKind);
MateKind mateFromKey(const std::string&);
MateFamily mateFamily(MateKind);
std::vector<MateRole> mateRoles(MateKind, bool hingeLimits = false);
void validateMateParameters(MateKind, const MateParameters&);
} // namespace thecad::modeling::assembly
