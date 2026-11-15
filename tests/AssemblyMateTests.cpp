#include "modeling/assembly/MateSolver.h"
#include <gp_Quaternion.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
using namespace thecad::modeling::assembly;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
DatumGeometry geometry(DatumKind kind, gp_Dir normal = gp_Dir(0, 0, 1))
{
    return {kind, gp_Ax3(gp_Pnt(), normal)};
}
} // namespace
int main()
{
    try
    {
        std::vector<RigidBody> bodies{{"fixed", {}, true}, {"floating", {}, false}};
        bodies[1].placement.SetRotation(gp_Quaternion(gp_Vec(0, 1, 0), 0.8));
        bodies[1].placement.SetTranslationPart({20, 15, 8});
        ResolvedMate concentric{"concentric",
                                MateKind::Concentric,
                                0,
                                1,
                                geometry(DatumKind::Axis),
                                geometry(DatumKind::Axis)};
        auto result = solveMates(bodies, {concentric});
        require(result.degreesOfFreedom == 2,
                "Concentric mate must leave axial translation and rotation");
        require(std::hypot(result.placements[1].TranslationPart().X(),
                           result.placements[1].TranslationPart().Y()) < 1e-5,
                "Axes did not align");
        require(result.placements[0].TranslationPart().Modulus() < 1e-12, "Fixed component moved");
        ResolvedMate coincident{"shoulder", MateKind::Coincident,       0,
                                1,          geometry(DatumKind::Plane), geometry(DatumKind::Plane)};
        result = solveMates(bodies, {concentric, coincident});
        require(result.degreesOfFreedom == 1,
                "Shaft shoulder must leave one rotational degree of freedom");
        require(result.placements[1].TranslationPart().Modulus() < 1e-5, "Shoulder did not seat");
        ResolvedMate angle{"angle",
                           MateKind::Angle,
                           0,
                           1,
                           geometry(DatumKind::Axis, gp_Dir(1, 0, 0)),
                           geometry(DatumKind::Axis, gp_Dir(1, 0, 0)),
                           47};
        result = solveMates(bodies, {concentric, coincident, angle});
        require(result.degreesOfFreedom == 0 && result.fullyConstrained[1],
                "Three mates did not fully constrain the shaft");
        const auto x = gp_Dir(1, 0, 0).Transformed(result.placements[1]);
        require(std::abs(x.Angle(gp_Dir(1, 0, 0)) * 180 / 3.141592653589793 - 47) < 1e-5,
                "Angle mate failed");
        bodies[1].placement = {};
        auto distance = coincident;
        distance.id = "distance";
        distance.kind = MateKind::Distance;
        distance.value = 15;
        result = solveMates(bodies, {distance});
        require(std::abs(result.placements[1].TranslationPart().Z() - 15) < 1e-5 &&
                    result.degreesOfFreedom == 3,
                "Distance mate failed");
        bool rejected = false;
        try
        {
            solveMates(bodies, {coincident, distance});
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Conflicting mates accepted");
        bodies[0].fixed = false;
        result = solveMates(bodies, {coincident});
        require(result.degreesOfFreedom == 9,
                "Ungrounded mate system incorrectly fixed a component");
        for (double degrees : {0., 90., 180.})
        {
            angle.value = degrees;
            result = solveMates(bodies, {angle});
            require(result.degreesOfFreedom > 0, "Angle mate overconstrained assembly");
        }
        require(!validMateTypes(MateKind::Concentric, DatumKind::Plane, DatumKind::Axis),
                "Unsupported reference pair accepted");
        std::cout << "PASS: assembly rigid-body mates, freedom, fixed bodies, conflict rejection "
                     "and singular-angle seeds\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
