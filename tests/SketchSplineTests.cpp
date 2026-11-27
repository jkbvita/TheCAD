#include "document/PartDocument.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/SketchSplines.h"
#include "modeling/SketchTools.h"
#include <BRepCheck_Analyzer.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid spline operation accepted");}
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try {
        using namespace modeling;
        const std::vector<Point2> fit{{0,0},{10,15},{22,-8},{35,5}};
        Sketch sketch; sketch.entities.push_back(interpolatingSpline(fit,"spline"));
        for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}) {
            sketch.plane=plane; const auto curve=sketchSplineCurve(sketch,sketch.entities.front());
            for(const auto p:fit){GeomAPI_ProjectPointOnCurve projected(sketchPoint(sketch,p),curve);require(projected.LowerDistance()<1e-6,"Spline missed a fit point");}
            require(BRepCheck_Analyzer(sketchShape(sketch)).IsValid(),"Invalid native spline edge");
        }
        sketch.plane=SketchPlane::XY;
        auto solved=requireSolvedSketch(solvePlanarSketch(sketch));
        auto moved=moveSketchPoint(solved,{"spline",splinePointRole(0)},{12,20});
        require(std::abs(moved.entities[0].splinePoints[0].y-20)<1e-6,"Interior fit point did not drag");
        SketchDimension dimension; dimension.id="x"; dimension.kind=DimensionKind::HorizontalDistance;
        dimension.first={}; dimension.second={"spline",splinePointRole(0)}; dimension.value=12;
        moved=setSketchDimension(moved,dimension);
        require(std::abs(sketchPointValue(moved,dimension.second).x-12)<1e-6,"Fit point dimension failed");
        moved=moveSketchPoint(moved,dimension.second,{12,25});
        require(std::abs(sketchPointValue(moved,dimension.second).y-25)<1e-6,"Constrained fit point drag failed");
        auto locked=sketch;
        int count=0;
        for(const auto& point:sketchPoints(sketch))if(point.first.role!=PointRole::Origin) {
            SketchRelation fixed; fixed.id="fix-"+std::to_string(++count); fixed.kind=RelationKind::FixedPoint;
            fixed.first=point.first; fixed.fixedPosition=point.second; locked.relations.push_back(fixed);
        }
        locked=requireSolvedSketch(solvePlanarSketch(locked));
        const auto definition=analyzeSketchDefinition(locked);
        require(definition.valid && definition.degreesOfFreedom==0 &&
            std::find(definition.definedCurves.begin(),definition.definedCurves.end(),"spline")!=definition.definedCurves.end(),"All fit points must define the spline");
        const auto immobile=moveSketchPoint(locked,{"spline",splinePointRole(0)},{15,17});
        require(std::abs(immobile.entities[0].splinePoints[0].x-10)<1e-6 &&
                std::abs(immobile.entities[0].splinePoints[0].y-15)<1e-6,"Fixed fit point moved");
        Sketch loop; loop.entities.push_back(interpolatingSpline({{-10,0},{0,10},{10,0},{0,-10},{-10,0}},"closed"));
        auto regions=closedContours(loop); require(regions.size()==1,"Closed spline did not form a profile");
        auto part=document::PartDocument::createEmpty(); const auto sid=part.addSketch(loop);
        part.addExtrudeRegions(sid,{regions[0].entityIds},8,Direction::Positive);
        require(BRepCheck_Analyzer(part.shape()).IsValid(),"Closed spline extrusion invalid");
        const auto resultId=part.features().back().id;
        auto changedProfile=part.features().front(); changedProfile.sketch.entities[0].splinePoints[0].y+=3;
        part.replaceFeature(changedProfile);
        require(part.features().back().id==resultId && BRepCheck_Analyzer(part.shape()).IsValid(),"Spline profile edit lost its feature reference");
        QTemporaryDir temp; const auto path=temp.filePath("spline.thepart"); part.save(path);
        auto loaded=document::PartDocument::load(path);
        require(loaded.features()[0].sketch.entities[0].kind==EntityKind::Spline &&
            loaded.features()[0].sketch.entities[0].splinePoints.size()==3,"Spline did not persist");
        auto pointPart=document::PartDocument::createEmpty(); pointPart.addSketch(moved); pointPart.save(path);
        auto pointLoaded=document::PartDocument::load(path);
        require(pointLoaded.features()[0].sketch.dimensions[0].second.role==splinePointRole(0),"Fit-point dimension identity lost");
        Sketch mixed; mixed.entities.push_back(interpolatingSpline({{0,0},{10,12},{20,0}},"arch"));
        mixed.entities.push_back({EntityKind::Line,{20,0},{0,0},1,"base"});
        require(closedContours(mixed).size()==1,"Mixed line/spline loop did not close");
        auto construction=setSketchConstruction(loop,{"closed"},true);
        require(closedContours(construction).empty(),"Construction spline formed a profile");
        rejects([]{interpolatingSpline({{0,0},{0,0},{10,5}});});
        rejects([]{interpolatingSpline({{0,0}});});
        std::cout<<"PASS: native interpolation, planes, point constraints/dragging, definition, profiles and persistence\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
