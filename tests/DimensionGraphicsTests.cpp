#include "modeling/DimensionAnnotation.h"
#include "modeling/DimensionInference.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
namespace {
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void near(Point2 a,Point2 b){require(std::hypot(a.x-b.x,a.y-b.y)<1e-7,"Annotation/reference mismatch");}
Point2 transform(Point2 p){return {100-p.y,70+p.x};}
}
int main(){try{
    for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}){
        Sketch s;s.plane=plane;s.entities={{EntityKind::Line,{10,10},{50,10},1,"line"},{EntityKind::Line,{-20,-10},{-10,0},1,"other"}};
        auto d=inferDimension(s,{}, {"line"},{65,25});d.kind=DimensionKind::LineLength;
        const auto original=dimensionGraphics(s,d,0.2);near(original.text,{65,25});
        for(auto& e:s.entities){e.first=transform(e.first);e.second=transform(e.second);}
        std::reverse(s.entities.begin(),s.entities.end()); // References must follow IDs, not indices.
        const auto rotated=dimensionGraphics(s,d,0.2);near(rotated.text,transform(original.text));
        require(rotated.segments.size()==original.segments.size(),"Stable layout after rotation");
        for(std::size_t i=0;i<rotated.segments.size();++i){near(rotated.segments[i].first,transform(original.segments[i].first));near(rotated.segments[i].second,transform(original.segments[i].second));}
        placeDimensionAnnotation(s,d,{20,40});near(dimensionAnnotationPosition(s,d),{20,40});
        placeDimensionAnnotation(s,d,{30,50});near(dimensionAnnotationPosition(s,d),{30,50});require(d.value==40,"Moving text must not edit value");
        auto& line=s.entities[1];line.second.y+=20;near(dimensionAnnotationPosition(s,d),{30,60});
        d.kind=DimensionKind::HorizontalDistance;placeDimensionAnnotation(s,d,{40,20});
        for(auto& e:s.entities){e.first.x+=15;e.first.y-=7;e.second.x+=15;e.second.y-=7;}
        near(dimensionAnnotationPosition(s,d),{55,13});
    }
    Sketch s;s.entities={{EntityKind::Circle,{10,20},{},5,"circle"}};
    for(const bool radius:{false,true}){
        auto d=inferDimension(s,{}, {"circle"},{20,20},radius);
        auto g=dimensionGraphics(s,d,0.1);near(g.text,{20,20});
        near(g.segments.front().first,radius?Point2{10,20}:Point2{5,20});near(g.segments.front().second,{15,20});
        require(g.segments.size()==(radius?4u:6u),"Circle boundary arrows and connected leader");
        s.entities[0].first={40,50};s.entities[0].radius=10;
        g=dimensionGraphics(s,d,0.1);near(g.text,{60,50});near(g.segments.front().second,{50,50});
        placeDimensionAnnotation(s,d,{40,70});g=dimensionGraphics(s,d,0.1);near(g.text,{40,70});near(g.segments.front().second,{40,60});
        require(d.value==(radius?5:10),"Graphics cannot change mathematical target");
        s.entities[0].first={10,20};s.entities[0].radius=5;
    }
    Sketch angular;angular.entities={{EntityKind::Line,{0,0},{30,0},1,"a"},{EntityKind::Line,{0,0},{20,20},1,"b"}};
    auto d=inferDimension(angular,{}, {"a","b"},{25,10});d.driving=false;
    const auto before=dimensionGraphics(angular,d,0.2);
    angular.entities[1].second={0,30};const auto after=dimensionGraphics(angular,d,0.2);
    require(after.segments.size()>before.segments.size(),"Reference angular arc must use current measured geometry");
    for(auto& e:angular.entities){e.first=transform(e.first);e.second=transform(e.second);}
    near(dimensionAnnotationPosition(angular,d),transform(after.text));
    std::cout<<"PASS: current geometry, stable IDs, translation/rotation/resize, repeated placement, circle boundary arrows and angular references\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
