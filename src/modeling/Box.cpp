#include "modeling/Box.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <Precision.hxx>

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace thecad::modeling {

TopoDS_Shape makeBox(double width, double depth, double height)
{
    for (const double length : {width, depth, height}) {
        if (!std::isfinite(length) || length <= Precision::Confusion()) {
            throw std::invalid_argument("Box dimensions must be finite and exceed the kernel tolerance.");
        }
    }
    BRepPrimAPI_MakeBox builder(width, depth, height);
    return builder.Shape();
}

} // namespace thecad::modeling
