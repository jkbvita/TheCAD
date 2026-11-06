#pragma once
#include <TopoDS_Shape.hxx>
#include <map>
#include <string>
#include <vector>
class BRepBuilderAPI_MakeShape;
namespace thecad::modeling {
// Names describe generating feature roles, never geometric proximity or map order.
// A split retains multiple images and is deliberately not a unique reference.
struct TopologyNames {
    std::map<std::string,std::vector<TopoDS_Shape>> images;
    void add(const std::string&,const TopoDS_Shape&);
    std::string nameOf(const TopoDS_Shape&) const;
    TopoDS_Shape resolve(const std::string&,TopAbs_ShapeEnum) const;
    void complete(const TopoDS_Shape&);
};
std::string topologyKey(const std::string& role,std::vector<std::string> sources);
TopologyNames propagateTopology(const TopologyNames&,BRepBuilderAPI_MakeShape&,const TopoDS_Shape&);
}
