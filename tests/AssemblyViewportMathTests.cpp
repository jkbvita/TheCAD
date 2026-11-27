#include "viewport/RayPlaneIntersection.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main()
{
    try
    {
        const gp_Pnt focus(1692.5, 1766.5, 1751.5);
        const gp_Pnt eye(0, 0, 0);
        const gp_Dir ray(gp_Vec(eye, focus));
        const auto point = thecad::viewport::intersectRayWithPlane(
            eye, ray, focus, gp_Dir(0, 0, 1));
        require(point.has_value(), "Cursor ray did not intersect the focus plane");
        require(point->Distance(focus) < 1e-8,
                "Cursor ray did not resolve to the expected world-space position");

        const auto parallel = thecad::viewport::intersectRayWithPlane(
            gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1));
        require(!parallel.has_value(), "Parallel cursor ray unexpectedly hit the plane");

        std::cout << "PASS: assembly cursor ray intersects the assembly focus plane\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
