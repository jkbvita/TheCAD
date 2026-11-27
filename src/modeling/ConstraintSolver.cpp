#include "modeling/ConstraintSolver.h"
#include "modeling/SketchModifiers.h"
#include "modeling/SketchOffset.h"
#include "GCS.h"
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <functional>
namespace thecad::modeling {
namespace {
constexpr double coordinateLimit=1e9;
void finiteValue(double value,const char* label){if(!std::isfinite(value)||std::abs(value)>coordinateLimit)throw std::runtime_error(std::string(label)+" must be finite and within the prototype limit.");}
// A mirror's point pair can coincide on its axis. Its normal equation is
// (source-target) dot (axis-end-axis-start) = 0, even at zero separation.
// Normalize only by the axis length, keeping the equation and Jacobian finite.
class MirrorNormal final : public GCS::Constraint {
public:
    MirrorNormal(GCS::Point& source,GCS::Point& target,GCS::Line& axis) {
        pvec={source.x,source.y,target.x,target.y,axis.p1.x,axis.p1.y,axis.p2.x,axis.p2.y};
        origpvec=pvec;rescale();
    }
    GCS::ConstraintType getTypeId() override{return GCS::Perpendicular;}
    void rescale(double coefficient=1) override {
        scale=coefficient/std::hypot(*pvec[6]-*pvec[4],*pvec[7]-*pvec[5]);
    }
    double error() override {
        return scale*((*pvec[0]-*pvec[2])*(*pvec[6]-*pvec[4])+(*pvec[1]-*pvec[3])*(*pvec[7]-*pvec[5]));
    }
    double grad(double* parameter) override {
        const double dx=*pvec[6]-*pvec[4],dy=*pvec[7]-*pvec[5],x=*pvec[0]-*pvec[2],y=*pvec[1]-*pvec[3];
        const double derivatives[]={dx,dy,-dx,-dy,-x,-y,x,y};double result=0;
        for(std::size_t i=0;i<pvec.size();++i)if(parameter==pvec[i])result+=derivatives[i];
        return scale*result;
    }
};
class WorkingSketch {
public:
    Sketch sketch;
    double originX=0,originY=0;
    std::map<std::pair<std::string,int>,GCS::Point> points;
    std::map<std::string,GCS::Line> lines;
    std::map<std::string,GCS::Circle> circles;
    GCS::VEC_pD unknowns;
    std::deque<double> constants;
    std::deque<double> auxiliaries;
    std::map<int,std::string> tags;
    int nextTag=1;
    explicit WorkingSketch(const Sketch& input):sketch(input) {
        points.emplace(std::make_pair(std::string{},int(PointRole::Origin)),GCS::Point(&originX,&originY));
        for(auto& entity:sketch.entities) {
            if(entity.id.empty())throw std::runtime_error("Solver entities require stable IDs.");
            const auto firstRole=entity.kind==EntityKind::Circle?PointRole::Center:PointRole::First;
            points.emplace(std::make_pair(entity.id,int(firstRole)),GCS::Point(&entity.first.x,&entity.first.y));
            const bool linked = isSketchProjection(sketch, entity.id);
            if (!linked) {unknowns.push_back(&entity.first.x);unknowns.push_back(&entity.first.y);}
            if (entity.kind == EntityKind::Arc) {
                points.emplace(std::make_pair(entity.id,int(PointRole::Center)),
                               GCS::Point(&entity.center.x, &entity.center.y));
                unknowns.push_back(&entity.center.x);
                unknowns.push_back(&entity.center.y);
            }
            if(isCircular(entity.kind)) {
                if (!linked) unknowns.push_back(&entity.radius);
                GCS::Circle circle;circle.center=point({entity.id,PointRole::Center});circle.rad=&entity.radius;circles.emplace(entity.id,circle);
            }
            if(entity.kind!=EntityKind::Point && entity.kind!=EntityKind::Circle) {
                points.emplace(std::make_pair(entity.id,int(PointRole::Second)),GCS::Point(&entity.second.x,&entity.second.y));
                if (!linked) {unknowns.push_back(&entity.second.x);unknowns.push_back(&entity.second.y);}
                if(entity.kind==EntityKind::Line){GCS::Line line;line.p1=point({entity.id,PointRole::First});line.p2=point({entity.id,PointRole::Second});lines.emplace(entity.id,line);}
            }
            if(entity.kind==EntityKind::Spline)for(std::size_t i=0;i<entity.splinePoints.size();++i) {
                auto& p=entity.splinePoints[i];
                points.emplace(std::make_pair(entity.id,int(splinePointRole(i))),GCS::Point(&p.x,&p.y));
                if(!linked){unknowns.push_back(&p.x);unknowns.push_back(&p.y);}
            }
        }
    }
    GCS::Point& point(const SketchPointRef& ref) {
        const auto found=points.find({ref.entityId,int(ref.role)});if(found==points.end())throw std::runtime_error("Constraint references a missing point.");return found->second;
    }
    double* constant(double value){constants.push_back(value);return &constants.back();}
    int tag(const std::string& id){const int value=nextTag++;tags.emplace(value,id);return value;}
    bool coordinatePinned(const SketchPointRef& reference,bool x,const SketchRelation& current,double expected) const {
        std::set<std::pair<std::string,int>> visited;std::vector<SketchPointRef> queue{reference};visited.insert({reference.entityId,int(reference.role)});
        for(std::size_t i=0;i<queue.size();++i)for(const auto& r:sketch.relations){
            if(r.kind!=RelationKind::Coincident && r.kind!=RelationKind::Concentric && r.kind!=(x?RelationKind::Vertical:RelationKind::Horizontal))continue;
            const auto same=[](const auto& a,const auto& b){return a.entityId==b.entityId && a.role==b.role;};
            std::optional<SketchPointRef> next;if(same(r.first,queue[i]))next=r.second;else if(same(r.second,queue[i]))next=r.first;
            if(next && visited.insert({next->entityId,int(next->role)}).second)queue.push_back(*next);
        }
        const auto matches=[&](const SketchPointRef& ref,double value){return visited.count({ref.entityId,int(ref.role)}) && std::abs(value-expected)<1e-9;};
        if(matches({},0))return true;
        for(const auto& r:sketch.relations)if(r.kind==RelationKind::FixedPoint && matches(r.first,x?r.fixedPosition.x:r.fixedPosition.y))return true;
        for(const auto& r:sketch.relations){
            if(r.id==current.id)break;if(r.kind!=RelationKind::FixedEntity)continue;
            if(matches(r.first,x?r.fixedPosition.x:r.fixedPosition.y))return true;
            const auto e=std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==r.first.entityId;});
            if(e!=sketch.entities.end() && e->kind==EntityKind::Line && matches({e->id,PointRole::Second},x?r.fixedSecond.x:r.fixedSecond.y))return true;
        }
        return false;
    }
    void addFixedEntity(GCS::System& system,const SketchRelation& r,int id){
        const auto fix=[&](SketchPointRef ref,Point2 position){auto& p=point(ref);
            if(!coordinatePinned(ref,true,r,position.x))system.addConstraintCoordinateX(p,constant(position.x),id);
            if(!coordinatePinned(ref,false,r,position.y))system.addConstraintCoordinateY(p,constant(position.y),id);
        };
        fix(r.first,r.fixedPosition);
        const auto& e=*std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==r.first.entityId;});
        if(e.kind==EntityKind::Line)fix({e.id,PointRole::Second},r.fixedSecond);
        if(e.kind==EntityKind::Circle){
            const bool radiusDriven=std::any_of(sketch.dimensions.begin(),sketch.dimensions.end(),[&](const auto& d){return d.driving && d.first.entityId==e.id && (d.kind==DimensionKind::CircleRadius || d.kind==DimensionKind::CircleDiameter);});
            if(!radiusDriven)system.addConstraintCircleRadius(circles.at(e.id),constant(r.fixedRadius),id);
        }
        // A Fix can already be implied by anchors. Keep the persisted snapshot
        // (it becomes driving if those anchors are removed), without a zero-
        // equation diagnostic tag. No existing constraint is removed.
        if(!std::isfinite(system.calculateConstraintErrorByTag(id)))tags.erase(id);
    }
    void addLineOrientation(GCS::System& system,const std::string& first,const std::string& second,bool perpendicular,int id) {
        auto& a=lines.at(first);auto& b=lines.at(second);
        const double ax=*a.p2.x-*a.p1.x,ay=*a.p2.y-*a.p1.y,bx=*b.p2.x-*b.p1.x,by=*b.p2.y-*b.p1.y;
        constexpr double pi=3.14159265358979323846;
        const double angle=perpendicular?(ax*by-ay*bx<0?-pi/2:pi/2):(ax*bx+ay*by<0?pi:0);
        // A normalized angular equation rotates initially parallel/perpendicular
        // lines instead of satisfying a dot/cross product by collapsing a line.
        system.addConstraintL2LAngle(a,b,constant(angle),id);
    }
    bool impliedAxisEquality(const SketchRelation& current,bool x) {
        // Midpoint-to-midpoint construction lines often inherit their direction
        // from the surrounding rectangle. Retain that relation in the sketch,
        // but do not add a duplicate equation to the solver's drag subsystem.
        std::map<double*,double*> parents;
        const auto coordinate=[&](const SketchPointRef& ref){const auto& p=point(ref);return x?p.x:p.y;};
        std::function<double*(double*)> root=[&](double* p)->double*{auto [it,inserted]=parents.emplace(p,p);if(it->second!=p)it->second=root(it->second);return it->second;};
        bool earlier=true;
        for(const auto& r:sketch.relations){if(r.id==current.id){earlier=false;continue;}
            if(r.kind==RelationKind::Coincident||r.kind==RelationKind::Concentric||
               (earlier&&r.kind==(x?RelationKind::Vertical:RelationKind::Horizontal)))
                parents[root(coordinate(r.first))]=root(coordinate(r.second));}
        std::map<double*,std::pair<double*,double*>> midpoints;
        for(const auto& r:sketch.relations)if(r.kind==RelationKind::Midpoint){const auto& line=lines.at(r.second.entityId);midpoints.emplace(root(coordinate(r.first)),std::make_pair(root(x?line.p1.x:line.p1.y),root(x?line.p2.x:line.p2.y)));}
        using Form=std::map<double*,double>;
        std::function<Form(double*,std::set<double*>)> expand=[&](double* p,std::set<double*> visited)->Form{
            p=root(p);const auto midpoint=midpoints.find(p);
            if(midpoint==midpoints.end()||!visited.insert(p).second)return {{p,1}};
            auto a=expand(midpoint->second.first,visited),b=expand(midpoint->second.second,visited);
            for(auto& [key,value]:a)value*=0.5;for(const auto& [key,value]:b)a[key]+=value*0.5;return a;
        };
        if(root(coordinate(current.first))==root(coordinate(current.second)))return false;
        return expand(coordinate(current.first),{})==expand(coordinate(current.second),{});
    }
    void addRelations(GCS::System& system) {
        for (const auto& e : sketch.entities)
            if (e.kind == EntityKind::Arc) {
                const int intrinsic = tag("arc:" + e.id);
                for (const auto role : {PointRole::First, PointRole::Second})
                    system.addConstraintPointOnCircle(point({e.id, role}), circles.at(e.id), intrinsic);
            }
        for(const auto& relation:sketch.relations) {
            if((relation.kind==RelationKind::Horizontal&&impliedAxisEquality(relation,false))||
               (relation.kind==RelationKind::Vertical&&impliedAxisEquality(relation,true)))continue;
            if(relation.kind==RelationKind::PointOnLine&&std::any_of(sketch.relations.begin(),sketch.relations.end(),[&](const auto& r){return r.kind==RelationKind::Midpoint&&r.second.entityId==relation.second.entityId&&areSketchPointsCoincident(sketch,r.first,relation.first);}))continue;
            const int id=tag(relation.id);auto& a=point(relation.first);auto& b=point(relation.second);
            switch(relation.kind) {
            case RelationKind::FixedEntity:addFixedEntity(system,relation,id);break;
            case RelationKind::Concentric:system.addConstraintP2PCoincident(a,b,id);break;
            case RelationKind::Collinear:{
                SketchDimension parallel;parallel.first=relation.first;parallel.second=relation.second;
                if(!parallelImplied(parallel,relation.id))addLineOrientation(system,relation.first.entityId,relation.second.entityId,false,id);
                system.addConstraintPointOnLine(b,lines.at(relation.first.entityId),id);break;
            }
            case RelationKind::Equal:
            case RelationKind::EqualCurveLength:
                if(lines.count(relation.first.entityId) && lines.count(relation.second.entityId))system.addConstraintEqualLength(lines.at(relation.first.entityId),lines.at(relation.second.entityId),id);
                else if(circles.count(relation.first.entityId) && circles.count(relation.second.entityId))system.addConstraintEqualRadius(circles.at(relation.first.entityId),circles.at(relation.second.entityId),id);
                else {
                    auto& line=lines.at(lines.count(relation.first.entityId)?relation.first.entityId:relation.second.entityId);
                    auto& circle=circles.at(circles.count(relation.first.entityId)?relation.first.entityId:relation.second.entityId);
                    auxiliaries.push_back(6.28318530717958647692*(*circle.rad));auto* circumference=&auxiliaries.back();unknowns.push_back(circumference);
                    // Keep the ratio as a linear equation: PlaneGCS's Equal
                    // reduction aliases two unknowns even for a non-unit ratio.
                    GCS::Point value(circle.rad,circumference),zero(constant(0),constant(0)),ratio(constant(1),constant(6.28318530717958647692));
                    system.addConstraintPointOnLine(value,zero,ratio,id);
                    system.addConstraintP2PDistance(line.p1,line.p2,circumference,id);
                }break;
            case RelationKind::PointOnLine:system.addConstraintPointOnLine(a,lines.at(relation.second.entityId),id);break;
            case RelationKind::Midpoint:{
                auto& line=lines.at(relation.second.entityId);
                // Equal coordinate differences express the midpoint exactly and
                // keep its Jacobian linear during interactive size changes.
                for(const bool x:{true,false}){
                    auto* first=x?line.p1.x:line.p1.y;auto* middle=x?a.x:a.y;auto* last=x?line.p2.x:line.p2.y;
                    auxiliaries.push_back((*last-*first)*0.5);auto* half=&auxiliaries.back();unknowns.push_back(half);
                    system.addConstraintDifference(first,middle,half,id);system.addConstraintDifference(middle,last,half,id);
                }break;
            }
            case RelationKind::PointOnCircle:system.addConstraintPointOnCircle(a,circles.at(relation.second.entityId),id);break;
            case RelationKind::Coincident:system.addConstraintP2PCoincident(a,b,id);break;
            case RelationKind::Horizontal:system.addConstraintHorizontal(a,b,id);break;
            case RelationKind::Vertical:system.addConstraintVertical(a,b,id);break;
            case RelationKind::FixedPoint:
                system.addConstraintCoordinateX(a,constant(relation.fixedPosition.x),id);system.addConstraintCoordinateY(a,constant(relation.fixedPosition.y),id);break;
            case RelationKind::Parallel:addLineOrientation(system,relation.first.entityId,relation.second.entityId,false,id);break;
            case RelationKind::Perpendicular:addLineOrientation(system,relation.first.entityId,relation.second.entityId,true,id);break;
            case RelationKind::Tangent:
                if(circles.count(relation.first.entityId)&&circles.count(relation.second.entityId)) {
                    auto& first=circles.at(relation.first.entityId);auto& second=circles.at(relation.second.entityId);
                    system.addConstraintTangentCircumf(first.center,second.center,first.rad,second.rad,false,id);
                }
                else {
                    const auto& lineId = lines.count(relation.first.entityId) ? relation.first.entityId : relation.second.entityId;
                    const auto& curveId = lineId == relation.first.entityId ? relation.second.entityId : relation.first.entityId;
                    auto& line = lines.at(lineId);
                    auto& circle = circles.at(curveId);
                    if (addJoinedArcTangent(system, lineId, curveId, id))
                        break;
                    const double side=(*line.p2.x-*line.p1.x)*(*circle.center.y-*line.p1.y)-(*line.p2.y-*line.p1.y)*(*circle.center.x-*line.p1.x);
                    system.addConstraintTangent(line,circle,side>=0,id);
                }break;
            }
        }
    }
    bool addJoinedArcTangent(GCS::System& system, const std::string& lineId,
                             const std::string& curveId, int id) {
        if (!points.count({curveId, int(PointRole::First)}))
            return false;
        const auto& line = lines.at(lineId);
        for (const auto arcEnd : {PointRole::First, PointRole::Second})
            for (const auto lineEnd : {PointRole::First, PointRole::Second}) {
                if (!areSketchPointsCoincident(sketch, {curveId, arcEnd}, {lineId, lineEnd}))
                    continue;
                // At a joined endpoint the radial-angle equation stays well
                // conditioned when the line-to-circle distance is stationary.
                GCS::Line radius;
                radius.p1 = circles.at(curveId).center;
                radius.p2 = point({curveId, arcEnd});
                const double ux = *line.p2.x - *line.p1.x, uy = *line.p2.y - *line.p1.y;
                const double vx = *radius.p2.x - *radius.p1.x, vy = *radius.p2.y - *radius.p1.y;
                auto side = line;
                system.addConstraintL2LAngle(side, radius, constant((ux * vy - uy * vx < 0 ? -1 : 1) *
                                                                   1.57079632679489661923), id);
                return true;
            }
        return false;
    }
    void addPolygons(GCS::System& system) {
        for(const auto& g:sketch.polygons) {
            auto& center=point({g.centerId,PointRole::First});auto& first=lines.at(g.edges.front());
            GCS::Line firstRadius;firstRadius.p1=center;firstRadius.p2=first.p1;
            for(std::size_t i=1;i<g.edges.size();++i){
                auto& edge=lines.at(g.edges[i]);GCS::Line radius;radius.p1=center;radius.p2=edge.p1;
                system.addConstraintEqualLength(first,edge,tag(g.id));
                system.addConstraintEqualLength(firstRadius,radius,tag(g.id));
            }
        }
    }
    void addReplications(GCS::System& system) {
        for (const auto& copy : sketch.replications) {
            const int id = tag("copy:" + copy.target);
            const auto auxiliary = [&](double value) {
                auxiliaries.push_back(value);
                unknowns.push_back(&auxiliaries.back());
                return &auxiliaries.back();
            };
            const auto scaled = [&](double* value, double factor) {
                if (std::abs(factor) < 1e-14)
                    return constant(0);
                if (factor == 1)
                    return value;
                auto* product = auxiliary(*value * factor);
                GCS::Point p(value, product), zero(constant(0), constant(0)),
                    direction(constant(1), constant(factor));
                system.addConstraintPointOnLine(p, zero, direction, id);
                return product;
            };
            const auto sum = [&](double* first, double* second, double* result, double offset) {
                auto* difference = auxiliary(*result - *first);
                system.addConstraintDifference(first, result, difference, id);
                system.addConstraintDifference(second, difference, constant(offset), id);
            };
            const auto constrain = [&](PointRole role) {
                auto& seed = point({copy.source, role});
                auto& target = point({copy.target, role});
                if (!copy.mirrorLine.empty()) {
                    auto axis=lines.at(copy.mirrorLine);
                    // Symmetric points may coincide on the axis. Normalize the
                    // perpendicular equation by the nondegenerate axis only,
                    // rather than the zero-length segment between those points.
                    auto* normal=new MirrorNormal(seed,target,axis);
                    normal->setTag(id);normal->setDriving(true);system.addConstraint(normal);
                    system.addConstraintMidpointOnLine(seed,target,axis.p1,axis.p2,id);
                } else if (copy.angle == 0) {
                    system.addConstraintDifference(seed.x, target.x, constant(copy.offset.x), id);
                    system.addConstraintDifference(seed.y, target.y, constant(copy.offset.y), id);
                } else {
                    const double c = std::cos(copy.angle), s = std::sin(copy.angle);
                    sum(scaled(seed.x, c), scaled(seed.y, -s), target.x, copy.offset.x);
                    sum(scaled(seed.x, s), scaled(seed.y, c), target.y, copy.offset.y);
                }
            };
            if (circles.count(copy.source)) {
                constrain(PointRole::Center);
                system.addConstraintEqualRadius(circles.at(copy.source), circles.at(copy.target), id);
            } else {
                constrain(PointRole::First);
                if (lines.count(copy.source))
                    constrain(PointRole::Second);
            }
        }
    }
    void addOffsets(GCS::System& system) {
        for (const auto& group : sketch.offsets) {
            const auto& first = group.segments.front();
            const int id = tag("offset:" + first.target);
            if (circles.count(first.source)) {
                auto& source = circles.at(first.source);
                auto& target = circles.at(first.target);
                system.addConstraintP2PCoincident(source.center, target.center, id);
                system.addConstraintDifference(source.rad, target.rad, constant(group.distance), id);
                continue;
            }
            for (std::size_t i = 0; i < group.segments.size(); ++i) {
                const auto& segment = group.segments[i];
                auto source = lines.at(segment.source);
                if (segment.reversed)
                    std::swap(source.p1, source.p2);
                auto& target = lines.at(segment.target);
                system.addConstraintL2LAngle(source, target, constant(0), id);
                system.addConstraintP2LDistance(target.p1, source, constant(std::abs(group.distance)),
                                                 group.distance > 0, id);
                if (i + 1 < group.segments.size() || group.closed) {
                    const auto& next = group.segments[(i + 1) % group.segments.size()];
                    system.addConstraintP2PCoincident(target.p2, lines.at(next.target).p1, id);
                }
                if (!group.closed && i == 0)
                    system.addConstraintPerpendicular(source.p1, target.p1, source, id);
                if (!group.closed && i + 1 == group.segments.size())
                    system.addConstraintPerpendicular(source.p2, target.p2, source, id);
            }
        }
    }
    bool parallelImplied(const SketchDimension& dimension,const std::string& except={}) const {
        // Avoid introducing a duplicate derived parallel equation where existing
        // orientation relations already imply it. No owner constraint is removed.
        std::map<std::string,std::vector<std::pair<std::string,int>>> graph;
        const auto join=[&](const std::string& a,const std::string& b,int parity){graph[a].push_back({b,parity});graph[b].push_back({a,parity});};
        bool beforeCurrent=true;
        for(const auto& r:sketch.relations) {
            if(!except.empty() && r.id==except){beforeCurrent=false;continue;}
            if(r.kind==RelationKind::Collinear && (except.empty() || beforeCurrent))join(r.first.entityId,r.second.entityId,0);
            if(r.kind==RelationKind::Parallel || r.kind==RelationKind::Perpendicular)join(r.first.entityId,r.second.entityId,r.kind==RelationKind::Perpendicular?1:0);
            if(r.kind==RelationKind::Horizontal || r.kind==RelationKind::Vertical)join(r.first.entityId,"world-horizontal",r.kind==RelationKind::Vertical?1:0);
        }
        std::set<std::pair<std::string,int>> pinned;
        for(const auto& r:sketch.relations)if(r.kind==RelationKind::FixedPoint)pinned.insert({r.first.entityId,int(r.first.role)});
        for(const auto& e:sketch.entities)if(e.kind==EntityKind::Line && pinned.count({e.id,int(PointRole::First)}) && pinned.count({e.id,int(PointRole::Second)})) {
            if(std::abs(e.second.y-e.first.y)<1e-9)join(e.id,"world-horizontal",0);
            if(std::abs(e.second.x-e.first.x)<1e-9)join(e.id,"world-horizontal",1);
        }
        for(const auto& d:sketch.dimensions){if(d.id==dimension.id)break;if(d.driving && d.kind==DimensionKind::LineDistance)join(d.first.entityId,d.second.entityId,0);}
        std::map<std::string,int> seen;std::vector<std::string> queue{dimension.first.entityId};seen[queue.front()]=0;
        for(std::size_t i=0;i<queue.size();++i)for(const auto& edge:graph[queue[i]])if(!seen.count(edge.first)){seen[edge.first]=seen[queue[i]]^edge.second;queue.push_back(edge.first);}
        const auto found=seen.find(dimension.second.entityId);return found!=seen.end() && found->second==0;
    }
    void addDimensions(GCS::System& system) {
        for(const auto& d:sketch.dimensions)if(d.driving) {
            const int id=tag(d.id);auto& a=point(d.first);auto& b=point(d.second);
            switch(d.kind) {
            case DimensionKind::PointLineDistance:
                system.addConstraintP2LDistance(point(d.first),lines.at(d.second.entityId),
                                                 constant(d.value),d.sense>0,id);break;
            case DimensionKind::LineDistance:
                if(!parallelImplied(d))system.addConstraintParallel(lines.at(d.first.entityId),lines.at(d.second.entityId),id);
                system.addConstraintP2LDistance(point(d.second),lines.at(d.first.entityId),constant(d.value),d.sense>0,id);break;
            case DimensionKind::LineAngle: {
                auto first=lines.at(d.first.entityId),second=lines.at(d.second.entityId);
                if(d.first.role==PointRole::Second)std::swap(first.p1,first.p2);if(d.second.role==PointRole::Second)std::swap(second.p1,second.p2);
                system.addConstraintL2LAngle(first,second,constant(d.value*d.sense*3.14159265358979323846/180.0),id);break;
            }
            case DimensionKind::PointDistance:
            case DimensionKind::LineLength:system.addConstraintP2PDistance(a,b,constant(d.value),id);break;
            case DimensionKind::HorizontalDistance:system.addConstraintDifference(a.x,b.x,constant(d.value*d.sense),id);break;
            case DimensionKind::VerticalDistance:system.addConstraintDifference(a.y,b.y,constant(d.value*d.sense),id);break;
            case DimensionKind::CircleRadius:system.addConstraintCircleRadius(circles.at(d.first.entityId),constant(d.value),id);break;
            case DimensionKind::CircleDiameter:system.addConstraintCircleDiameter(circles.at(d.first.entityId),constant(d.value),id);break;
            }
        }
    }
    void holdLines(GCS::System& system,const SolveOptions& options) {
        if(options.stationaryLines.empty())return;
        // Redundancy diagnosis can solve trial geometry. Preserve the edit seed
        // while choosing independent anchors, including partially fixed axes.
        const auto diagnose=[&] {
            std::vector<double> seed;for(auto* value:unknowns)seed.push_back(*value);
            system.initSolution();
            for(std::size_t i=0;i<unknowns.size();++i)*unknowns[i]=seed[i];
        };
        system.declareUnknowns(unknowns);diagnose();
        int freedom=system.dofsNumber();
        for(const auto& entity:options.stationaryLines) {
            const auto& line=lines.at(entity);
            for(auto p:{line.p1,line.p2})for(const bool x:{true,false}) {
                const int id=tag("hold:"+entity);
                if(x)system.addConstraintCoordinateX(p,constant(*p.x),id);
                else system.addConstraintCoordinateY(p,constant(*p.y),id);
                diagnose();const int remaining=system.dofsNumber();
                // H/V, fixed-point and dimensional relations may already
                // determine this coordinate; do not add a duplicate equation.
                if(remaining>=freedom){system.clearByTag(id);tags.erase(id);}
                else freedom=remaining;
            }
        }
        system.invalidatedDiagnosis();
    }
    void addTargets(GCS::System& system,const SolveOptions& options) {
        for(const auto& target:options.lineTargets){
            finiteValue(target.parameter,"Line grab parameter");finiteValue(target.position.x,"Target X");finiteValue(target.position.y,"Target Y");
            if(target.parameter<0||target.parameter>1||isSketchProjection(sketch,target.entityId))throw std::runtime_error("Invalid line drag target.");
            const auto& line=lines.at(target.entityId);const int id=tag("drag:"+target.entityId);
            for(const bool x:{true,false}){
                double* first=x?line.p1.x:line.p1.y;double* last=x?line.p2.x:line.p2.y;
                const auto auxiliary=[&](double value){auxiliaries.push_back(value);unknowns.push_back(&auxiliaries.back());return &auxiliaries.back();};
                auto* extent=auxiliary(*last-*first);auto* offset=auxiliary((*last-*first)*target.parameter);auto* position=auxiliary(*first+*offset);
                system.addConstraintDifference(first,last,extent,id);system.addConstraintDifference(first,position,offset,id);
                GCS::Point ratio(extent,offset),zero(constant(0),constant(0)),direction(constant(1),constant(target.parameter));
                system.addConstraintPointOnLine(ratio,zero,direction,id);
                GCS::Point grab(x?position:constant(0),x?constant(0):position);
                if(x)system.addConstraintCoordinateX(grab,constant(target.position.x),-1);
                else system.addConstraintCoordinateY(grab,constant(target.position.y),-1);
            }
        }
        for(const auto& target:options.pointTargets) {
            finiteValue(target.position.x,"Target X");finiteValue(target.position.y,"Target Y");
            if(target.point.role==PointRole::Origin||isSketchProjection(sketch,target.point.entityId))
                throw std::runtime_error("The reference geometry is fixed.");
            auto& p=point(target.point);const int id=target.hard?tag("edit:"+target.point.entityId):-1;
            system.addConstraintCoordinateX(p,constant(target.position.x),id);system.addConstraintCoordinateY(p,constant(target.position.y),id);
        }
        for(const auto& target:options.radiusTargets) {
            if(isSketchProjection(sketch,target.first))throw std::runtime_error("Projected geometry follows its source edge.");
            finiteValue(target.second,"Radius target");if(target.second<=1e-7)throw std::runtime_error("Radius must be above kernel tolerance.");
            system.addConstraintCircleRadius(circles.at(target.first),constant(target.second),tag("edit:"+target.first));
        }
    }
    std::vector<std::string> identifiers(const GCS::VEC_I& source) const {
        std::set<std::string> result;for(int id:source){const auto found=tags.find(id);if(found!=tags.end())result.insert(found->second);}return {result.begin(),result.end()};
    }
};
}
const char* solveStateName(SolveState state) {
    switch(state){case SolveState::UnderConstrained:return "Under-defined";case SolveState::FullyConstrained:return "Fully defined";case SolveState::Redundant:return "Redundant constraints";case SolveState::Conflicting:return "Conflicting constraints";case SolveState::NonConverged:return "Solver did not converge";case SolveState::InvalidInput:return "Invalid constraint input";case SolveState::UnsupportedSpace:return "Unsupported sketch space";default:return "Solver failure";}
}
SketchSolveResult solvePlanarSketch(const Sketch& input,const SolveOptions& options)
{
    SketchSolveResult result;result.sketch=input;bool solving=false;
    try {
        if(input.space!=SketchSpace::Planar2D){result.diagnostics.state=SolveState::UnsupportedSpace;result.diagnostics.message="Spatial sketches require a future 3D solver adapter.";return result;}
        if(options.maximumIterations<1 || options.maximumIterations>10000 || !std::isfinite(options.tolerance) || options.tolerance<1e-12 || options.tolerance>1e-4)throw std::runtime_error("Invalid solver iteration limit or tolerance.");
        validateSketchConstraintReferences(input);
        auto geometry=input;geometry.relations.clear();geometry.dimensions.clear();geometry.polygons.clear();(void)sketchShape(geometry);
        if(input.entities.empty()){result.success=true;result.diagnostics.state=SolveState::FullyConstrained;result.diagnostics.degreesOfFreedom=0;result.diagnostics.message="Empty sketch.";return result;}
        WorkingSketch work(input);GCS::System system;
        system.maxIter=options.maximumIterations;system.maxIterRedundant=options.maximumIterations;
        system.convergence=options.tolerance;system.debugMode=GCS::NoDebug;
        work.addRelations(system);work.addPolygons(system);work.addReplications(system);work.addOffsets(system);
        work.addDimensions(system);work.holdLines(system,options);work.addTargets(system,options);
        system.declareUnknowns(work.unknowns);solving=true;system.initSolution();
        const auto status=system.solve();
        if(status==GCS::SolveStatus::Success || status==GCS::SolveStatus::Converged)system.applySolution();
        system.diagnose();result.diagnostics.degreesOfFreedom=system.dofsNumber();
        GCS::VEC_I ids;system.getConflicting(ids);result.diagnostics.conflicting=work.identifiers(ids);
        system.getRedundant(ids);result.diagnostics.redundant=work.identifiers(ids);
        GCS::VEC_I partial;system.getPartiallyRedundant(partial);const auto more=work.identifiers(partial);
        for(const auto& id:more)if(std::find(result.diagnostics.redundant.begin(),result.diagnostics.redundant.end(),id)==result.diagnostics.redundant.end())result.diagnostics.redundant.push_back(id);
        if(!result.diagnostics.conflicting.empty()){result.diagnostics.state=SolveState::Conflicting;result.diagnostics.message="Conflicting constraints; the previous sketch is retained.";return result;}
        if(!result.diagnostics.redundant.empty()){result.diagnostics.state=SolveState::Redundant;result.diagnostics.message="Redundant constraints; edit an existing constraint instead of adding an independent duplicate.";return result;}
        if(status!=GCS::SolveStatus::Success && status!=GCS::SolveStatus::Converged){result.diagnostics.state=SolveState::NonConverged;result.diagnostics.message="The solver could not satisfy the requested constraints within its iteration limit.";return result;}
        for(const auto& entry:work.tags) {
            const double error=system.calculateConstraintErrorByTag(entry.first);
            if(!std::isfinite(error) || std::abs(error)>std::max(1e-6,options.tolerance*10)) {
                result.diagnostics.conflicting.push_back(entry.second);result.diagnostics.state=SolveState::NonConverged;result.diagnostics.message="A constraint residual exceeded tolerance; the previous sketch is retained.";return result;
            }
            result.diagnostics.maximumResidual=std::max(result.diagnostics.maximumResidual,std::abs(error));
        }
        std::vector<std::pair<SketchPointRef,Point2>> anchors;
        for(const auto& target:options.pointTargets)if(target.hard)anchors.push_back({target.point,target.position});
        for(const auto& entity:options.stationaryLines)for(const auto role:{PointRole::First,PointRole::Second}) {
            const auto expected=sketchPointValue(input,{entity,role}),actual=sketchPointValue(work.sketch,{entity,role});
            if(std::hypot(actual.x-expected.x,actual.y-expected.y)>std::max(1e-7,options.tolerance*10))
                throw std::runtime_error("This edit conflicts with keeping the mirror axis stationary.");
            anchors.push_back({{entity,role},expected});
        }
        auto candidate=canonicalizeSketchEqualities(work.sketch,anchors);
        validateSketchOffsetGeometry(candidate);
        for(auto& d:candidate.dimensions)if(!d.driving)d.value=dimensionMeasurement(candidate,d);
        (void)sketchShape(candidate);
        result.sketch=std::move(candidate);result.success=true;
        result.diagnostics.state=result.diagnostics.degreesOfFreedom==0?SolveState::FullyConstrained:SolveState::UnderConstrained;
        result.diagnostics.message=std::string(solveStateName(result.diagnostics.state))+" ("+std::to_string(result.diagnostics.degreesOfFreedom)+" degrees of freedom).";
    } catch(const Standard_Failure& e){result.diagnostics.state=SolveState::SolverFailure;result.diagnostics.message=std::string("Invalid solved geometry: ")+e.GetMessageString();}
      catch(const std::exception& e){result.diagnostics.state=solving?SolveState::SolverFailure:SolveState::InvalidInput;result.diagnostics.message=e.what();}
    return result;
}
Sketch requireSolvedSketch(const SketchSolveResult& result) {
    if(!result.success){std::string message=result.diagnostics.message;for(const auto& id:result.diagnostics.conflicting)message+=" "+id;for(const auto& id:result.diagnostics.redundant)message+=" "+id;throw std::runtime_error(message);}
    return result.sketch;
}

