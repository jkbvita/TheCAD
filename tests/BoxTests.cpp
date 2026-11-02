#include "modeling/Box.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    int result = 0;
    for (TopExp_Explorer explorer(shape, type); explorer.More(); explorer.Next()) {
        ++result;
    }
    return result;
}

void checkBox(double width, double depth, double height)
{
    const auto box = thecad::modeling::makeBox(width, depth, height);
    require(!box.IsNull(), "Box must exist");
    require(BRepCheck_Analyzer(box).IsValid(), "Box topology must be valid");
    require(count(box, TopAbs_SOLID) == 1, "Box must contain one solid");
    require(count(box, TopAbs_FACE) == 6, "Box must have six faces");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(box, properties);
    const double expectedVolume = width * depth * height;
    require(std::abs(properties.Mass() - expectedVolume) < expectedVolume * 1e-10,
            "Box volume must match its dimensions");
    Bnd_Box bounds;
    BRepBndLib::Add(box, bounds);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double tolerance = 10.0 * Precision::Confusion();
    require(std::abs(xmin) < tolerance && std::abs(ymin) < tolerance && std::abs(zmin) < tolerance,
            "Box must start at the origin");
    require(std::abs(xmax - width) < tolerance && std::abs(ymax - depth) < tolerance
            && std::abs(zmax - height) < tolerance, "Box bounds must match its dimensions");
}
}

int main()
{
    try {
        checkBox(100.0, 70.0, 40.0);
        checkBox(1.25, 2.5, 0.5);
        for (const double invalid : {0.0, -1.0, Precision::Confusion() / 2.0,
                 Precision::Confusion(), std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            for (int dimension = 0; dimension < 3; ++dimension) {
                double lengths[] = {10.0, 20.0, 30.0};
                lengths[dimension] = invalid;
                bool rejected = false;
                try {
                    (void)thecad::modeling::makeBox(lengths[0], lengths[1], lengths[2]);
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                require(rejected, "Invalid box dimension must be rejected on every axis");
            }
        }
        std::cout << "PASS: topology, solid/face counts, volume, bounds, and 21 invalid inputs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
