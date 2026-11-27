#include "viewport/BRepScenePicker.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <iostream>

namespace
{
bool require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}
}

int main()
{
    using namespace thecad::viewport;

    const auto lower = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const auto upper = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    gp_Trsf upperPlacement;
    upperPlacement.SetTranslation(gp_Vec(0.0, 0.0, 10.0));

    BRepScenePicker picker;
    picker.setScene({{"lower", lower, gp_Trsf(), true},
                     {"upper", upper, upperPlacement, true}});

    const gp_Lin ray(gp_Pnt(5.0, 5.0, 30.0), gp_Dir(0.0, 0.0, -1.0));
    const auto front = picker.surfaceAlongRay(ray, gp_Pnt(5.0, 5.0, 30.0),
                                              gp_Dir(0.0, 0.0, -1.0));
    if (!require(front && front->id == "upper", "nearest transformed component face was not picked") ||
        !require(front && front->faceIndex > 0, "picked face did not have a stable local index") ||
        !require(front && std::abs(front->point.Z() - 20.0) < 1e-7,
                 "transformed hit point was not returned in world coordinates"))
        return 1;

    picker.setScene({{"lower", lower, gp_Trsf(), true},
                     {"upper", upper, upperPlacement, false}});
    const auto lowerHit = picker.surfaceAlongRay(ray, gp_Pnt(5.0, 5.0, 30.0),
                                                 gp_Dir(0.0, 0.0, -1.0));
    if (!require(lowerHit && lowerHit->id == "lower", "hidden component occluded a visible face") ||
        !require(lowerHit && std::abs(lowerHit->point.Z() - 10.0) < 1e-7,
                 "nearest visible face point was incorrect"))
        return 1;

    picker.clear();
    if (!require(!picker.surfaceAlongRay(ray, gp_Pnt(5.0, 5.0, 30.0),
                                         gp_Dir(0.0, 0.0, -1.0)),
                 "cleared scene still returned a face"))
        return 1;

    return 0;
}