SketchDefinition analyzeSketchDefinition(const Sketch& sketch){
    SketchDefinition result;
    try{
        if(sketch.space!=SketchSpace::Planar2D)return result;
        validateSketchConstraintReferences(sketch);
        (void)sketchShape(sketch);WorkingSketch work(sketch);GCS::System system;system.debugMode=GCS::NoDebug;
        work.addRelations(system);work.addPolygons(system);work.addReplications(system);work.addOffsets(system);
        work.addDimensions(system);
        system.declareUnknowns(work.unknowns);system.diagnose();
        GCS::VEC_I conflicts,redundant;system.getConflicting(conflicts);system.getRedundant(redundant);
        if(!conflicts.empty() || !redundant.empty())return result;
        GCS::VEC_pD dependent;system.getDependentParams(dependent);std::set<double*> movable(dependent.begin(),dependent.end());
        if(system.isEmptyDiagnoseMatrix())movable.insert(work.unknowns.begin(),work.unknowns.end());
        const auto fixedPoint=[&](SketchPointRef ref){const auto& p=work.point(ref);return !movable.count(p.x) && !movable.count(p.y);};
        for(const auto& point:sketchPoints(sketch))if(fixedPoint(point.first))result.fixedPoints.push_back(point.first);
        for(const auto& e:sketch.entities){
            // A fixed orientation or size alone does not define a curve. All of
            // its geometric parameters, including position, must be immobile.
            bool defined=false;
            if(e.kind==EntityKind::Line || e.kind==EntityKind::Rectangle)
                defined=fixedPoint({e.id,PointRole::First}) && fixedPoint({e.id,PointRole::Second});
            if(e.kind==EntityKind::Spline) {
                defined=fixedPoint({e.id,PointRole::First}) && fixedPoint({e.id,PointRole::Second});
                for(std::size_t i=0;i<e.splinePoints.size();++i)defined=defined&&fixedPoint({e.id,splinePointRole(i)});
            }
            if(isCircular(e.kind)) {
                defined=fixedPoint({e.id,PointRole::Center}) && !movable.count(work.circles.at(e.id).rad);
                if(e.kind==EntityKind::Arc)
                    defined=defined && fixedPoint({e.id,PointRole::First}) && fixedPoint({e.id,PointRole::Second});
            }
            if(defined)result.definedCurves.push_back(e.id);
        }
        result.degreesOfFreedom=system.dofsNumber();result.valid=true;
    }catch(const Standard_Failure&){return {};}catch(const std::exception&){return {};}
    return result;
}

