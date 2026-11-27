#include "modeling/assembly/MateTypes.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::modeling::assembly
{
namespace
{
const char* names[] = {"Coincident",
                       "Concentric",
                       "Distance",
                       "Angle",
                       "Parallel",
                       "Perpendicular",
                       "Tangent",
                       "Lock",
                       "Profile Center",
                       "Symmetric",
                       "Width",
                       "Path Mate",
                       "Linear/Linear Coupler",
                       "Limit Distance",
                       "Limit Angle",
                       "Cam",
                       "Slot",
                       "Hinge",
                       "Gear",
                       "Rack and Pinion",
                       "Screw",
                       "Universal Joint",
                       "Magnetic",
                       "Belt",
                       "Lock To Sketch",
                       "In Place"};
const char* keys[] = {"coincident",     "concentric",      "distance",    "angle",
                      "parallel",       "perpendicular",   "tangent",     "lock",
                      "profile-center", "symmetric",       "width",       "path",
                      "linear-coupler", "limit-distance",  "limit-angle", "cam",
                      "slot",           "hinge",           "gear",        "rack-pinion",
                      "screw",          "universal-joint", "magnetic",    "belt",
                      "lock-to-sketch", "in-place"};
int index(MateKind k)
{
    const int i = int(k);
    if (i < 0 || i >= 26)
        throw std::runtime_error("Unsupported mate kind.");
    return i;
}
} // namespace
const char* mateName(MateKind k)
{
    return names[index(k)];
}
const char* mateKey(MateKind k)
{
    return keys[index(k)];
}
MateKind mateFromKey(const std::string& key)
{
    for (int i = 0; i < 26; ++i)
        if (key == keys[i])
            return MateKind(i);
    throw std::runtime_error("Unknown mate kind: " + key);
}
MateFamily mateFamily(MateKind k)
{
    const int i = index(k);
    return i < 8    ? MateFamily::Standard
           : i < 15 ? MateFamily::Advanced
           : i < 22 ? MateFamily::Mechanical
                    : MateFamily::Generated;
}
std::vector<MateRole> mateRoles(MateKind kind, bool limits)
{
    switch (kind)
    {
    case MateKind::Symmetric:
        return {{"plane", "Symmetry plane"}, {"entities", "Entities to mate", 2, 2}};
    case MateKind::Width:
        return {{"width", "Width selections", 1, 2}, {"tab", "Tab selections", 1, 2}};
    case MateKind::Path:
        return {{"point", "Component vertex"},
                {"path", "Path selection", 1, 128},
                {"up", "Up vector", 0, 1}};
    case MateKind::Cam:
        return {{"cam", "Cam faces", 1, 128}, {"follower", "Cam follower"}};
    case MateKind::Hinge:
    {
        std::vector<MateRole> r{{"concentric", "Concentric selections", 2, 2},
                                {"coincident", "Coincident selections", 2, 2}};
        if (limits)
            r.push_back({"angle", "Angle selections", 2, 2});
        return r;
    }
    case MateKind::LinearCoupler:
        return {{"first", "First motion direction"},
                {"second", "Second motion direction"},
                {"first-reference", "First reference component", 0, 1},
                {"second-reference", "Second reference component", 0, 1}};
    case MateKind::RackPinion:
        return {{"rack", "Rack"}, {"pinion", "Pinion / Gear"}};
    case MateKind::Slot:
        return {{"slot", "Slot selection"}, {"follower", "Axis, cylinder or slot"}};
    case MateKind::Belt:
        return {{"pulleys", "Ordered pulley components", 2, 64}};
    default:
        return {{"first", "First reference"}, {"second", "Second reference"}};
    }
}
void validateMateParameters(MateKind k, const MateParameters& p)
{
    index(k);
    for (double v : {p.limit.minimum, p.limit.maximum, p.position.distance, p.position.percent,
                     p.profile.offset, p.profile.rotation, p.coupler.firstRatio,
                     p.coupler.secondRatio, p.coupler.pitch})
        if (!std::isfinite(v) || std::abs(v) > 1e9)
            throw std::runtime_error("Mate parameter exceeds the finite 1e9 limit.");
    if (p.limit.minimum > p.limit.maximum)
        throw std::runtime_error("Minimum mate limit exceeds maximum.");
    if (p.position.percent < 0 || p.position.percent > 100 || int(p.position.mode) < 0 ||
        int(p.position.mode) > 3)
        throw std::runtime_error("Invalid mate placement mode or percentage.");
    if (int(p.standard.firstArc) < 0 || int(p.standard.firstArc) > 2 ||
        int(p.standard.secondArc) < 0 || int(p.standard.secondArc) > 2)
        throw std::runtime_error("Invalid cylindrical distance condition.");
    if (p.coupler.firstRatio <= 0 || p.coupler.secondRatio <= 0 || p.coupler.pitch <= 0)
        throw std::runtime_error("Mate ratios and pitch must be positive.");
    if ((k == MateKind::LimitAngle || (k == MateKind::Hinge && p.hinge.limits)) &&
        (p.limit.minimum < 0 || p.limit.maximum > 180))
        throw std::runtime_error("Angle limits must lie between 0 and 180 degrees.");
    if (!std::isfinite(p.belt.length) || p.belt.length <= 0 || p.belt.length > 1e9 ||
        !std::isfinite(p.belt.thickness) || p.belt.thickness < 0 || p.belt.thickness > 1e9 ||
        p.belt.diameters.size() > 64 || p.belt.sides.size() > 64)
        throw std::runtime_error("Invalid belt length, thickness or pulley count.");
    for (double diameter : p.belt.diameters)
        if (!std::isfinite(diameter) || diameter < 0 || diameter > 1e9)
            throw std::runtime_error(
                "Belt diameters must be finite and nonnegative; zero uses the measured diameter.");
    for (int side : p.belt.sides)
        if (side != -1 && side != 1)
            throw std::runtime_error("Belt routing sides must be -1 or +1.");
    if (p.path.followAxis < -1 || p.path.followAxis > 2 || p.path.upAxis < -1 ||
        p.path.upAxis > 2 || (p.path.upAxis >= 0 && p.path.upAxis == p.path.followAxis))
        throw std::runtime_error("Path orientation must use different component axes.");
}
} // namespace thecad::modeling::assembly
