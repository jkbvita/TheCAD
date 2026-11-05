#include "modeling/ReferenceGeometry.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <stdexcept>
namespace thecad::modeling {
namespace {
constexpr const char* keys[]={"point-xyz","point-on-reference","point-axis-plane","point-two-axes","point-projection","axis-xyz","axis-on-reference","axis-two-points","axis-two-planes","axis-normal-to-plane","plane-xyz","plane-offset","plane-three-points","plane-at-angle","plane-through-point"};
constexpr const char* names[]={"XYZ coordinates","On point / vertex","Axis and plane intersection","Two axes intersection","Project point onto plane","XYZ position and direction","Line / edge / face / axis","Two points / vertices","Two planes intersection","Point and normal to plane","XYZ position and normal","Offset from plane / face","Through three points","At angle about axis","Parallel plane through point"};
int methodIndex(DatumMethod method){const int index=int(method);if(index<0||index>=15)throw std::runtime_error("Unsupported reference geometry method.");return index;}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void bounded(double value){require(std::isfinite(value)&&std::abs(value)<=1e9,"Reference coordinates and parameters must be finite and within 1e9.");}
DatumGeometry point(gp_Pnt p){return {DatumKind::Point,gp_Ax3(p,gp_Dir(0,0,1))};}
DatumGeometry axis(gp_Pnt p,gp_Vec direction){require(direction.Magnitude()>1e-9,"Axis direction must not be zero.");return {DatumKind::Axis,gp_Ax3(p,gp_Dir(direction))};}
}
DatumKind datumKind(DatumMethod method){const int index=methodIndex(method);return index<5?DatumKind::Point:index<10?DatumKind::Axis:DatumKind::Plane;}
const char* datumMethodName(DatumMethod method){return names[methodIndex(method)];}
const char* datumMethodKey(DatumMethod method){return keys[methodIndex(method)];}
DatumMethod datumMethodFromKey(const std::string& key){for(int i=0;i<15;++i)if(key==keys[i])return DatumMethod(i);throw std::runtime_error("Unsupported reference geometry method.");}
std::vector<DatumKind> datumReferenceKinds(DatumMethod method){using K=DatumKind;switch(method){
case DatumMethod::PointXYZ:case DatumMethod::AxisXYZ:case DatumMethod::PlaneXYZ:return {};
case DatumMethod::PointOnReference:return {K::Point};
case DatumMethod::PointAxisPlane:return {K::Axis,K::Plane};
case DatumMethod::PointTwoAxes:return {K::Axis,K::Axis};
case DatumMethod::PointProjection:return {K::Point,K::Plane};
case DatumMethod::AxisOnReference:return {K::Axis};
case DatumMethod::AxisTwoPoints:return {K::Point,K::Point};
case DatumMethod::AxisTwoPlanes:return {K::Plane,K::Plane};
case DatumMethod::AxisNormalToPlane:return {K::Point,K::Plane};
case DatumMethod::PlaneOffset:return {K::Plane};
case DatumMethod::PlaneThreePoints:return {K::Point,K::Point,K::Point};
case DatumMethod::PlaneAtAngle:return {K::Plane,K::Axis};
case DatumMethod::PlaneThroughPoint:return {K::Plane,K::Point};
}throw std::runtime_error("Unsupported reference geometry method.");}
DatumGeometry evaluateDatum(const DatumDefinition& definition,const std::vector<DatumGeometry>& references){
    try{
        bounded(definition.position.X());bounded(definition.position.Y());bounded(definition.position.Z());bounded(definition.direction.X());bounded(definition.direction.Y());bounded(definition.direction.Z());bounded(definition.value);
        const auto kinds=datumReferenceKinds(definition.method);require(references.size()==kinds.size()&&definition.references.size()==kinds.size(),"Select every required reference.");
        for(std::size_t i=0;i<kinds.size();++i)require(references[i].kind==kinds[i],"Selected reference has the wrong geometry type.");
        const auto position=[&](int i){return references.at(std::size_t(i)).frame.Location();};
        const auto direction=[&](int i){return gp_Vec(references.at(std::size_t(i)).frame.Direction());};
        DatumGeometry result;
        switch(definition.method){
        case DatumMethod::PointXYZ:result=point(definition.position);break;
        case DatumMethod::PointOnReference:result=references[0];break;
        case DatumMethod::PointAxisPlane:{const auto d=direction(0),n=direction(1);const double divisor=d.Dot(n);require(std::abs(divisor)>1e-10,"Axis is parallel to the plane; there is no unique intersection.");result=point(position(0).Translated(d*(gp_Vec(position(0),position(1)).Dot(n)/divisor)));break;}
        case DatumMethod::PointTwoAxes:{const auto u=direction(0),v=direction(1),w=gp_Vec(position(0),position(1));const double c=u.Dot(v),denominator=1-c*c;require(denominator>1e-12,"Axes are parallel or coincident; there is no unique intersection.");const double t=(w.Dot(u)-c*w.Dot(v))/denominator;const auto p=position(0).Translated(u*t);require(gp_Vec(position(1),p).Crossed(v).Magnitude()<1e-7,"Axes are skew and do not intersect.");result=point(p);break;}
        case DatumMethod::PointProjection:{const auto p=position(0);result=point(p.Translated(direction(1)*gp_Vec(p,position(1)).Dot(direction(1))));break;}
        case DatumMethod::AxisXYZ:result=axis(definition.position,definition.direction);break;
        case DatumMethod::AxisOnReference:result=references[0];break;
        case DatumMethod::AxisTwoPoints:result=axis(position(0),gp_Vec(position(0),position(1)));break;
        case DatumMethod::AxisTwoPlanes:{const auto n=direction(0),m=direction(1),d=n.Crossed(m);const double square=d.SquareMagnitude();require(square>1e-12,"Planes are parallel or coincident; there is no unique axis.");const double a=gp_Vec(position(0).XYZ()).Dot(n),b=gp_Vec(position(1).XYZ()).Dot(m);const auto p=(m.Crossed(d)*a+d.Crossed(n)*b)/square;result=axis(gp_Pnt(p.XYZ()),d);break;}
        case DatumMethod::AxisNormalToPlane:result=axis(position(0),direction(1));break;
        case DatumMethod::PlaneXYZ:require(definition.direction.Magnitude()>1e-9,"Plane normal must not be zero.");result={DatumKind::Plane,gp_Ax3(definition.position,gp_Dir(definition.direction))};break;
        case DatumMethod::PlaneOffset:result=references[0];result.frame.SetLocation(position(0).Translated(direction(0)*definition.value));break;
        case DatumMethod::PlaneThreePoints:{const auto x=gp_Vec(position(0),position(1)),y=gp_Vec(position(0),position(2));require(x.Magnitude()>1e-9&&y.Magnitude()>1e-9&&x.Crossed(y).Magnitude()>1e-10*x.Magnitude()*y.Magnitude(),"Three plane points must be distinct and non-collinear.");result={DatumKind::Plane,gp_Ax3(position(0),gp_Dir(x.Crossed(y)),gp_Dir(x))};break;}
        case DatumMethod::PlaneAtAngle:{require(std::abs(direction(0).Dot(direction(1)))<1e-9,"Rotation axis must be parallel to the source plane.");result=references[0];const auto n=direction(0);result.frame.SetLocation(result.frame.Location().Translated(n*gp_Vec(position(0),position(1)).Dot(n)));gp_Trsf rotation;rotation.SetRotation(gp_Ax1(position(1),gp_Dir(direction(1))),definition.value*std::acos(-1.0)/180);result.frame.Transform(rotation);break;}
        case DatumMethod::PlaneThroughPoint:result=references[0];result.frame.SetLocation(position(1));break;
        }
        bounded(result.frame.Location().X());bounded(result.frame.Location().Y());bounded(result.frame.Location().Z());return result;
    }catch(const Standard_Failure& error){throw std::runtime_error(error.GetMessageString());}
}
TopoDS_Shape datumDisplayShape(const DatumGeometry& datum,double extent){
    if(!std::isfinite(extent)||extent<=0||extent>1e9)throw std::runtime_error("Invalid reference display extent.");
    if(datum.kind==DatumKind::Point)return BRepBuilderAPI_MakeVertex(datum.frame.Location());
    if(datum.kind==DatumKind::Axis){const auto d=gp_Vec(datum.frame.Direction())*extent;return BRepBuilderAPI_MakeEdge(datum.frame.Location().Translated(-d),datum.frame.Location().Translated(d));}
    return BRepBuilderAPI_MakeFace(gp_Pln(datum.frame),-extent,extent,-extent,extent);
}
}
