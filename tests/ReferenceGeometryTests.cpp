#include "document/PartDocument.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;using namespace thecad::document;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void closeValue(double actual,double expected){require(std::abs(actual-expected)<1e-6,"Incorrect reference coordinate");}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid reference geometry was accepted");}
GeometryReference datum(const std::string& id){GeometryReference r;r.source=ReferenceSource::Datum;r.featureId=id;return r;}
GeometryReference plane(int index){GeometryReference r;r.source=ReferenceSource::BasePlane;r.index=index;return r;}
GeometryReference axis(int index){GeometryReference r;r.source=ReferenceSource::BaseAxis;r.index=index;return r;}
std::string point(PartDocument& doc,double x,double y,double z){DatumDefinition d;d.position={x,y,z};return doc.addReferenceGeometry(d);}
std::string make(PartDocument& doc,DatumMethod method,std::vector<GeometryReference> refs,double value=0){DatumDefinition d;d.method=method;d.references=std::move(refs);d.value=value;return doc.addReferenceGeometry(d);}
double volume(const PartDocument& doc){GProp_GProps p;BRepGProp::VolumeProperties(doc.shape(),p);return p.Mass();}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    auto doc=PartDocument::createEmpty();const auto p=point(doc,10,20,30),q=point(doc,10,20,50),r=point(doc,30,20,30);
    require(doc.shape().IsNull(),"Reference points must not produce solids");
    const auto a=make(doc,DatumMethod::AxisTwoPoints,{datum(p),datum(q)});closeValue(doc.resolveReference(datum(a)).frame.Direction().Z(),1);
    const auto pl=make(doc,DatumMethod::PlaneThreePoints,{datum(p),datum(q),datum(r)});closeValue(std::abs(doc.resolveReference(datum(pl)).frame.Direction().Y()),1);
    const auto offset=make(doc,DatumMethod::PlaneOffset,{plane(0)},12);closeValue(doc.resolveReference(datum(offset)).frame.Location().Z(),12);
    const auto intersection=make(doc,DatumMethod::PointAxisPlane,{datum(a),datum(offset)});closeValue(doc.resolveReference(datum(intersection)).frame.Location().Z(),12);
    const auto twoPlanes=make(doc,DatumMethod::AxisTwoPlanes,{plane(0),plane(1)});closeValue(std::abs(doc.resolveReference(datum(twoPlanes)).frame.Direction().X()),1);
    const auto normal=make(doc,DatumMethod::AxisNormalToPlane,{datum(p),datum(offset)});closeValue(doc.resolveReference(datum(normal)).frame.Direction().Z(),1);
    const auto rotated=make(doc,DatumMethod::PlaneAtAngle,{plane(0),axis(0)},45);closeValue(std::abs(doc.resolveReference(datum(rotated)).frame.Direction().Y()),std::sqrt(0.5));
    const auto projected=make(doc,DatumMethod::PointProjection,{datum(p),plane(0)});closeValue(doc.resolveReference(datum(projected)).frame.Location().Z(),0);
    const auto cross=make(doc,DatumMethod::PointTwoAxes,{axis(0),axis(1)});closeValue(doc.resolveReference(datum(cross)).frame.Location().Distance({0,0,0}),0);
    const auto parallel=make(doc,DatumMethod::PlaneThroughPoint,{plane(0),datum(p)});closeValue(doc.resolveReference(datum(parallel)).frame.Location().Z(),30);
    DatumDefinition direction;direction.method=DatumMethod::AxisXYZ;direction.position={1,2,3};direction.direction={1,2,3};const auto explicitAxis=doc.addReferenceGeometry(direction);closeValue(doc.resolveReference(datum(explicitAxis)).frame.Direction().Z(),3/std::sqrt(14.0));
    direction.method=DatumMethod::PlaneXYZ;const auto explicitPlane=doc.addReferenceGeometry(direction);require(doc.resolveReference(datum(explicitPlane)).kind==DatumKind::Plane,"XYZ plane kind");
    auto sketch=doc.sketchOnReferencePlane(offset);sketch.entities={{EntityKind::Rectangle,{0,0},{10,20},1,"rectangle"}};const auto sid=doc.addSketch(sketch);doc.addExtrude(sid,{"rectangle"},5,Direction::Positive);closeValue(volume(doc),1000);require(BRepCheck_Analyzer(doc.shape()).IsValid(),"Reference-plane extrusion must be valid");
    auto feature=*std::find_if(doc.features().begin(),doc.features().end(),[&](const auto& f){return f.id==offset;});feature.reference.value=22;doc.replaceFeature(feature);closeValue(sketchAxes(doc.features()[doc.features().size()-2].sketch).Location().Z(),22);closeValue(volume(doc),1000);
    const auto count=doc.features().size();rejects([&]{make(doc,DatumMethod::AxisTwoPoints,{datum(p),datum(p)});});rejects([&]{make(doc,DatumMethod::AxisTwoPlanes,{plane(0),datum(offset)});});rejects([&]{make(doc,DatumMethod::PointAxisPlane,{axis(0),plane(0)});});rejects([&]{make(doc,DatumMethod::PlaneThreePoints,{datum(p),datum(p),datum(r)});});rejects([&]{make(doc,DatumMethod::PlaneOffset,{datum(p)},2);});require(doc.features().size()==count,"Rejected definitions must be transactional");
    direction.method=DatumMethod::AxisXYZ;direction.direction={0,0,0};rejects([&]{doc.addReferenceGeometry(direction);});
    QTemporaryDir directory;const auto path=directory.filePath("reference.thepart");doc.save(path);auto reopened=PartDocument::load(path);closeValue(volume(reopened),1000);require(reopened.features().size()==doc.features().size(),"Reference features must persist");closeValue(reopened.resolveReference(datum(offset)).frame.Location().Z(),22);
    auto support=reopened.features()[0];support.reference.position={11,22,33};reopened.replaceFeature(support);closeValue(reopened.resolveReference(datum(a)).frame.Location().X(),11);closeValue(reopened.resolveReference(datum(pl)).frame.Location().Y(),22);
    auto box=PartDocument::createBox({20,30,40});const auto options=box.referenceOptions();const auto face=std::find_if(options.begin(),options.end(),[](const auto& o){return o.reference.source==ReferenceSource::BodyFace&&o.geometry.kind==DatumKind::Plane;});require(face!=options.end(),"Solid faces must be reference candidates");make(box,DatumMethod::PlaneOffset,{face->reference},3);auto boxFeature=box.features()[0];boxFeature.box.width=25;rejects([&]{box.replaceFeature(boxFeature);});closeValue(box.parameters().width,20);
    const auto vertex=std::find_if(options.begin(),options.end(),[](const auto& o){return o.reference.source==ReferenceSource::BodyVertex;});require(vertex!=options.end(),"Solid vertices must be reference candidates");make(box,DatumMethod::PointOnReference,{vertex->reference});const auto attached=box.sketchOnFace(1);require(attached.attachment->sourceFeature==box.features()[0].id,"Datums must not replace the last solid feature identity");
    auto tilted=PartDocument::createEmpty();DatumDefinition pose;pose.method=DatumMethod::PlaneXYZ;pose.position={5,7,9};pose.direction={1,2,3};const auto tiltedPlane=tilted.addReferenceGeometry(pose);auto profile=tilted.sketchOnReferencePlane(tiltedPlane);profile.entities={{EntityKind::Rectangle,{-10,-10},{10,10},1,"outline"}};const auto profileId=tilted.addSketch(profile);tilted.addExtrude(profileId,{"outline"},10,Direction::Positive);const auto cuttingPlane=make(tilted,DatumMethod::PlaneOffset,{datum(tiltedPlane)},10);auto cutter=tilted.sketchOnReferencePlane(cuttingPlane);cutter.entities={{EntityKind::Circle,{0,0},{},2,"hole"}};const auto cutterId=tilted.addSketch(cutter);tilted.addCut(cutterId,{"hole"},2,false,Direction::Negative);closeValue(volume(tilted),4000-8*std::acos(-1.0));require(BRepCheck_Analyzer(tilted.shape()).IsValid(),"Tilted reference-plane cut must be valid");
    auto cylinder=PartDocument::createEmpty();Sketch round;round.entities={{EntityKind::Circle,{0,0},{},5,"round"}};const auto roundId=cylinder.addSketch(round);cylinder.addExtrude(roundId,{"round"},10,Direction::Positive);const auto cylinderOptions=cylinder.referenceOptions();const auto cylindrical=std::find_if(cylinderOptions.begin(),cylinderOptions.end(),[](const auto& o){return o.reference.source==ReferenceSource::BodyFace&&o.geometry.kind==DatumKind::Axis;});require(cylindrical!=cylinderOptions.end(),"Cylindrical face must provide an axis");const auto cylinderAxis=make(cylinder,DatumMethod::AxisOnReference,{cylindrical->reference});closeValue(std::abs(cylinder.resolveReference(datum(cylinderAxis)).frame.Direction().Z()),1);
    auto lines=PartDocument::createEmpty();Sketch guide;guide.entities={{EntityKind::Line,{0,0},{20,10},1,"guide"}};const auto guideId=lines.addSketch(guide);GeometryReference line;line.source=ReferenceSource::SketchLine;line.featureId=guideId;line.entityId="guide";const auto lineAxis=make(lines,DatumMethod::AxisOnReference,{line});auto editedGuide=lines.features()[0];editedGuide.sketch.entities[0].second={40,10};lines.replaceFeature(editedGuide);closeValue(lines.resolveReference(datum(lineAxis)).frame.Direction().X(),40/std::sqrt(1700.0));
    direction.method=DatumMethod::AxisXYZ;direction.position={0,0,50};direction.direction={1,0,0};const auto skewAxis=doc.addReferenceGeometry(direction);rejects([&]{make(doc,DatumMethod::PointTwoAxes,{datum(skewAxis),axis(1)});});rejects([&]{make(doc,DatumMethod::PointTwoAxes,{axis(0),axis(0)});});
    doc.save(path);QFile file(path);require(file.open(QIODevice::ReadOnly),"Read saved references");auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();root["version"]=12;require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write version rejection fixture");file.write(QJsonDocument(root).toJson());file.close();rejects([&]{PartDocument::load(path);});
    doc.save(path);require(file.open(QIODevice::ReadOnly),"Read schema fixture");root=QJsonDocument::fromJson(file.readAll()).object();file.close();const auto rejectRoot=[&](QJsonObject bad){require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write rejection fixture");file.write(QJsonDocument(bad).toJson());file.close();rejects([&]{PartDocument::load(path);});};
    auto history=root["features"].toArray();auto first=history[0].toObject();auto definition=first["definition"].toObject();definition["unknown"]=true;first["definition"]=definition;history[0]=first;auto bad=root;bad["features"]=history;rejectRoot(bad);
    history=root["features"].toArray();first=history[0].toObject();definition=first["definition"].toObject();definition["method"]="axis-xyz";definition["direction"]=QJsonArray{0,0,0};first["definition"]=definition;history[0]=first;bad=root;bad["features"]=history;rejectRoot(bad);
    history=root["features"].toArray();first=history[0].toObject();definition=first["definition"].toObject();definition["method"]="point-on-reference";definition["references"]=QJsonArray{QJsonObject{{"source",3},{"feature",QString::fromStdString(doc.features()[0].id)},{"entity",""},{"role",1},{"index",0},{"signature",""}}};first["definition"]=definition;history[0]=first;bad=root;bad["features"]=history;rejectRoot(bad);
    std::cout<<"PASS: 3D reference definitions, degeneracy rejection, dependencies, save/reopen, tilted sketch/extrude/cut, solid identity and conservative topology references\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
