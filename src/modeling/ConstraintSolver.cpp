#include "modeling/ConstraintSolver.h"
#include "GCS.h"
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
namespace thecad::modeling {
namespace {
constexpr double coordinateLimit=1e9;
void finiteValue(double value,const char* label){if(!std::isfinite(value)||std::abs(value)>coordinateLimit)throw std::runtime_error(std::string(label)+" must be finite and within the prototype limit.");}
class WorkingSketch {
public:
    Sketch sketch;
    double originX=0,originY=0;
    std::map<std::pair<std::string,int>,GCS::Point> points;
    std::map<std::string,GCS::Line> lines;
    std::map<std::string,GCS::Circle> circles;
    GCS::VEC_pD unknowns;
    std::deque<double> constants;
    std::map<int,std::string> tags;
    int nextTag=1;
    explicit WorkingSketch(const Sketch& input):sketch(input) {
        points.emplace(std::make_pair(std::string{},int(PointRole::Origin)),GCS::Point(&originX,&originY));
        for(auto& entity:sketch.entities) {
            if(entity.id.empty())throw std::runtime_error("Solver entities require stable IDs.");
            const auto firstRole=entity.kind==EntityKind::Circle?PointRole::Center:PointRole::First;
            points.emplace(std::make_pair(entity.id,int(firstRole)),GCS::Point(&entity.first.x,&entity.first.y));
            unknowns.push_back(&entity.first.x);unknowns.push_back(&entity.first.y);
            if(entity.kind==EntityKind::Circle) {
                unknowns.push_back(&entity.radius);GCS::Circle circle;circle.center=point({entity.id,PointRole::Center});circle.rad=&entity.radius;circles.emplace(entity.id,circle);
            } else {
                points.emplace(std::make_pair(entity.id,int(PointRole::Second)),GCS::Point(&entity.second.x,&entity.second.y));
                unknowns.push_back(&entity.second.x);unknowns.push_back(&entity.second.y);
                if(entity.kind==EntityKind::Line){GCS::Line line;line.p1=point({entity.id,PointRole::First});line.p2=point({entity.id,PointRole::Second});lines.emplace(entity.id,line);}
            }
        }
    }
    GCS::Point& point(const SketchPointRef& ref) {
        const auto found=points.find({ref.entityId,int(ref.role)});if(found==points.end())throw std::runtime_error("Constraint references a missing point.");return found->second;
    }
    double* constant(double value){constants.push_back(value);return &constants.back();}
    int tag(const std::string& id){const int value=nextTag++;tags.emplace(value,id);return value;}
    void addRelations(GCS::System& system) {
        for(const auto& relation:sketch.relations) {
            const int id=tag(relation.id);auto& a=point(relation.first);auto& b=point(relation.second);
            switch(relation.kind) {
            case RelationKind::Coincident:system.addConstraintP2PCoincident(a,b,id);break;
            case RelationKind::Horizontal:system.addConstraintHorizontal(a,b,id);break;
            case RelationKind::Vertical:system.addConstraintVertical(a,b,id);break;
            case RelationKind::FixedPoint:
                system.addConstraintCoordinateX(a,constant(relation.fixedPosition.x),id);system.addConstraintCoordinateY(a,constant(relation.fixedPosition.y),id);break;
            case RelationKind::Parallel:system.addConstraintParallel(lines.at(relation.first.entityId),lines.at(relation.second.entityId),id);break;
            case RelationKind::Perpendicular:system.addConstraintPerpendicular(lines.at(relation.first.entityId),lines.at(relation.second.entityId),id);break;
            case RelationKind::Tangent:
                if(circles.count(relation.first.entityId)&&circles.count(relation.second.entityId)) {
                    auto& first=circles.at(relation.first.entityId);auto& second=circles.at(relation.second.entityId);
                    system.addConstraintTangentCircumf(first.center,second.center,first.rad,second.rad,false,id);
                }
                else {
                    auto& line=lines.at(lines.count(relation.first.entityId)?relation.first.entityId:relation.second.entityId);
                    auto& circle=circles.at(circles.count(relation.first.entityId)?relation.first.entityId:relation.second.entityId);
                    const double side=(*line.p2.x-*line.p1.x)*(*circle.center.y-*line.p1.y)-(*line.p2.y-*line.p1.y)*(*circle.center.x-*line.p1.x);
                    system.addConstraintTangent(line,circle,side>=0,id);
                }break;
            }
        }
    }
    bool parallelImplied(const SketchDimension& dimension) const {
        // Avoid introducing a duplicate derived parallel equation where existing
        // orientation relations already imply it. No owner constraint is removed.
        std::map<std::string,std::vector<std::pair<std::string,int>>> graph;
        const auto join=[&](const std::string& a,const std::string& b,int parity){graph[a].push_back({b,parity});graph[b].push_back({a,parity});};
        for(const auto& r:sketch.relations) {
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
    void addTargets(GCS::System& system,const SolveOptions& options) {
        for(const auto& target:options.pointTargets) {
            finiteValue(target.position.x,"Target X");finiteValue(target.position.y,"Target Y");
            if(target.point.role==PointRole::Origin)throw std::runtime_error("The sketch origin is fixed.");
            auto& p=point(target.point);const int id=target.hard?tag("edit:"+target.point.entityId):-1;
            system.addConstraintCoordinateX(p,constant(target.position.x),id);system.addConstraintCoordinateY(p,constant(target.position.y),id);
        }
        for(const auto& target:options.radiusTargets) {
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
        auto geometry=input;geometry.relations.clear();geometry.dimensions.clear();(void)sketchShape(geometry);
        if(input.entities.empty()){result.success=true;result.diagnostics.state=SolveState::FullyConstrained;result.diagnostics.degreesOfFreedom=0;result.diagnostics.message="Empty sketch.";return result;}
        WorkingSketch work(input);GCS::System system;
        system.maxIter=options.maximumIterations;system.maxIterRedundant=options.maximumIterations;
        system.convergence=options.tolerance;system.debugMode=GCS::NoDebug;
        work.addRelations(system);work.addDimensions(system);work.addTargets(system,options);
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
        auto candidate=canonicalizeSketchEqualities(work.sketch,anchors);
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
    if(found==candidate.relations.end())candidate.relations.push_back(relation);else *found=relation;
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
Sketch removeSketchRelation(const Sketch& sketch,const std::string& id) {
    auto candidate=sketch;const auto found=std::find_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.id==id;});
    if(found==candidate.relations.end())throw std::runtime_error("Unknown relation.");candidate.relations.erase(found);
    return requireSolvedSketch(solvePlanarSketch(candidate));
}
}
