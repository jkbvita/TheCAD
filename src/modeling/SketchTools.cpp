#include <cctype>
#include "modeling/SketchTools.h"
#include "modeling/ConstraintSolver.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace thecad::modeling {
ConstructionState constructionState(const Sketch& sketch,const std::vector<std::string>& ids){
    if(ids.empty())return ConstructionState::Unavailable;bool normal=false,construction=false;
    for(const auto& id:ids){const auto e=std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==id;});
        if(e==sketch.entities.end() || (e->kind!=EntityKind::Line&&!isCircular(e->kind)&&e->kind!=EntityKind::Rectangle))return ConstructionState::Unavailable;
        normal|=!e->construction;construction|=e->construction;
    }return normal&&construction?ConstructionState::Mixed:construction?ConstructionState::Construction:ConstructionState::Normal;
}
Sketch setSketchConstruction(const Sketch& sketch,const std::vector<std::string>& ids,bool construction){
    if(constructionState(sketch,ids)==ConstructionState::Unavailable)throw std::runtime_error("Select sketch curves to change their construction state.");
    auto candidate=sketch;for(auto& e:candidate.entities)if(std::find(ids.begin(),ids.end(),e.id)!=ids.end())e.construction=construction;
    validateSketchConstraintReferences(candidate);return candidate;
}
namespace {
constexpr double pi=3.14159265358979323846;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const SketchEntity& entity(const Sketch& s,const std::string& id){
    auto e=std::find_if(s.entities.begin(),s.entities.end(),[&](const auto& v){return v.id==id;});require(e!=s.entities.end(),"Polygon reference is missing.");return *e;
}
void valid(PolygonParameters p){require(p.sides>=3 && p.sides<=999,"A polygon requires 3 to 999 sides, subject to the sketch entity limit.");require(std::isfinite(p.radius) && p.radius>1e-6 && p.radius<=1e9 && std::isfinite(p.angle) && std::abs(p.angle)<=1e9 && std::isfinite(p.center.x) && std::isfinite(p.center.y),"Invalid polygon center, circumradius or angle.");}
void place(Sketch& s,const Sketch::RegularPolygon& group,PolygonParameters p){
    const auto vertex=[&](int i){const double a=p.angle+2*pi*i/p.sides;return Point2{p.center.x+p.radius*std::cos(a),p.center.y+p.radius*std::sin(a)};};
    for(auto& e:s.entities){if(e.id==group.centerId)e.first=p.center;const auto it=std::find(group.edges.begin(),group.edges.end(),e.id);if(it!=group.edges.end()){const int i=int(it-group.edges.begin());e.first=vertex(i);e.second=vertex((i+1)%p.sides);}}
}
}
const Sketch::RegularPolygon* polygonForEntity(const Sketch& s,const std::string& id){for(const auto& g:s.polygons)if(g.centerId==id || std::find(g.edges.begin(),g.edges.end(),id)!=g.edges.end())return &g;return nullptr;}
PolygonParameters polygonParameters(const Sketch& s,const Sketch::RegularPolygon& g){const auto c=entity(s,g.centerId).first,v=entity(s,g.edges.front()).first;return {c,std::hypot(v.x-c.x,v.y-c.y),std::atan2(v.y-c.y,v.x-c.x),int(g.edges.size())};}
void validatePolygons(const Sketch& s,bool geometry){
    std::set<std::string> groups,members;
    for(const auto& g:s.polygons){
        require(!g.id.empty() && g.id.size()<=110 && std::all_of(g.id.begin(),g.id.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';}) && groups.insert(g.id).second,"Polygon IDs must be valid and unique.");
        require(g.edges.size()>=3 && g.edges.size()<=999,"Invalid polygon side count.");
        require(entity(s,g.centerId).kind==EntityKind::Point && members.insert(g.centerId).second,"Polygon center must reference its own sketch point.");
        for(const auto& id:g.edges)require(entity(s,id).kind==EntityKind::Line && members.insert(id).second,"Polygon edges must be distinct existing lines.");
        for(std::size_t i=0;i<g.edges.size();++i){const auto& a=g.edges[i];const auto& b=g.edges[(i+1)%g.edges.size()];
            require(std::any_of(s.relations.begin(),s.relations.end(),[&](const auto& r){return r.kind==RelationKind::Coincident &&
                ((r.first.entityId==a && r.first.role==PointRole::Second && r.second.entityId==b && r.second.role==PointRole::First) ||
                 (r.second.entityId==a && r.second.role==PointRole::Second && r.first.entityId==b && r.first.role==PointRole::First));}),"Polygon boundary connections are structural; delete a member to dissolve the group.");}
        if(!geometry)continue;
        const auto p=polygonParameters(s,g);valid(p);const double tolerance=1e-6*std::max(1.0,p.radius);
        for(std::size_t i=0;i<g.edges.size();++i){const auto& e=entity(s,g.edges[i]);const double a=p.angle+2*pi*i/g.edges.size();const Point2 expected{p.center.x+p.radius*std::cos(a),p.center.y+p.radius*std::sin(a)};
            require(std::hypot(e.first.x-expected.x,e.first.y-expected.y)<tolerance,"Polygon lost its regular shape or winding.");const auto& next=entity(s,g.edges[(i+1)%g.edges.size()]);require(e.second.x==next.first.x && e.second.y==next.first.y,"Polygon boundary is disconnected.");}
    }
}
Sketch addRegularPolygon(const Sketch& s,const std::string& id,PolygonParameters p){
    valid(p);auto result=s;Sketch::RegularPolygon group{id,id+"-center",{}};
    require(std::none_of(s.polygons.begin(),s.polygons.end(),[&](const auto& g){return g.id==id;}),"Polygon ID already exists.");
    result.entities.push_back({EntityKind::Point,p.center,{},1,group.centerId,true});
    for(int i=0;i<p.sides;++i){const auto edge=id+"-edge-"+std::to_string(i);group.edges.push_back(edge);result.entities.push_back({EntityKind::Line,{},{},1,edge});}
    place(result,group,p);
    for(int i=0;i<p.sides;++i)result.relations.push_back({id+"-join-"+std::to_string(i),RelationKind::Coincident,{group.edges[i],PointRole::Second},{group.edges[(i+1)%p.sides],PointRole::First}});
    result.polygons.push_back(group);(void)sketchShape(result);return result;
}
Sketch editRegularPolygon(const Sketch& s,const std::string& id,PolygonParameters p){
    valid(p);auto result=s;auto it=std::find_if(result.polygons.begin(),result.polygons.end(),[&](const auto& g){return g.id==id;});require(it!=result.polygons.end(),"Polygon is unavailable.");
    const auto old=*it;auto group=old;
    if(p.sides!=int(old.edges.size())){
        for(const auto& edge:old.edges)for(const auto& d:s.dimensions)require(d.first.entityId!=edge && d.second.entityId!=edge,"Remove edge dimensions before changing polygon side count.");
        for(const auto& r:s.relations)if(r.id.rfind(id+"-join-",0)!=0)for(const auto& edge:old.edges)require(r.first.entityId!=edge && r.second.entityId!=edge,"Remove external edge relations before changing polygon side count.");
        result.relations.erase(std::remove_if(result.relations.begin(),result.relations.end(),[&](const auto& r){return r.id.rfind(id+"-join-",0)==0;}),result.relations.end());
        while(int(group.edges.size())>p.sides){const auto removed=group.edges.back();group.edges.pop_back();result.entities.erase(std::remove_if(result.entities.begin(),result.entities.end(),[&](const auto& e){return e.id==removed;}),result.entities.end());}
        while(int(group.edges.size())<p.sides){const auto edge=id+"-edge-"+std::to_string(group.edges.size());require(std::none_of(result.entities.begin(),result.entities.end(),[&](const auto& e){return e.id==edge;}),"A polygon edge ID is already in use.");group.edges.push_back(edge);result.entities.push_back({EntityKind::Line,{},{},1,edge});}
        for(int i=0;i<p.sides;++i)result.relations.push_back({id+"-join-"+std::to_string(i),RelationKind::Coincident,{group.edges[i],PointRole::Second},{group.edges[(i+1)%p.sides],PointRole::First}});
        *it=group;
    }
    place(result,group,p); // Warm-start the intended regular solution.
    auto solved=requireSolvedSketch(solvePlanarSketch(result));const auto actual=polygonParameters(solved,*std::find_if(solved.polygons.begin(),solved.polygons.end(),[&](const auto& g){return g.id==id;}));
    require(std::hypot(actual.center.x-p.center.x,actual.center.y-p.center.y)<1e-6 && std::abs(actual.radius-p.radius)<1e-6 && std::abs(std::remainder(actual.angle-p.angle,2*pi))<1e-6,"Polygon parameters conflict with existing constraints; the previous polygon is retained.");
    return solved;
}
}
