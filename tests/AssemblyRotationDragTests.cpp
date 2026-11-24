#include "viewport/ComponentRotationDrag.h"
#include <iostream>
#include <stdexcept>
using thecad::viewport::componentRotationDrag;
int main()
{
    try
    {
        const auto check = [](bool valid) { if (!valid) throw std::runtime_error("Trackball mapping failed"); };
        const gp_Dir view(0, 0, -1), up(0, 1, 0);
        const auto yaw = componentRotationDrag({0, 0}, {100, 0}, {}, 100, view, up);
        check((yaw * gp_Vec(0, 0, 1) - gp_Vec(1, 0, 0)).Magnitude() < 1e-8);
        const auto roll = componentRotationDrag({100, 0}, {0, -100}, {}, 100, view, up);
        check((roll * gp_Vec(1, 0, 0) - gp_Vec(0, 1, 0)).Magnitude() < 1e-8);
        const auto reverse = componentRotationDrag({0, -100}, {100, 0}, {}, 100, view, up);
        check(std::abs((reverse * roll).GetRotationAngle()) < 1e-8);
        const auto scaled = componentRotationDrag({210, 20}, {10, -180}, {10, 20}, 200, view, up);
        check(std::abs((scaled * roll.Inverted()).GetRotationAngle()) < 1e-8);
        const auto distant = componentRotationDrag({10000, 0}, {-10000, 0}, {}, 100, view, up);
        check(std::isfinite(distant.SquareNorm()) && std::abs(distant.SquareNorm() - 1) < 1e-8);
        std::cout << "PASS: component trackball yaw, roll, reversal, scaling and distant cursor\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
