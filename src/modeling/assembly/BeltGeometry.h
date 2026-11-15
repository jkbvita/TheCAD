#pragma once
#include "modeling/Sketch.h"
namespace thecad::modeling::assembly
{
struct BeltPulley
{
    gp_Ax3 frame;
    double radius = 1;
    int side = 1;
};
struct BeltGeometry
{
    gp_Ax3 frame;
    Sketch centerline, boundary;
    double length = 0;
};
BeltGeometry beltGeometry(const std::vector<BeltPulley>& pulleys, double thickness = 1,
                          double planeOffset = 0);
double beltLength(const std::vector<BeltPulley>& pulleys);
} // namespace thecad::modeling::assembly
