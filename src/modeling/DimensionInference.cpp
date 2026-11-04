#include "modeling/DimensionInference.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::modeling {
namespace {
constexpr double pi=3.14159265358979323846;
const SketchEntity& entity(const Sketch& sketch,const std::string& id){const auto found=std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==id;});if(found==sketch.entities.end())throw std::runtime_error("The selected sketch entity is unavailable.");return *found;}
Point2 vector(const SketchEntity& e){return {e.second.x-e.first.x,e.second.y-e.first.y};}
double cross(Point2 a,Point2 b){return a.x*b.y-a.y*b.x;}
double angle(Point2 a){double result=std::atan2(a.y,a.x);return result<0?result+2*pi:result;}
void inferPointDistance(const Sketch& sketch,SketchDimension& d,bool line) {
    const auto a=sketchPointValue(sketch,d.first),b=sketchPointValue(sketch,d.second);
    const bool xInside=d.annotation.x>=std::min(a.x,b.x) && d.annotation.x<=std::max(a.x,b.x);
    const bool yInside=d.annotation.y>=std::min(a.y,b.y) && d.annotation.y<=std::max(a.y,b.y);
    const double dx=b.x-a.x,dy=b.y-a.y;
    if((xInside && !yInside && std::abs(dx)>1e-7) || std::abs(dy)<1e-7){d.kind=DimensionKind::HorizontalDistance;d.sense=dx<0?-1:1;}
    else if((yInside && !xInside && std::abs(dy)>1e-7) || std::abs(dx)<1e-7){d.kind=DimensionKind::VerticalDistance;d.sense=dy<0?-1:1;}
    else d.kind=line?DimensionKind::LineLength:DimensionKind::PointDistance;
}
}
SketchDimension inferDimension(const Sketch& sketch,const std::vector<SketchPointRef>& points,const std::vector<std::string>& entities,Point2 annotation,bool radius) {
    SketchDimension d;d.id="pending-dimension";d.annotation=annotation;
    if(points.size()==2){d.first=points[0];d.second=points[1];inferPointDistance(sketch,d,false);}
    else if(!points.empty())throw std::runtime_error("Select a second point, then click to place the dimension.");
    else if(entities.size()==1) {
        const auto& e=entity(sketch,entities[0]);
        if(e.kind==EntityKind::Circle){d.kind=radius?DimensionKind::CircleRadius:DimensionKind::CircleDiameter;d.first={e.id,PointRole::Center};}
        else if(e.kind==EntityKind::Line){d.first={e.id,PointRole::First};d.second={e.id,PointRole::Second};inferPointDistance(sketch,d,true);}
        else throw std::runtime_error("Select a line, circle or two sketch points.");
    } else if(entities.size()==2) {
        const auto& a=entity(sketch,entities[0]);const auto& b=entity(sketch,entities[1]);
        if(a.kind!=EntityKind::Line || b.kind!=EntityKind::Line)throw std::runtime_error("Two-entity dimensions currently require two lines.");
        d.first={a.id,PointRole::First};d.second={b.id,PointRole::First};const auto u=vector(a),v=vector(b);const double determinant=cross(u,v);
        if(std::abs(determinant)<=1e-7*std::hypot(u.x,u.y)*std::hypot(v.x,v.y)) {
            d.kind=DimensionKind::LineDistance;d.sense=cross(u,{b.first.x-a.first.x,b.first.y-a.first.y})<0?-1:1;
        } else {
            d.kind=DimensionKind::LineAngle;
            const double t=cross({b.first.x-a.first.x,b.first.y-a.first.y},v)/determinant;const Point2 center{a.first.x+t*u.x,a.first.y+t*u.y};
            struct Ray{double angle;int line;bool reverse;};std::vector<Ray> rays{{angle(u),0,false},{angle({-u.x,-u.y}),0,true},{angle(v),1,false},{angle({-v.x,-v.y}),1,true}};
            std::sort(rays.begin(),rays.end(),[](const auto& x,const auto& y){return x.angle<y.angle;});const double cursor=angle({annotation.x-center.x,annotation.y-center.y});
            for(std::size_t i=0;i<rays.size();++i){const auto& lo=rays[i];const auto& hi=rays[(i+1)%rays.size()];double width=hi.angle-lo.angle;if(width<0)width+=2*pi;double offset=cursor-lo.angle;if(offset<0)offset+=2*pi;if(offset<=width){
                const auto& first=lo.line==0?lo:hi;const auto& second=lo.line==1?lo:hi;
                d.first.role=first.reverse?PointRole::Second:PointRole::First;d.second.role=second.reverse?PointRole::Second:PointRole::First;d.sense=lo.line==0?1:-1;break;
            }}
        }
    } else throw std::runtime_error("Select one line/circle, two lines or two points.");
    d.value=dimensionMeasurement(sketch,d);
    if(!std::isfinite(d.value) || d.value<=1e-7)throw std::runtime_error("The selected references have no measurable distance or angle.");
    return d;
}
}
