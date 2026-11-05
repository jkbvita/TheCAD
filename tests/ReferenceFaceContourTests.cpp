#include "document/PartDocument.h"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <functional>
#include <cmath>
using namespace thecad::modeling;
using namespace thecad::document;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void reject(const std::function<void()>& operation){bool failed=false;try{operation();}catch(const std::exception& e){failed=std::string(e.what()).size()>0;}require(failed,"Expected clear rejection");}
double volume(const TopoDS_Shape& shape){require(BRepCheck_Analyzer(shape).IsValid(),"Invalid solid");GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
Sketch rectangle(double offset=0){Sketch s;s.entities={
{EntityKind::Line,{offset,0},{offset+20,0},1,"a"+std::to_string(int(offset))},
{EntityKind::Line,{offset+20,0},{offset+20,10},1,"b"+std::to_string(int(offset))},
{EntityKind::Line,{offset+20,10},{offset,10},1,"c"+std::to_string(int(offset))},
{EntityKind::Line,{offset,10},{offset,0},1,"d"+std::to_string(int(offset))}};return s;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 QTemporaryDir directory;require(directory.isValid(),"Temp directory");
 for(int branches:{0,1,3}){auto s=rectangle();
 if(branches>0)s.entities.push_back({EntityKind::Line,{0,0},{-5,-5},1,"branch1"});
 if(branches>1){s.entities.push_back({EntityKind::Line,{-5,-5},{-10,-5},1,"branch2"});s.entities.push_back({EntityKind::Line,{20,10},{25,15},1,"branch3"});}
 auto contours=closedContours(s);require(contours.size()==1 && contours[0].entityIds.size()==4,"Rectangle branches must not join perimeter");
 require(contourContains(contours[0],s,{5,5})&&!contourContains(contours[0],s,{-2,-2}),"Actual region hit test");
 auto selected=selectContour(s,contours[0].entityIds);
 auto cutDocument=PartDocument::createBox({40,30,10});const auto cutSketch=cutDocument.addSketch(s);cutDocument.addCut(cutSketch,closedContours(cutDocument.features()[1].sketch)[0].entityIds,3,false,Direction::Positive);require(std::abs(volume(cutDocument.shape())-11400)<1e-6,"Dangling geometry must not prevent a cut");
 require(std::abs(volume(makeExtrusion(selected,3,Direction::Positive))-600)<1e-6,"Selected region volume");
 auto document=PartDocument::createEmpty();auto id=document.addSketch(s);document.addExtrude(id,closedContours(document.features()[0].sketch)[0].entityIds,3,Direction::Positive);
 const auto path=directory.filePath("branches.thepart");document.save(path);auto loaded=PartDocument::load(path);
 require(loaded.features()[0].sketch.entities.size()==s.entities.size(),"Open geometry lost on reopen");require(std::abs(volume(loaded.shape())-600)<1e-6,"Saved contour regeneration");}
 Sketch legacy;legacy.entities={{EntityKind::Rectangle,{0,0},{20,10},1,"rectangle"},{EntityKind::Line,{0,0},{-5,0},1,"tail"}};require(closedContours(legacy).size()==1,"Legacy rectangle with corner branch");
 auto circleCross=legacy;circleCross.entities={{EntityKind::Circle,{0,0},{},3,"circle"},{EntityKind::Line,{-5,0},{5,0},1,"line"}};require(closedContours(circleCross).size()==2,"An intersecting diameter defines two selectable regions");
 auto triangle=rectangle();triangle.entities.resize(3);triangle.entities[1].second={0,10};triangle.entities[2].first={0,10};triangle.entities[2].second={0,0};triangle.entities.push_back({EntityKind::Line,{0,0},{-3,0},1,"tail"});require(closedContours(triangle).size()==1,"Triangle with tail");
 auto two=rectangle();auto other=rectangle(40);two.entities.insert(two.entities.end(),other.entities.begin(),other.entities.end());require(closedContours(two).size()==2,"Independent cycles");
 two.entities.push_back({EntityKind::Line,{20,0},{40,0},1,"bridge"});require(closedContours(two).size()==2,"Bridge between closed cycles");
 Sketch open;open.entities={{EntityKind::Line,{0,0},{1,1},1,"open"}};require(closedContours(open).empty(),"Only open lines");
 auto crossing=rectangle();crossing.entities.push_back({EntityKind::Line,{-1,5},{21,5},1,"cross"});require(closedContours(crossing).size()==2,"A crossing line divides the rectangle into selectable regions");
 auto base=PartDocument::createEmpty();auto profile=rectangle();auto source=base.addSketch(profile);base.addExtrude(source,closedContours(base.features()[0].sketch)[0].entityIds,15,Direction::Positive);
 TopTools_IndexedMapOfShape faces;TopExp::MapShapes(base.shape(),TopAbs_FACE,faces);int top=0,side=0;
 for(int i=1;i<=faces.Extent();++i){const auto axes=planarFaceAxes(TopoDS::Face(faces(i)));if(axes.Direction().Z()>0.99)top=i;if(axes.Direction().X()>0.99)side=i;}
 require(top && side,"Top and side faces");
 for(int index:{top,side}){auto document=base;auto s=document.sketchOnFace(index);const auto axes=sketchAxes(s);auto point=sketchPoint(s,{3,4});gp_Vec delta(axes.Location(),point);require(std::abs(delta.Dot(gp_Vec(axes.Direction())))<1e-8,"Frame plane transform");require(std::abs(delta.Dot(gp_Vec(axes.XDirection()))-3)<1e-8,"Frame local X");
 s.entities={{EntityKind::Circle,{0,0},{},2,"circle"}};auto id=document.addSketch(s);
 document.addExtrude(id,{"circle"},3,Direction::Positive);require(volume(document.shape())>volume(base.shape()),"Face extrusion");
 auto path=directory.filePath("face.thepart");document.save(path);auto loaded=PartDocument::load(path);require(loaded.features()[2].sketch.attachment.has_value(),"Attachment lost");require(std::abs(volume(loaded.shape())-volume(document.shape()))<1e-6,"Face regeneration");
 auto changed=loaded.features()[1];changed.length=16;reject([&]{loaded.replaceFeature(changed);});require(loaded.features()[1].length==15,"Failed upstream edit changed document");
 auto cut=base;auto cutId=cut.addSketch(s);cut.addCut(cutId,{"circle"},3,true,Direction::Negative);require(volume(cut.shape())<volume(base.shape()),"Face through cut");
 auto tampered=loaded.features()[2];tampered.sketch.attachment->frame.SetLocation(gp_Pnt(123,456,789));reject([&]{loaded.replaceFeature(tampered);});
 }
 gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,1,1)),0.47);rotation.SetTranslationPart(gp_Vec(12,-8,35));auto rotated=BRepBuilderAPI_Transform(base.shape(),rotation,Standard_True).Shape();TopTools_IndexedMapOfShape rotatedFaces;TopExp::MapShapes(rotated,TopAbs_FACE,rotatedFaces);
 for(int i=1;i<=rotatedFaces.Extent();++i){Sketch s;s.attachment=FaceAttachment{"test",i,"test",planarFaceAxes(TopoDS::Face(rotatedFaces(i)))};s.entities={{EntityKind::Circle,{0,0},{},1,"c"}};require(std::abs(volume(makeExtrusion(s,2,Direction::Positive))-2*3.141592653589793)<1e-6,"Rotated face prism");}
 auto cylinder=BRepPrimAPI_MakeCylinder(3,5).Shape();TopTools_IndexedMapOfShape curved;TopExp::MapShapes(cylinder,TopAbs_FACE,curved);reject([&]{planarFaceAxes(TopoDS::Face(curved(1)));});
 std::cout<<"PASS: branch contours A-G, top/side/rotated face transforms, face extrusion/cut, save/reopen and fail-closed attachment validation\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
