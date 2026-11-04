#include "modeling/ConstraintSolver.h"
#include "document/PartDocument.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace thecad::modeling;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void closeValue(double a,double b){if(std::abs(a-b)>1e-6)throw std::runtime_error("Numeric result "+std::to_string(a)+" expected "+std::to_string(b));}
SketchDimension dimension(std::string id,DimensionKind kind,SketchPointRef a,SketchPointRef b,double value,int sense=1){SketchDimension d;d.id=id;d.kind=kind;d.first=a;d.second=b;d.value=value;d.sense=sense;return d;}
SketchRelation fixed(std::string id,SketchPointRef point,Point2 position){SketchRelation r;r.id=id;r.kind=RelationKind::FixedPoint;r.first=point;r.fixedPosition=position;return r;}
Sketch rectangle(){Sketch s;s.entities={{EntityKind::Line,{0,0},{50,0},1,"a"},{EntityKind::Line,{50,0},{50,30},1,"b"},{EntityKind::Line,{50,30},{0,30},1,"c"},{EntityKind::Line,{0,30},{0,0},1,"d"}};
for(int i=0;i<4;++i){s.relations.push_back({"join-"+std::to_string(i),RelationKind::Coincident,{s.entities[i].id,PointRole::Second},{s.entities[(i+1)%4].id,PointRole::First}});s.relations.push_back({"axis-"+std::to_string(i),i%2?RelationKind::Vertical:RelationKind::Horizontal,{s.entities[i].id,PointRole::First},{s.entities[i].id,PointRole::Second}});}s.relations.push_back({"origin",RelationKind::Coincident,{"a",PointRole::First},{}});return s;}
Sketch solved(const Sketch& s,const SolveOptions& options={}){auto result=solvePlanarSketch(s,options);if(!result.success)throw std::runtime_error(result.diagnostics.message+" "+(result.diagnostics.conflicting.empty()?"":result.diagnostics.conflicting.front())+" "+(result.diagnostics.redundant.empty()?"":result.diagnostics.redundant.front()));return result.sketch;}

