#include "modeling/RelationCommands.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/DimensionInference.h"
#include "document/PartDocument.h"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double a,double b){if(std::abs(a-b)>1e-6)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
double length(const SketchEntity& e){return std::hypot(e.second.x-e.first.x,e.second.y-e.first.y);}
Sketch lines(){Sketch s;s.entities={{EntityKind::Line,{0,0},{20,0},1,"a"},{EntityKind::Line,{0,10},{15,20},1,"b"},{EntityKind::Line,{-20,-10},{-5,5},1,"c"}};return s;}
int main(){try{
    auto s=lines();auto available=availableRelations(s,{"a","b","c"});require(std::find(available.begin(),available.end(),RelationKind::Perpendicular)==available.end(),"Perpendicular requires exactly two lines");
    for(const auto kind:{RelationKind::Parallel,RelationKind::Perpendicular,RelationKind::Equal,RelationKind::Collinear}){
        auto constrained=addSelectionRelations(s,kind,{"a","b"},"relation");validateSketchRelations(constrained);
        auto d=inferDimension(constrained,{}, {"a"},{30,30});d.id="length";d.kind=DimensionKind::LineLength;d.value=30;d.first={"a",PointRole::First};d.second={"a",PointRole::Second};
        constrained=setSketchDimension(constrained,d);validateSketchRelations(constrained);near(length(constrained.entities[0]),30);
        if(kind==RelationKind::Equal)near(length(constrained.entities[1]),30);
        auto dragged=moveSketchPoint(constrained,{"b",PointRole::Second},{28,24});validateSketchRelations(dragged);near(length(dragged.entities[0]),30);
    }
    auto equal=addSelectionRelations(s,RelationKind::Equal,{"a","b","c"},"equal");require(equal.relations.size()==2,"Multi-entity relation batch");near(length(equal.entities[0]),length(equal.entities[2]));
    Sketch horizontal;horizontal.entities={s.entities[0],{EntityKind::Line,{30,10},{50,10},1,"b"}};horizontal.relations={{"h-a",RelationKind::Horizontal,{"a",PointRole::First},{"a",PointRole::Second}},{"h-b",RelationKind::Horizontal,{"b",PointRole::First},{"b",PointRole::Second}},{"origin",RelationKind::Coincident,{"a",PointRole::First},{}}};
    auto collinear=addSelectionRelations(horizontal,RelationKind::Collinear,{"a","b"},"collinear");near(collinear.entities[1].first.y,0);require(!areSketchPointsCoincident(collinear,{"a",PointRole::First},{"b",PointRole::First}),"Collinearity must not merge endpoints");
    auto fixed=addSelectionRelations(horizontal,RelationKind::FixedEntity,{"a"},"fix");validateSketchRelations(fixed);near(moveSketchPoint(fixed,{"a",PointRole::Second},{50,20}).entities[0].second.x,20);
    Sketch circles;circles.entities={{EntityKind::Circle,{0,0},{},5,"a"},{EntityKind::Circle,{25,15},{},9,"b"}};
    auto concentric=addSelectionRelations(circles,RelationKind::Concentric,{"a","b"},"center");near(concentric.entities[0].first.x,concentric.entities[1].first.x);near(concentric.entities[0].radius,5);near(concentric.entities[1].radius,9);
    auto moved=moveSketchPoint(concentric,{"a",PointRole::Center},{10,30});near(moved.entities[0].first.y,moved.entities[1].first.y);
    auto radii=addSelectionRelations(circles,RelationKind::Equal,{"a","b"},"radii");auto diameter=inferDimension(radii,{}, {"a"},{20,20});diameter.id="diameter";diameter.value=20;radii=setSketchDimension(radii,diameter);near(radii.entities[1].radius,10);
    Sketch mixed;mixed.entities={{EntityKind::Line,{0,0},{20,0},1,"line"},{EntityKind::Circle,{30,20},{},3,"circle"}};
    auto curves=addSelectionRelations(mixed,RelationKind::EqualCurveLength,{"line","circle"},"curve-length");auto radius=inferDimension(curves,{}, {"circle"},{50,30},true);radius.id="radius";radius.value=5;curves=setSketchDimension(curves,radius);near(length(curves.entities[0]),2*3.14159265358979323846*5);near(curves.entities[1].radius,5);
    Sketch all;all.entities={mixed.entities[0],mixed.entities[1],{EntityKind::Point,{40,50},{},1,"point"}};auto freeze=addSelectionRelations(all,RelationKind::FixedEntity,{"line","circle","point"},"freeze");require(solvePlanarSketch(freeze).diagnostics.degreesOfFreedom==0,"Fix anchors whole geometry");
    auto replacement=freeze.entities[1];replacement.radius=12;bool rejected=false;try{editSketchEntity(freeze,replacement);}catch(const std::exception&){rejected=true;}require(rejected,"Fix rejects resizing without changing old geometry");near(freeze.entities[1].radius,3);
    Sketch points;points.entities={{EntityKind::Point,{0,0},{},1,"a"},{EntityKind::Point,{15,20},{},1,"b"}};auto aligned=addSelectionRelations(points,RelationKind::Horizontal,{"a","b"},"align");near(aligned.entities[0].first.y,aligned.entities[1].first.y);
    QTemporaryDir directory;
    const auto equalityGroups=relationGroups(equal);require(equalityGroups.size()==1 && equalityGroups[0].entityIds.size()==3 && equalityGroups[0].relationIds.size()==2,"One Equal command owns all three members");
    require(equal.relations[0].groupId=="equal" && equal.relations[1].groupId=="equal","Stable Equal group ID");
    require(removeSketchRelation(equal,equal.relations[1].id).relations.empty(),"Deleting any group member releases the complete equality group");
    auto resizedGroup=radii.relations[0];resizedGroup.kind=RelationKind::EqualCurveLength;auto changedGroup=setSketchRelation(radii,resizedGroup);require(changedGroup.relations[0].groupId==radii.relations[0].groupId,"Changing a compatible equality preserves its group");
    auto translated=moveSketchPoint(circles,{"a",PointRole::Center},{12,22},{},true);near(translated.entities[0].first.x,12);near(translated.entities[0].first.y,22);near(translated.entities[0].radius,5);
    auto dimensionedMove=moveSketchPoint(radii,{"a",PointRole::Center},{12,22},{},true);near(dimensionedMove.entities[0].radius,10);near(dimensionedMove.entities[1].radius,10);
    auto fixedMove=moveSketchPoint(freeze,{"circle",PointRole::Center},{15,18},{},true);near(fixedMove.entities[1].first.x,30);near(fixedMove.entities[1].radius,3);
    Sketch tangent;tangent.entities={{EntityKind::Line,{-20,0},{20,0},1,"line"},{EntityKind::Circle,{2,3},{},3,"circle"}};tangent=addSelectionRelations(tangent,RelationKind::FixedEntity,{"line"},"fixed-line");tangent=addSelectionRelations(tangent,RelationKind::Tangent,{"line","circle"},"tangent");auto sliding=moveSketchPoint(tangent,{"circle",PointRole::Center},{12,10},{},true);near(sliding.entities[1].first.x,12);near(sliding.entities[1].first.y,3);near(sliding.entities[1].radius,3);
    for(const auto& specimen:{equal,collinear,concentric,radii,curves,freeze,aligned}){
        auto document=thecad::document::PartDocument::createEmpty();document.addSketch(specimen);const auto path=directory.filePath("relations.thepart");document.save(path);
        auto restored=thecad::document::PartDocument::load(path).features()[0].sketch;require(restored.relations.size()==specimen.relations.size(),"Persist every relation");validateSketchRelations(restored);
        for(std::size_t i=0;i<restored.relations.size();++i){require(restored.relations[i].id==specimen.relations[i].id && restored.relations[i].kind==specimen.relations[i].kind && restored.relations[i].groupId==specimen.relations[i].groupId,"Stable relation IDs/types/groups");}
    }
    {
        const std::string batch="01234567-89ab-cdef-0123-456789abcdef";
        auto specimen=addSelectionRelations(s,RelationKind::Equal,{"a","b","c"},batch);auto doc=thecad::document::PartDocument::createEmpty();doc.addSketch(specimen);const auto legacyPath=directory.filePath("legacy-equal.thepart");doc.save(legacyPath);
        QFile legacyFile(legacyPath);require(legacyFile.open(QIODevice::ReadOnly),"Read grouped fixture");auto root=QJsonDocument::fromJson(legacyFile.readAll()).object();legacyFile.close();auto history=root["features"].toArray();auto feature=history[0].toObject();auto relations=feature["relations"].toArray();
        for(int i=0;i<relations.size();++i){auto r=relations[i].toObject();r.remove("group");relations[i]=r;}feature["relations"]=relations;history[0]=feature;root["features"]=history;root["version"]=9;
        require(legacyFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write legacy grouped fixture");legacyFile.write(QJsonDocument(root).toJson());legacyFile.close();auto restored=thecad::document::PartDocument::load(legacyPath).features()[0].sketch;
        require(relationGroups(restored).size()==1 && restored.relations[0].groupId==batch,"Migrate schema-9 generated batches without losing their identity");
        for(int mutation=0;mutation<3;++mutation){auto bad=root;bad["version"]=10;auto h=history;auto f=feature;auto rs=relations;for(int i=0;i<rs.size();++i){auto r=rs[i].toObject();r["group"]=QString::fromStdString(batch);rs[i]=r;}auto r=rs[1].toObject();if(mutation==0)r["group"]="bad!";if(mutation==1)r["first"]=QJsonObject{{"entity","b"},{"point","first"}};if(mutation==2)r["group"]=2;rs[1]=r;f["relations"]=rs;h[0]=f;bad["features"]=h;
            require(legacyFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write invalid group");legacyFile.write(QJsonDocument(bad).toJson());legacyFile.close();bool failed=false;try{thecad::document::PartDocument::load(legacyPath);}catch(const std::exception&){failed=true;}require(failed,"Reject invalid or inconsistent equality groups");
        }
    }
    auto document=thecad::document::PartDocument::createEmpty();document.addSketch(freeze);const auto path=directory.filePath("schema.thepart");document.save(path);
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Read relation fixture");const auto good=QJsonDocument::fromJson(file.readAll()).object();file.close();
    for(int mutation=0;mutation<5;++mutation){auto root=good;auto history=root["features"].toArray();auto feature=history[0].toObject();auto relations=feature["relations"].toArray();auto relation=relations[0].toObject();
        if(mutation==0)relation.remove("radius");if(mutation==1)relation["secondPosition"]="bad";if(mutation==2)relation["first"]=QJsonObject{{"entity","missing"},{"point","first"}};
        relations[0]=relation;feature["relations"]=relations;history[0]=feature;root["features"]=history;if(mutation==3)root["version"]=8;if(mutation==4)root["version"]=9.5;
        require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write malformed fixture");file.write(QJsonDocument(root).toJson());file.close();bool failed=false;try{thecad::document::PartDocument::load(path);}catch(const std::exception&){failed=true;}require(failed,"Malformed or disguised relations reject");
    }
    std::cout<<"PASS: relation applicability, multi-entity batches, persistent line/circle relations, exact circumference, Fix, driving dimensions and constrained dragging\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