Sketch setSketchDimension(const Sketch& sketch,const SketchDimension& dimension) {
    auto candidate=sketch;auto found=std::find_if(candidate.dimensions.begin(),candidate.dimensions.end(),[&](const auto& d){return d.id==dimension.id;});
    if(found==candidate.dimensions.end())candidate.dimensions.push_back(dimension);else *found=dimension;
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
Sketch removeSketchDimension(const Sketch& sketch,const std::string& id) {
    auto candidate=sketch;const auto found=std::find_if(candidate.dimensions.begin(),candidate.dimensions.end(),[&](const auto& d){return d.id==id;});
    if(found==candidate.dimensions.end())throw std::runtime_error("Unknown dimension.");candidate.dimensions.erase(found);
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
Sketch setSketchRelation(const Sketch& sketch,const SketchRelation& relation) {
    auto candidate=sketch;auto found=std::find_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.id==relation.id;});
    if(found==candidate.relations.end())candidate.relations.push_back(relation);else {
        const auto group=found->groupId;
        if(!group.empty() && std::count_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.groupId==group;})>1){
            if(relation.kind!=RelationKind::Equal && relation.kind!=RelationKind::EqualCurveLength)throw std::runtime_error("Delete the equality group before applying a different relation to its members.");
            for(auto& r:candidate.relations)if(r.groupId==group)r.kind=relation.kind;
        }else {*found=relation;if(relation.kind!=RelationKind::Equal && relation.kind!=RelationKind::EqualCurveLength)found->groupId.clear();}
    }
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
Sketch removeSketchRelation(const Sketch& sketch,const std::string& id) {
    auto candidate=sketch;const auto found=std::find_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.id==id;});
    if(found==candidate.relations.end())throw std::runtime_error("Unknown relation.");
    const auto group=found->groupId;
    if(group.empty())candidate.relations.erase(found);
    else candidate.relations.erase(std::remove_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.groupId==group;}),candidate.relations.end());
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
}
