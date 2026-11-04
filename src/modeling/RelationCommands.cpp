#include "modeling/RelationCommands.h"
#include "modeling/ConstraintSolver.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace thecad::modeling {
namespace {
const SketchEntity& entity(const Sketch& s,const std::string& id){const auto e=std::find_if(s.entities.begin(),s.entities.end(),[&](const auto& v){return v.id==id;});if(e==s.entities.end())throw std::runtime_error("Selected geometry is unavailable.");return *e;}
SketchPointRef ref(const SketchEntity& e){return {e.id,e.kind==EntityKind::Circle?PointRole::Center:PointRole::First};}
double length(const SketchEntity& e){return e.kind==EntityKind::Circle?6.28318530717958647692*e.radius:std::hypot(e.second.x-e.first.x,e.second.y-e.first.y);}
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
}
const char* relationName(RelationKind k){switch(k){
case RelationKind::Coincident:return "Coincident";case RelationKind::Horizontal:return "Horizontal";case RelationKind::Vertical:return "Vertical";
case RelationKind::FixedPoint:return "Fix Point";case RelationKind::Parallel:return "Parallel";case RelationKind::Perpendicular:return "Perpendicular";case RelationKind::Tangent:return "Tangent";
case RelationKind::PointOnLine:return "Point on Line";case RelationKind::PointOnCircle:return "Point on Circle";
case RelationKind::Collinear:return "Collinear";case RelationKind::Equal:return "Equal";case RelationKind::Concentric:return "Concentric";case RelationKind::EqualCurveLength:return "Equal Curve Length";case RelationKind::FixedEntity:return "Fix";
}return "Unsupported relation";}
std::vector<RelationKind> availableRelations(const Sketch& s,const std::vector<std::string>& ids){
    if(ids.empty() || std::set<std::string>(ids.begin(),ids.end()).size()!=ids.size())return {};
    std::vector<const SketchEntity*> selected;for(const auto& id:ids){auto e=std::find_if(s.entities.begin(),s.entities.end(),[&](const auto& v){return v.id==id;});if(e==s.entities.end())return {};selected.push_back(&*e);}
    const auto all=[&](EntityKind kind){return std::all_of(selected.begin(),selected.end(),[&](const auto* e){return e->kind==kind;});};
    const bool lines=all(EntityKind::Line),circles=all(EntityKind::Circle),points=all(EntityKind::Point);std::vector<RelationKind> result;
    if(lines || (points && ids.size()>=2)){result.push_back(RelationKind::Horizontal);result.push_back(RelationKind::Vertical);}
    if(lines && ids.size()>=2){result.push_back(RelationKind::Collinear);if(ids.size()==2)result.push_back(RelationKind::Perpendicular);result.push_back(RelationKind::Parallel);result.push_back(RelationKind::Equal);}
    if(points && ids.size()>=2)result.push_back(RelationKind::Coincident);
    if(circles && ids.size()>=2){result.push_back(RelationKind::Concentric);result.push_back(RelationKind::Equal);result.push_back(RelationKind::EqualCurveLength);}
    if(ids.size()==2){
        const auto a=selected[0]->kind,b=selected[1]->kind;
        if((a==EntityKind::Line && b==EntityKind::Circle)||(b==EntityKind::Line && a==EntityKind::Circle)){result.push_back(RelationKind::Tangent);result.push_back(RelationKind::EqualCurveLength);}
        if(circles)result.push_back(RelationKind::Tangent);
        if((a==EntityKind::Point && b==EntityKind::Circle)||(b==EntityKind::Point && a==EntityKind::Circle)){result.push_back(RelationKind::Concentric);result.push_back(RelationKind::PointOnCircle);}
        if((a==EntityKind::Point && b==EntityKind::Line)||(b==EntityKind::Point && a==EntityKind::Line))result.push_back(RelationKind::PointOnLine);
    }
    if(lines || circles || points || std::all_of(selected.begin(),selected.end(),[](const auto* e){return e->kind==EntityKind::Line || e->kind==EntityKind::Circle || e->kind==EntityKind::Point;}))result.push_back(RelationKind::FixedEntity);
    return result;
}
Sketch addSelectionRelations(const Sketch& s,RelationKind kind,const std::vector<std::string>& ids,const std::string& prefix){
    const auto allowed=availableRelations(s,ids);require(std::find(allowed.begin(),allowed.end(),kind)!=allowed.end(),"This relation does not apply to the current selection.");
    auto candidate=s;int n=0;
    const auto add=[&](SketchPointRef a,SketchPointRef b,const SketchEntity* frozen=nullptr){SketchRelation r;r.id=prefix+"-"+std::to_string(++n);r.kind=kind;r.first=a;r.second=b;if(frozen){r.fixedPosition=frozen->first;r.fixedSecond=frozen->second;r.fixedRadius=frozen->radius;}candidate.relations.push_back(r);};
    if(kind==RelationKind::FixedEntity){for(const auto& id:ids){const auto& e=entity(s,id);add(ref(e),{},&e);}}
    else if((kind==RelationKind::Horizontal || kind==RelationKind::Vertical) && entity(s,ids.front()).kind==EntityKind::Line){for(const auto& id:ids)add({id,PointRole::First},{id,PointRole::Second});}
    else if(kind==RelationKind::PointOnLine || kind==RelationKind::PointOnCircle){const auto& a=entity(s,ids[0]);const auto& b=entity(s,ids[1]);add(ref(a.kind==EntityKind::Point?a:b),ref(a.kind==EntityKind::Point?b:a));}
    else for(std::size_t i=1;i<ids.size();++i)add(ref(entity(s,ids[0])),ref(entity(s,ids[i])));
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
bool isNewRelation(RelationKind kind){return int(kind)>=int(RelationKind::Collinear);}
void validateNewRelation(const Sketch& s,const SketchRelation& r,bool geometry){
    const auto& a=entity(s,r.first.entityId);
    if(r.kind==RelationKind::FixedEntity){
        require(a.kind==EntityKind::Line || a.kind==EntityKind::Circle || a.kind==EntityKind::Point,"Fix supports lines, circles and points.");
        require(r.first.role==ref(a).role && r.second.role==PointRole::Origin && r.second.entityId.empty(),"Invalid fixed-entity references.");
        for(const double value:{r.fixedPosition.x,r.fixedPosition.y,r.fixedSecond.x,r.fixedSecond.y,r.fixedRadius})require(std::isfinite(value)&&std::abs(value)<=1e9,"Invalid fixed-entity snapshot.");
        if(a.kind==EntityKind::Circle)require(r.fixedRadius>1e-7,"Invalid fixed radius.");
        if(geometry){require(a.first.x==r.fixedPosition.x && a.first.y==r.fixedPosition.y,"Fixed entity moved.");if(a.kind==EntityKind::Line)require(a.second.x==r.fixedSecond.x && a.second.y==r.fixedSecond.y,"Fixed line endpoint moved.");if(a.kind==EntityKind::Circle)require(std::abs(a.radius-r.fixedRadius)<1e-6,"Fixed circle changed size.");}return;
    }
    const auto& b=entity(s,r.second.entityId);const auto allowed=availableRelations(s,{a.id,b.id});require(std::find(allowed.begin(),allowed.end(),r.kind)!=allowed.end(),"Invalid relation entity types.");
    require(r.first.role==ref(a).role && r.second.role==ref(b).role,"Invalid relation reference roles.");if(!geometry)return;
    if(r.kind==RelationKind::Equal){const double x=a.kind==EntityKind::Circle?a.radius:length(a),y=b.kind==EntityKind::Circle?b.radius:length(b);require(std::abs(x-y)<1e-6,"Equal relation is not satisfied.");}
    else if(r.kind==RelationKind::EqualCurveLength)require(std::abs(length(a)-length(b))<1e-6,"Equal curve lengths are not satisfied.");
    else if(r.kind==RelationKind::Concentric)require(a.first.x==b.first.x && a.first.y==b.first.y,"Concentric centers are not coincident.");
    else if(r.kind==RelationKind::Collinear){const double ux=a.second.x-a.first.x,uy=a.second.y-a.first.y,vx=b.second.x-b.first.x,vy=b.second.y-b.first.y,al=length(a),bl=length(b);require(al>1e-7&&bl>1e-7&&std::abs(ux*vy-uy*vx)<=1e-7*al*bl && std::abs(ux*(b.first.y-a.first.y)-uy*(b.first.x-a.first.x))/al<1e-6,"Collinear relation is not satisfied.");}
}
}
