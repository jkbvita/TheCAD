#include "modeling/SketchTools.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/DimensionInference.h"
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
#include <stdexcept>
using namespace thecad::modeling;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
double volume(const TopoDS_Shape& s){GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
int main(){try{
    Sketch points;points.entities={{EntityKind::Point,{0,0},{},1,"p"},{EntityKind::Point,{10,20},{},1,"q"},{EntityKind::Line,{-30,20},{30,20},1,"axis",true}};
    points.relations={{"origin",RelationKind::Coincident,{"p",PointRole::First},{}},{"on",RelationKind::PointOnLine,{"q",PointRole::First},{"axis",PointRole::First}}};
    require(sketchPoints(points).size()==5,"Point entities expose exactly one reference");require(closedContours(points).empty(),"Points and centerlines cannot bound extrusion profiles");
    auto solved=requireSolvedSketch(solvePlanarSketch(points));auto moved=moveSketchPoint(solved,{"q",PointRole::First},{15,20});validateSketchRelations(moved);
    auto d=inferDimension(solved,{{"p",PointRole::First},{"q",PointRole::First}}, {},{20,40});d.id="distance";d.kind=DimensionKind::HorizontalDistance;d.sense=1;d.value=25;
    auto driven=setSketchDimension(solved,d);near(sketchPointValue(driven,{"q",PointRole::First}).x,25);
    auto ordinary=points;ordinary.entities[2].construction=false;require(ordinary.entities[2].id=="axis","Construction conversion retains identity");
    Sketch onCircle;onCircle.entities={{EntityKind::Circle,{20,20},{},5,"circle"},{EntityKind::Point,{25,20},{},1,"point"}};onCircle.relations={{"on-circle",RelationKind::PointOnCircle,{"point",PointRole::First},{"circle",PointRole::Center}}};
    validateSketchRelations(requireSolvedSketch(solvePlanarSketch(onCircle)));
    QTemporaryDir directory;
    for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}){
        Sketch s;s.plane=plane;s=addRegularPolygon(s,"polygon",{{0,0},10,0,6});auto result=solvePlanarSketch(s);require(result.success,result.diagnostics.message.c_str());require(result.diagnostics.degreesOfFreedom==4,"Regular polygon has center, size and orientation freedom");s=result.sketch;
        const auto edgeIds=s.polygons[0].edges;require(s.entities.size()==7 && closedContours(s).size()==1,"Hexagon exposes six boundary edges and a center point");
        auto edited=editRegularPolygon(s,"polygon",{{30,20},20,0.3,6});validatePolygons(edited,true);require(edited.polygons[0].edges==edgeIds,"Parameter edits preserve edge IDs");near(polygonParameters(edited,edited.polygons[0]).radius,20);
        auto triangle=editRegularPolygon(edited,"polygon",{{30,20},20,0.3,3});require(triangle.polygons[0].edges.size()==3 && closedContours(triangle).size()==1,"Polygon side-count editing");
        auto radius=inferDimension(s,{{s.polygons[0].centerId,PointRole::First},{edgeIds[0],PointRole::First}}, {},{20,15});radius.kind=DimensionKind::PointDistance;radius.value=15;radius.id="radius";
        auto changed=setSketchDimension(s,radius);validatePolygons(changed,true);near(polygonParameters(changed,changed.polygons[0]).radius,15);
        auto side=inferDimension(s,{}, {edgeIds[0]},{15,-10});side.kind=DimensionKind::LineLength;side.value=14;side.id="side";
        auto resized=setSketchDimension(s,side);validatePolygons(resized,true);near(polygonParameters(resized,resized.polygons[0]).radius,14);
        auto part=thecad::document::PartDocument::createEmpty();const auto id=part.addSketch(changed);part.addExtrude(id,edgeIds,5,Direction::Positive);const double expected=3*std::sqrt(3.0)/2*15*15*5;near(volume(part.shape()),expected);
        const auto path=directory.filePath("polygon.thepart");part.save(path);auto loaded=thecad::document::PartDocument::load(path);require(loaded.features()[0].sketch.polygons[0].edges==edgeIds,"Polygon reference persistence");near(volume(loaded.shape()),expected);
        auto feature=loaded.features()[0];feature.sketch.dimensions[0].value=20;loaded.replaceFeature(feature);near(volume(loaded.shape()),expected*400/225);validatePolygons(loaded.features()[0].sketch,true);
        auto construction=s;for(auto& e:construction.entities)if(e.id==edgeIds[0])e.construction=true;require(closedContours(construction).empty(),"A construction edge must not close a polygon contour");
        const auto removed=removeSketchEntity(s,edgeIds[0]);require(removed.polygons.empty(),"Deleting a polygon member dissolves its regularity group without dangling references");
    }
    auto part=thecad::document::PartDocument::createEmpty();part.addSketch(points);const auto path=directory.filePath("points.thepart");part.save(path);auto restored=thecad::document::PartDocument::load(path);require(restored.features()[0].sketch.entities[0].kind==EntityKind::Point && restored.features()[0].sketch.entities[2].construction,"Point and construction persistence");
    auto polygonPart=thecad::document::PartDocument::createEmpty();polygonPart.addSketch(addRegularPolygon({},"hexagon",{{0,0},10,0,6}));polygonPart.save(path);
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Read schema fixture");const auto good=QJsonDocument::fromJson(file.readAll()).object();file.close();
    for(int mutation=0;mutation<8;++mutation){auto root=good;auto history=root["features"].toArray();auto feature=history[0].toObject();auto groups=feature["polygons"].toArray();auto group=groups[0].toObject();auto edges=group["edges"].toArray();
        if(mutation==0)group["center"]="missing";if(mutation==1){edges[1]=edges[0];group["edges"]=edges;}if(mutation==2)group["edges"]=QJsonArray{edges[0],edges[1]};if(mutation==3)group["id"]="bad id";
        groups[0]=group;feature["polygons"]=groups;
        if(mutation==4){auto entities=feature["entities"].toArray();auto e=entities[1].toObject();e["construction"]="true";entities[1]=e;feature["entities"]=entities;}
        if(mutation==5)feature.remove("polygons");if(mutation==6)root["version"]=7;if(mutation==7){auto relations=feature["relations"].toArray();relations.removeFirst();feature["relations"]=relations;}history[0]=feature;root["features"]=history;
        require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write malformed fixture");file.write(QJsonDocument(root).toJson());file.close();bool rejected=false;try{thecad::document::PartDocument::load(path);}catch(const std::exception&){rejected=true;}require(rejected,"Malformed or disguised polygon metadata must reject");
    }
    // Construction geometry crossing a valid boundary must be ignored by the contour graph.
    auto crossed=addRegularPolygon({},"outline",{{0,0},10,0,6});crossed.entities.push_back({EntityKind::Line,{-20,0},{20,0},1,"centerline",true});crossed.entities.push_back({EntityKind::Point,{0,0},{},1,"free-point"});require(closedContours(crossed).size()==1,"Construction crossings/points cannot split extrusion regions");
    std::cout<<"PASS: persistent points/on-geometry relations, construction exclusion, regular polygon parameters/edge dimensions, IDs, save/reopen and extrusion regeneration on all planes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
