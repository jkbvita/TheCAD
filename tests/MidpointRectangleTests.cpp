#include "modeling/Sketch.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/RelationCommands.h"
#include "document/PartDocument.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Sketch rectangle(SketchPlane plane){
    Sketch s;s.plane=plane;s.entities={{EntityKind::Line,{0,0},{-40,0},1,"a"},{EntityKind::Line,{-40,0},{-40,30},1,"b"},{EntityKind::Line,{-40,30},{0,30},1,"c"},{EntityKind::Line,{0,30},{0,0},1,"d"}};
    for(int i=0;i<4;++i){s.relations.push_back({"axis-"+std::to_string(i),i%2?RelationKind::Vertical:RelationKind::Horizontal,{s.entities[i].id,PointRole::First},{s.entities[i].id,PointRole::Second}});s.relations.push_back({"join-"+std::to_string(i),RelationKind::Coincident,{s.entities[i].id,PointRole::Second},{s.entities[(i+1)%4].id,PointRole::First}});}
    s.relations.push_back({"origin",RelationKind::Coincident,{"a",PointRole::First},{}});return s;
}
int main(){try{
    for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}){
        const auto s=rectangle(plane);
        for(const auto& requested:std::vector<std::pair<SketchPointRef,Point2>>{{{"a",PointRole::Second},{-55,9}},{{"b",PointRole::Second},{-55,45}},{{"c",PointRole::Second},{9,45}}}){
            const auto moved=moveSketchPoint(s,requested.first,requested.second);validateSketchRelations(moved);
            near(moved.entities[0].first.x,0);near(moved.entities[0].first.y,0);
            const auto p=sketchPointValue(moved,requested.first);near(p.x,requested.first.entityId=="c"?0:-55);near(p.y,requested.first.entityId=="a"?0:45);
            require(moved.relations.size()==s.relations.size(),"Corner resizing preserves every relation");require(closedContours(moved).size()==1,"Resized rectangle remains one closed profile");
        }
        auto dimensioned=s;SketchDimension width;width.id="width";width.kind=DimensionKind::LineLength;width.first={"a",PointRole::First};width.second={"a",PointRole::Second};width.value=40;dimensioned=setSketchDimension(dimensioned,width);
        auto taller=moveSketchPoint(dimensioned,{"c",PointRole::Second},{8,45});near(taller.entities[2].second.y,45);near(dimensionMeasurement(taller,width),40);validateSketchRelations(taller);
    }
    Sketch sketch;sketch.entities={{EntityKind::Line,{-20,10},{40,30},1,"guide"},{EntityKind::Point,{10,20},{},1,"point"},{EntityKind::Line,{10,20},{30,50},1,"branch"}};
    sketch.relations={{"point-mid",RelationKind::Midpoint,{"point",PointRole::First},{"guide",PointRole::First}},{"line-mid",RelationKind::Midpoint,{"branch",PointRole::First},{"guide",PointRole::First}}};
    validateSketchRelations(sketch);near(sketchPointValue(sketch,{"guide",PointRole::Midpoint}).x,10);
    auto guide=sketch.entities[0];guide.first={-30,-10};guide.second={50,50};auto rotated=editSketchEntity(sketch,guide);validateSketchRelations(rotated);near(rotated.entities[1].first.x,10);near(rotated.entities[1].first.y,20);near(rotated.entities[2].first.y,20);
    guide=rotated.entities[0];guide.second={70,70};auto resized=editSketchEntity(rotated,guide);const auto currentMidpoint=sketchPointValue(resized,{"guide",PointRole::Midpoint});near(resized.entities[1].first.x,currentMidpoint.x);near(resized.entities[1].first.y,currentMidpoint.y);near(resized.entities[2].first.x,currentMidpoint.x);require(std::abs(currentMidpoint.x-10)>1,"Midpoint attachment follows a resized line");validateSketchRelations(resized);
    Sketch free;free.entities={sketch.entities[0],{EntityKind::Point,{-30,-20},{},1,"point"}};free=addSelectionRelations(free,RelationKind::FixedEntity,{"guide"},"fixed-guide");
    auto snapped=moveSketchPoint(free,{"point",PointRole::First},{10,20},SketchPointRef{"guide",PointRole::Midpoint});near(snapped.entities[1].first.x,10);near(snapped.entities[1].first.y,20);require(snapped.relations.back().kind==RelationKind::Midpoint,"Dragging creates a persistent midpoint relation");require(snapped.relations.back().second.role==PointRole::First,"Derived snap references are converted before persistence");
    auto again=moveSketchPoint(snapped,{"point",PointRole::First},{10,20},SketchPointRef{"guide",PointRole::Midpoint});require(again.relations.size()==snapped.relations.size(),"Repeated snapping does not duplicate midpoint equations");
    auto onLine=free;onLine.entities[1].first={25,25};onLine.relations.push_back({"on-line",RelationKind::PointOnLine,{"point",PointRole::First},{"guide",PointRole::First}});auto locked=moveSketchPoint(onLine,{"point",PointRole::First},{10,20},SketchPointRef{"guide",PointRole::Midpoint});near(locked.entities[1].first.x,10);near(locked.entities[1].first.y,20);require(locked.relations.size()==3,"Midpoint locking retains an existing Point on Line relation");
    auto released=removeSketchRelation(resized,"point-mid");require(released.entities.size()==resized.entities.size() && released.relations.size()==1,"Deleting a midpoint relation keeps its geometry");
    QTemporaryDir directory;const auto path=directory.filePath("midpoint.thepart");auto doc=thecad::document::PartDocument::createEmpty();doc.addSketch(resized);doc.save(path);auto restored=thecad::document::PartDocument::load(path).features()[0].sketch;validateSketchRelations(restored);require(restored.relations[0].id=="point-mid" && restored.relations[0].kind==RelationKind::Midpoint,"Persist midpoint identities and mathematics");
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Read midpoint fixture");const auto good=QJsonDocument::fromJson(file.readAll()).object();file.close();
    for(int mutation=0;mutation<3;++mutation){auto root=good;auto features=root["features"].toArray();auto feature=features[0].toObject();auto relations=feature["relations"].toArray();auto relation=relations[0].toObject();if(mutation==0)root["version"]=10;if(mutation==1)relation["second"]=QJsonObject{{"entity","point"},{"point","first"}};if(mutation==2)relation["second"]=QJsonObject{{"entity","guide"},{"point","midpoint"}};relations[0]=relation;feature["relations"]=relations;features[0]=feature;root["features"]=features;
        require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write malformed midpoint fixture");file.write(QJsonDocument(root).toJson());file.close();bool rejected=false;try{thecad::document::PartDocument::load(path);}catch(const std::exception&){rejected=true;}require(rejected,"Reject invalid or disguised midpoint constraints");
    }
    std::cout<<"PASS: all three free rectangle corners, anchored axes, driving dimensions, persistent point/line midpoints, dragging, deletion, round-trip and schema validation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