double volume(const TopoDS_Shape& shape){GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return properties.Mass();}
int main(){try {
 using namespace thecad::document;QTemporaryDir directory;require(directory.isValid(),"Temporary directory");
 auto sketch=rectangle();sketch.dimensions={dimension("width",DimensionKind::HorizontalDistance,{"a",PointRole::First},{"a",PointRole::Second},50),dimension("height",DimensionKind::VerticalDistance,{"a",PointRole::First},{"d",PointRole::First},30)};
 sketch.dimensions[0].annotation={25,-10};
 for(auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}) {
  sketch.plane=plane;auto document=PartDocument::createEmpty();auto id=document.addSketch(sketch);document.addExtrude(id,{"a","b","c","d"},10,Direction::Positive);closeValue(volume(document.shape()),15000);
  const auto path=directory.filePath("dimensions.thepart");document.save(path);auto loaded=PartDocument::load(path);auto feature=loaded.features()[0];
  require(feature.sketch.space==SketchSpace::Planar2D && feature.sketch.dimensions.size()==2,"Persistent dimensions and sketch space");closeValue(feature.sketch.dimensions[0].annotation.y,-10);
  feature.sketch.dimensions[0].value=80;loaded.replaceFeature(feature);closeValue(volume(loaded.shape()),24000);closeValue(loaded.features()[0].sketch.entities[1].second.x,80);
  auto invalid=loaded.features()[0];invalid.sketch.dimensions.push_back(dimension("contradiction",DimensionKind::HorizontalDistance,{"a",PointRole::First},{"a",PointRole::Second},70));bool failed=false;
  try{loaded.replaceFeature(invalid);}catch(const std::exception&){failed=true;}require(failed,"Conflicting document edit must fail");closeValue(volume(loaded.shape()),24000);require(loaded.features()[0].sketch.dimensions.size()==2,"Rollback dimensions");
  loaded.save(path);closeValue(volume(PartDocument::load(path).shape()),24000);
 }
 auto document=PartDocument::createBox({100,100,20});auto face=document.sketchOnFace(6);face.entities={{EntityKind::Circle,{10,10},{},3,"circle"}};face.dimensions={dimension("radius",DimensionKind::CircleRadius,{"circle",PointRole::Center},{},5)};
 SketchRelation anchor;anchor.id="fixed-center";anchor.kind=RelationKind::FixedPoint;anchor.first={"circle",PointRole::Center};anchor.fixedPosition={10,10};face.relations={anchor};
 auto reference=dimension("diameter-reference",DimensionKind::CircleDiameter,{"circle",PointRole::Center},{},10);reference.driving=false;reference.annotation={20,15};face.dimensions.push_back(reference);
 auto faceId=document.addSketch(face);document.addExtrude(faceId,{"circle"},10,Direction::Positive);auto feature=document.features()[1];feature.sketch.dimensions[0].value=7;document.replaceFeature(feature);closeValue(document.features()[1].sketch.entities[0].radius,7);
 const auto path=directory.filePath("face.thepart");document.save(path);auto loaded=PartDocument::load(path);require(loaded.features()[1].sketch.relations[0].kind==RelationKind::FixedPoint,"Fixed relation persistence");closeValue(loaded.features()[1].sketch.entities[0].radius,7);closeValue(loaded.features()[1].sketch.dimensions[1].value,14);require(!loaded.features()[1].sketch.dimensions[1].driving,"Reference status persistence");
 QFile file(path);require(file.open(QIODevice::ReadOnly),"Read dimension file");const auto good=QJsonDocument::fromJson(file.readAll()).object();file.close();
 for(int mutation=0;mutation<8;++mutation){auto root=good;auto history=root["features"].toArray();auto f=history[1].toObject();auto dims=f["dimensions"].toArray();auto d=dims[0].toObject();
  if(mutation==0)d["type"]="unsupported";if(mutation==1)d["driving"]="true";if(mutation==2)d["sense"]=0;if(mutation==3)d["first"]=QJsonObject{{"entity","missing"},{"point","center"}};if(mutation==4)d["value"]=-2;if(mutation==5)f["space"]="spatial3d";if(mutation==6)f.remove("space");
  dims[0]=d;f["dimensions"]=dims;if(mutation==7)f.remove("dimensions");history[1]=f;root["features"]=history;require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write invalid fixture");file.write(QJsonDocument(root).toJson());file.close();bool failed=false;try{PartDocument::load(path);}catch(const std::exception&){failed=true;}require(failed,"Malformed dimension metadata must reject");
 }
 auto anglePart=PartDocument::createEmpty();Sketch triangle;triangle.entities={{EntityKind::Line,{0,0},{20,0},1,"base"},{EntityKind::Line,{0,0},{10,10},1,"side"},{EntityKind::Line,{10,10},{20,0},1,"end"}};
 triangle.relations={fixed("fixed-start",{"base",PointRole::First},{0,0}),fixed("fixed-end",{"base",PointRole::Second},{20,0}),{"join-a",RelationKind::Coincident,{"side",PointRole::First},{"base",PointRole::First}},{"join-b",RelationKind::Coincident,{"side",PointRole::Second},{"end",PointRole::First}},{"join-c",RelationKind::Coincident,{"end",PointRole::Second},{"base",PointRole::Second}}};
 triangle.dimensions={dimension("inclination",DimensionKind::LineAngle,{"base",PointRole::First},{"side",PointRole::First},60),dimension("side-length",DimensionKind::PointDistance,{"side",PointRole::First},{"side",PointRole::Second},10)};
 const auto triangleId=anglePart.addSketch(triangle);anglePart.addExtrude(triangleId,{"base","side","end"},5,Direction::Positive);closeValue(volume(anglePart.shape()),500*std::sin(3.14159265358979323846/3));
 const auto anglePath=directory.filePath("angle.thepart");anglePart.save(anglePath);auto angular=PartDocument::load(anglePath);require(angular.features()[0].sketch.dimensions[0].kind==DimensionKind::LineAngle,"Angle kind persistence");auto changedAngle=angular.features()[0];changedAngle.sketch.dimensions[0].value=30;angular.replaceFeature(changedAngle);closeValue(volume(angular.shape()),250);angular.save(anglePath);closeValue(volume(PartDocument::load(anglePath).shape()),250);
 QFile angleFile(anglePath);require(angleFile.open(QIODevice::ReadOnly),"Read schema fixture");auto angularRoot=QJsonDocument::fromJson(angleFile.readAll()).object();angleFile.close();angularRoot["version"]=5;require(angleFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write old-schema fixture");angleFile.write(QJsonDocument(angularRoot).toJson());angleFile.close();bool oldRejected=false;try{PartDocument::load(anglePath);}catch(const std::exception&){oldRejected=true;}require(oldRejected,"New dimensions cannot masquerade as schema 5");
 Sketch spacing;spacing.entities={{EntityKind::Line,{0,0},{20,0},1,"first"},{EntityKind::Line,{0,10},{20,10},1,"second"}};spacing.relations={fixed("a",{"first",PointRole::First},{0,0}),fixed("b",{"first",PointRole::Second},{20,0})};spacing.dimensions={dimension("gap",DimensionKind::LineDistance,{"first",PointRole::First},{"second",PointRole::First},15)};auto spacingPart=PartDocument::createEmpty();spacingPart.addSketch(spacing);spacingPart.save(anglePath);auto loadedSpacing=PartDocument::load(anglePath);closeValue(loadedSpacing.features()[0].sketch.entities[1].first.y,15);closeValue(loadedSpacing.features()[0].sketch.entities[1].second.y,15);
 auto independent=rectangle();independent.dimensions={dimension("width",DimensionKind::HorizontalDistance,{"a",PointRole::First},{"a",PointRole::Second},80)};auto changed=setSketchDimension(independent,independent.dimensions[0]);closeValue(changed.entities[0].second.x,80);closeValue(independent.entities[0].second.x,50);require(removeSketchDimension(changed,"width").dimensions.empty(),"Dimension deletion");
 std::cout<<"PASS: persistent dimensions, fixed anchors, all planes/face support, width-driven extrusion regeneration and failed-edit rollback\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
