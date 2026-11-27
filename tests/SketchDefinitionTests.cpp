#include "modeling/ConstraintSolver.h"
#include "modeling/RelationCommands.h"
#include "modeling/SketchTools.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool curve(const SketchDefinition& state,const std::string& id){return std::find(state.definedCurves.begin(),state.definedCurves.end(),id)!=state.definedCurves.end();}
bool point(const SketchDefinition& state,const std::string& id,PointRole role){return std::any_of(state.fixedPoints.begin(),state.fixedPoints.end(),[&](const auto& p){return p.entityId==id && p.role==role;});}
SketchDefinition analyze(const Sketch& sketch){auto state=analyzeSketchDefinition(sketch);require(state.valid,"Definition diagnosis must succeed");auto solved=solvePlanarSketch(sketch);require(solved.success,"Test sketch must remain solvable");require(state.degreesOfFreedom==solved.diagnostics.degreesOfFreedom,"Observers must preserve actual sketch DOFs");return state;}
SketchDimension dimension(std::string id,DimensionKind kind,SketchPointRef first,SketchPointRef second,double value){SketchDimension d;d.id=id;d.kind=kind;d.first=first;d.second=second;d.value=value;return d;}
}
int main(){try{
    Sketch sketch;sketch.entities={{EntityKind::Line,{10,20},{50,20},1,"line"}};
    require(!curve(analyze(sketch),"line"),"Unconstrained direction must stay blue even when numerically horizontal");
    sketch.relations={{"horizontal",RelationKind::Horizontal,{"line",PointRole::First},{"line",PointRole::Second}}};
    auto state=analyze(sketch);require(!curve(state,"line"),"Horizontal alone must leave the line blue");require(!point(state,"line",PointRole::First)&&!point(state,"line",PointRole::Second),"Horizontal endpoints must stay movable");
    const auto moved=moveSketchPoint(sketch,{"line",PointRole::Second},{70,35});require(std::abs(moved.entities[0].second.x-50)>1,"Underdefined line must allow endpoint dragging");require(std::abs(moved.entities[0].first.y-moved.entities[0].second.y)<1e-6,"Dragging must retain horizontal relation");require(sketch.entities[0].second.x==50&&sketch.relations.size()==1,"Diagnosis and dragging must not mutate source");
    sketch.relations.push_back({"fixed",RelationKind::FixedPoint,{"line",PointRole::First},{},{10,20}});
    state=analyze(sketch);require(!curve(state,"line")&&point(state,"line",PointRole::First)&&!point(state,"line",PointRole::Second),"Anchoring one endpoint must leave undimensioned line blue");
    sketch.dimensions={dimension("length",DimensionKind::LineLength,{"line",PointRole::First},{"line",PointRole::Second},40)};
    sketch.entities.push_back({EntityKind::Point,{-20,-30},{},1,"free"});state=analyze(sketch);require(state.degreesOfFreedom==2&&curve(state,"line")&&point(state,"line",PointRole::Second)&&!point(state,"free",PointRole::First),"Fully defined line must remain black in an underdefined sketch");
    auto released=removeSketchDimension(sketch,"length");require(!curve(analyze(released),"line"),"Removing length must restore blue");
    released=removeSketchRelation(sketch,"fixed");require(!curve(analyze(released),"line"),"Defined length and direction with free translation must remain blue");
    auto free=sketch;free.relations.clear();free.dimensions.clear();free.dimensions={dimension("length",DimensionKind::LineLength,{"line",PointRole::First},{"line",PointRole::Second},40)};require(!curve(analyze(free),"line"),"Length alone must leave direction free");
    Sketch coupled;coupled.entities={{EntityKind::Line,{0,0},{20,0},1,"a"},{EntityKind::Line,{0,10},{30,10},1,"b"}};coupled.relations={{"parallel",RelationKind::Parallel,{"a",PointRole::First},{"b",PointRole::First}}};state=analyze(coupled);require(!curve(state,"a")&&!curve(state,"b"),"Parallel lines can rotate together");
    coupled.relations.push_back({"axis",RelationKind::Horizontal,{"a",PointRole::First},{"a",PointRole::Second}});state=analyze(coupled);require(!curve(state,"a")&&!curve(state,"b"),"Coupled fixed directions must not define size or position");require(!point(state,"b",PointRole::First),"Parallel reference must not anchor endpoints");
    coupled.entities[1].second={0,40};coupled.relations[0].kind=RelationKind::Perpendicular;require(!curve(analyze(coupled),"b"),"Perpendicular alone must leave the line blue");
    Sketch circle;circle.entities={{EntityKind::Circle,{10,20},{},5,"circle"}};state=analyze(circle);require(!curve(state,"circle")&&!point(state,"circle",PointRole::Center),"Free circle and center must remain blue");
    circle.dimensions={dimension("radius",DimensionKind::CircleRadius,{"circle",PointRole::Center},{},5)};state=analyze(circle);require(!curve(state,"circle")&&!point(state,"circle",PointRole::Center),"Driven size with movable center must remain blue");
    circle.entities.push_back({EntityKind::Circle,{30,40},{},5,"equal"});circle=addSelectionRelations(circle,RelationKind::Equal,{"circle","equal"},"equal");state=analyze(circle);require(!curve(state,"equal")&&!point(state,"equal",PointRole::Center),"Equal size must not define center position");
    circle.dimensions[0].driving=false;state=analyze(circle);require(!curve(state,"circle")&&!curve(state,"equal"),"Reference dimension must not define size");
    circle.relations.push_back({"center",RelationKind::FixedPoint,{"circle",PointRole::Center},{},{10,20}});state=analyze(circle);require(point(state,"circle",PointRole::Center)&&!curve(state,"circle"),"Anchored center must not define free radius");
    circle.dimensions[0].driving=true;state=analyze(circle);require(curve(state,"circle")&&!curve(state,"equal"),"Only circle with both defined size and center must turn black");
    Sketch points;points.entities={{EntityKind::Point,{3,4},{},1,"p"}};require(!point(analyze(points),"p",PointRole::First),"A points-only sketch must retain both freedoms");points.relations={{"fixed",RelationKind::FixedPoint,{"p",PointRole::First},{},{3,4}}};require(point(analyze(points),"p",PointRole::First),"Fixed standalone point must be black");
    for(const bool construction:{false,true}){
        Sketch mixed;mixed.entities={{EntityKind::Line,{-20,-10},{-5,15},1,"line",construction},{EntityKind::Circle,{20,20},{},8,"circle",construction},{EntityKind::Point,{40,-20},{},1,"point",construction},{EntityKind::Point,{60,30},{},1,"free"}};
        mixed=addSelectionRelations(mixed,RelationKind::FixedEntity,{"line","circle","point"},"fix");state=analyze(mixed);
        require(state.degreesOfFreedom==2&&curve(state,"line")&&curve(state,"circle"),"All defined ordinary and construction curves must be black in mixed sketches");
        require(point(state,"line",PointRole::First)&&point(state,"line",PointRole::Second)&&point(state,"circle",PointRole::Center)&&point(state,"point",PointRole::First)&&!point(state,"free",PointRole::First),"All defined endpoints, centers and standalone points must be black");
    }
    auto polygon=addRegularPolygon({},"hexagon",{{15,25},12,0.25,6});
    polygon=addSelectionRelations(polygon,RelationKind::FixedEntity,{polygon.polygons[0].edges[0]},"anchor-edge");state=analyze(polygon);
    require(state.degreesOfFreedom==0,"Anchored regular polygon must be defined");
    for(const auto& edge:polygon.polygons[0].edges)require(curve(state,edge)&&point(state,edge,PointRole::First)&&point(state,edge,PointRole::Second),"All regular polygon edges and vertices must inherit definition");
    Sketch diagonal;diagonal.entities={{EntityKind::Line,{0,0},{30,40},1,"diagonal"}};diagonal.dimensions={dimension("dx",DimensionKind::HorizontalDistance,{"diagonal",PointRole::First},{"diagonal",PointRole::Second},30),dimension("dy",DimensionKind::VerticalDistance,{"diagonal",PointRole::First},{"diagonal",PointRole::Second},40)};state=analyze(diagonal);require(!curve(state,"diagonal")&&!point(state,"diagonal",PointRole::First),"Dimensions defining size and direction must leave free translation blue");
    diagonal.relations={{"anchor",RelationKind::FixedPoint,{"diagonal",PointRole::First},{},{0,0}}};require(curve(analyze(diagonal),"diagonal"),"Anchored dimensioned diagonal must turn black");
    Sketch rectangle;rectangle.entities={{EntityKind::Rectangle,{0,0},{30,40},1,"rectangle"}};require(!curve(analyze(rectangle),"rectangle"),"Intrinsic rectangle axes must not imply full definition");
    require(analyze({}).degreesOfFreedom==0,"Empty sketch diagnosis");
    auto invalid=sketch;invalid.relations[0].first.entityId="missing";require(!analyzeSketchDefinition(invalid).valid,"Invalid references must not appear fully defined");
    invalid=sketch;invalid.space=SketchSpace::Spatial3D;require(!analyzeSketchDefinition(invalid).valid,"Unsupported sketch space must not inherit planar definition");
    std::cout<<"PASS: full entity mobility, partial constraints, independent endpoints, circle size/center, reference dimensions and unchanged solver freedoms\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
