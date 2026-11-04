#include "modeling/DimensionAnnotation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::modeling {
namespace {
constexpr double pi=3.14159265358979323846;
Point2 add(Point2 a,Point2 b){return {a.x+b.x,a.y+b.y};}
Point2 sub(Point2 a,Point2 b){return {a.x-b.x,a.y-b.y};}
Point2 mul(Point2 a,double t){return {a.x*t,a.y*t};}
double dot(Point2 a,Point2 b){return a.x*b.x+a.y*b.y;}
double cross(Point2 a,Point2 b){return a.x*b.y-a.y*b.x;}
double length(Point2 a){return std::hypot(a.x,a.y);}
Point2 unit(Point2 a){const double n=length(a);return n>1e-12?mul(a,1/n):Point2{1,0};}
Point2 normal(Point2 a){return {-a.y,a.x};}
const SketchEntity& entity(const Sketch& s,const std::string& id){
    const auto e=std::find_if(s.entities.begin(),s.entities.end(),[&](const auto& v){return v.id==id;});
    if(e==s.entities.end())throw std::runtime_error("Dimension reference is unavailable.");return *e;
}
struct Frame {Point2 origin,x{1,0};double scale=1;};
Frame frame(const Sketch& s,const SketchDimension& d){
    const auto a=sketchPointValue(s,d.first);
    if(d.kind==DimensionKind::CircleRadius || d.kind==DimensionKind::CircleDiameter)
        return {a,{1,0},std::max(entity(s,d.first.entityId).radius,1e-9)};
    const auto b=sketchPointValue(s,d.second);
    if(d.kind==DimensionKind::LineAngle || d.kind==DimensionKind::LineDistance){
        const auto& first=entity(s,d.first.entityId);const auto u=sub(first.second,first.first);
        if(d.kind==DimensionKind::LineDistance)return {mul(add(a,b),0.5),unit(u)};
        const auto& second=entity(s,d.second.entityId);const auto v=sub(second.second,second.first);
        const auto determinant=cross(u,v);
        const auto center=std::abs(determinant)>1e-12?add(first.first,mul(u,cross(sub(second.first,first.first),v)/determinant)):a;
        return {center,unit(d.first.role==PointRole::Second?mul(u,-1):u)};
    }
    const bool aligned=d.kind==DimensionKind::LineLength || d.kind==DimensionKind::PointDistance;
    return {mul(add(a,b),0.5),aligned?unit(sub(b,a)):Point2{1,0}};
}
}
Point2 dimensionAnnotationPosition(const Sketch& s,const SketchDimension& d){
    if(!d.placement)return d.annotation;
    const auto f=frame(s,d);return add(f.origin,mul(add(mul(f.x,d.placement->x),mul(normal(f.x),d.placement->y)),f.scale));
}
void placeDimensionAnnotation(const Sketch& s,SketchDimension& d,Point2 position){
    const auto f=frame(s,d);const auto delta=mul(sub(position,f.origin),1/f.scale);
    d.annotation=position;d.placement=Point2{dot(delta,f.x),dot(delta,normal(f.x))};
}
void attachDimensionAnnotations(Sketch& s){for(auto& d:s.dimensions)if(!d.placement)placeDimensionAnnotation(s,d,d.annotation);}
DimensionGraphics dimensionGraphics(const Sketch& s,const SketchDimension& d,double unitsPerPixel){
    DimensionGraphics g;g.text=dimensionAnnotationPosition(s,d);
    const double size=7*unitsPerPixel;
    const auto line=[&](Point2 a,Point2 b){if(length(sub(b,a))>1e-9)g.segments.push_back({a,b});};
    const auto arrow=[&](Point2 tip,Point2 direction,double available){
        const auto u=unit(direction),n=normal(u);const double h=std::min(size,available*0.25);
        line(tip,add(tip,add(mul(u,h),mul(n,h*0.4))));line(tip,add(tip,sub(mul(u,h),mul(n,h*0.4))));
    };
    const auto a=sketchPointValue(s,d.first);
    if(d.kind==DimensionKind::CircleRadius || d.kind==DimensionKind::CircleDiameter){
        const double radius=entity(s,d.first.entityId).radius;const auto u=unit(sub(g.text,a));
        const auto near=add(a,mul(u,radius)),far=sub(a,mul(u,radius));
        if(d.kind==DimensionKind::CircleDiameter){line(far,near);arrow(far,u,2*radius);arrow(near,mul(u,-1),2*radius);}
        else {line(a,near);arrow(near,mul(u,-1),radius);}
        line(near,g.text);return g;
    }
    if(d.kind==DimensionKind::LineAngle){
        const auto f=frame(s,d);const double start=std::atan2(f.x.y,f.x.x);
        const double sweep=dimensionMeasurement(s,d)*d.sense*pi/180;
        const double radius=std::max(12*unitsPerPixel,length(sub(g.text,f.origin)));
        const int count=std::max(8,int(std::ceil(std::abs(sweep)*24)));Point2 first,last;
        for(int i=0;i<=count;++i){const double angle=start+sweep*i/count;const auto p=add(f.origin,{radius*std::cos(angle),radius*std::sin(angle)});if(i)line(last,p);else first=p;last=p;}
        line(f.origin,first);line(f.origin,last);
        arrow(first,mul(normal(sub(first,f.origin)),double(d.sense)),radius*std::abs(sweep));
        arrow(last,mul(normal(sub(last,f.origin)),double(-d.sense)),radius*std::abs(sweep));
        // Keep text connected even when dragged outside the chosen angular sector.
        double t=std::atan2(g.text.y-f.origin.y,g.text.x-f.origin.x)-start;
        while(t<0)t+=2*pi;while(t>=2*pi)t-=2*pi;
        if(d.sense<0 && t>0)t-=2*pi;
        t=std::clamp(t,std::min(0.0,sweep),std::max(0.0,sweep));
        line(add(f.origin,{radius*std::cos(start+t),radius*std::sin(start+t)}),g.text);return g;
    }
    const auto b=sketchPointValue(s,d.second);Point2 aa,bb;
    if(d.kind==DimensionKind::HorizontalDistance){aa={a.x,g.text.y};bb={b.x,g.text.y};}
    else if(d.kind==DimensionKind::VerticalDistance){aa={g.text.x,a.y};bb={g.text.x,b.y};}
    else if(d.kind==DimensionKind::LineDistance){
        const auto u=frame(s,d).x,n=normal(u);aa=add(a,mul(u,dot(sub(g.text,a),u)));bb=add(aa,mul(n,dot(sub(b,a),n)));
    }else {const auto n=normal(unit(sub(b,a)));const double offset=dot(sub(g.text,a),n);aa=add(a,mul(n,offset));bb=add(b,mul(n,offset));}
    line(a,aa);line(b,bb);line(aa,bb);const auto u=sub(bb,aa);
    arrow(aa,u,length(u));arrow(bb,mul(u,-1),length(u));
    const auto axis=unit(u);const auto projection=add(aa,mul(axis,dot(sub(g.text,aa),axis)));
    const double t=dot(sub(projection,aa),axis);
    if(t<0)line(aa,projection);else if(t>length(u))line(bb,projection);
    line(projection,g.text);return g;
}
}
