#include "modeling/FeaturePreview.h"
#include <BRepPrimAPI_MakeBox.hxx>
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
    expect(block,8000);expect(tall,12000);expect(cut,6000);
    std::cout<<"PASS: exact addition/removal previews, blind and through cuts, edits and unchanged source geometry\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
