#include "modeling/FeaturePreview.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pnt.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
namespace {
double volume(const TopoDS_Shape& shape){if(shape.IsNull())return 0;GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return properties.Mass();}
void expect(const TopoDS_Shape& shape,double expected){if(std::abs(volume(shape)-expected)>1e-6 || (!shape.IsNull()&&!BRepCheck_Analyzer(shape).IsValid()))throw std::runtime_error("Incorrect material preview geometry");}
}
int main(){try{
    const auto block=BRepPrimAPI_MakeBox(20,20,20).Shape();
    auto change=materialChanges({},block);expect(change.added,8000);expect(change.removed,0);
    const auto tall=BRepPrimAPI_MakeBox(20,20,30).Shape();change=materialChanges(block,tall);expect(change.added,4000);expect(change.removed,0);
    change=materialChanges(tall,block);expect(change.added,0);expect(change.removed,4000);
    const auto tool=BRepPrimAPI_MakeBox(gp_Pnt(5,5,-10),10,10,40).Shape();
    const auto cut=BRepAlgoAPI_Cut(block,tool).Shape();change=materialChanges(block,cut);expect(change.added,0);expect(change.removed,2000);
    change=materialChanges(block,block);expect(change.added,0);expect(change.removed,0);
    change=materialChanges({},{});expect(change.added,0);expect(change.removed,0);
    const auto inside = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,10), gp_Dir(0,0,1)), 1, 3, 1, 3).Face();
    const auto outside = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,25), gp_Dir(0,0,1)), 1, 3, 1, 3).Face();
    const auto faces = edgeTreatmentHighlights(block, {inside, outside});
    const TopExp_Explorer added(faces.added, TopAbs_FACE), removed(faces.removed, TopAbs_FACE);
    if (!added.More() || !added.Current().IsSame(outside) || !removed.More() || !removed.Current().IsSame(inside))
        throw std::runtime_error("Edge preview must color the existing treatment faces by stock classification");
    expect(block,8000);expect(tall,12000);expect(cut,6000);
    std::cout<<"PASS: exact addition/removal previews, blind and through cuts, edits and unchanged source geometry\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
