#include "modeling/Box.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <Precision.hxx>

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace thecad::modeling {

TopoDS_Shape makeBox(double width, double depth, double height,TopologyNames* names,const std::string& featureId)
{
    for (const double length : {width, depth, height}) {
        if (!std::isfinite(length) || length <= Precision::Confusion()) {
            throw std::invalid_argument("Box dimensions must be finite and exceed the kernel tolerance.");
        }
    }
    BRepPrimAPI_MakeBox builder(width, depth, height);
    const auto shape=builder.Shape();if(names){names->images.clear();const std::pair<const char*,TopoDS_Shape> faces[]={{"xmin",builder.LeftFace()},{"xmax",builder.RightFace()},{"ymin",builder.FrontFace()},{"ymax",builder.BackFace()},{"zmin",builder.BottomFace()},{"zmax",builder.TopFace()}};for(const auto& [role,face]:faces)names->add(topologyKey(role,{featureId}),face);names->complete(shape);}return shape;
}

} // namespace thecad::modeling
