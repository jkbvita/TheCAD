#include "document/ReferenceGeometryData.h"
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace thecad::document {
using namespace modeling;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void fields(const QJsonObject& object,const QSet<QString>& expected){const auto keys=object.keys();require(QSet<QString>(keys.begin(),keys.end())==expected,"Invalid reference geometry fields.");}
double number(const QJsonValue& value){require(value.isDouble()&&std::isfinite(value.toDouble())&&std::abs(value.toDouble())<=1e9,"Invalid reference geometry parameter.");return value.toDouble();}
int integer(const QJsonValue& value,int maximum){const double n=number(value);require(n>=0&&n<=maximum&&n==std::floor(n),"Invalid reference geometry index.");return int(n);}
std::string text(const QJsonValue& value){require(value.isString()&&value.toString().size()<=128,"Invalid reference geometry text.");return value.toString().toStdString();}
void id(const std::string& value){static const QRegularExpression pattern("\\A[A-Za-z0-9_-]{1,128}\\z");require(pattern.match(QString::fromStdString(value)).hasMatch(),"Invalid reference feature/entity ID.");}
gp_Pnt vectorValue(const QJsonValue& value){require(value.isArray()&&value.toArray().size()==3,"Reference vector requires three coordinates.");const auto array=value.toArray();return {number(array[0]),number(array[1]),number(array[2])};}
DatumGeometry point(gp_Pnt p){return {DatumKind::Point,gp_Ax3(p,gp_Dir(0,0,1))};}
}
std::string referenceBodySignature(const TopoDS_Shape& body){std::ostringstream stream;BRepTools::Write(body,stream,Standard_False,Standard_False,TopTools_FormatVersion_CURRENT);return QCryptographicHash::hash(QByteArray::fromStdString(stream.str()),QCryptographicHash::Sha256).toHex().toStdString();}
DatumGeometry resolveGeometryReference(const GeometryReference& ref,const std::map<std::string,DatumGeometry>& datums,const std::map<std::string,Sketch>& sketches,const std::map<std::string,TopoDS_Shape>& bodies){
    const int source=int(ref.source);require(source>=0&&source<=8,"Invalid reference geometry source.");
    require(int(ref.role)>=0&&int(ref.role)<=int(PointRole::Midpoint),"Invalid reference point role.");
    const bool builtin=source<=2,sketch=ref.source==ReferenceSource::SketchPoint||ref.source==ReferenceSource::SketchLine,body=source>=6;
    require(builtin?ref.featureId.empty():!ref.featureId.empty(),"Missing or unexpected reference feature.");
    if(!builtin)id(ref.featureId);
    require(sketch?!ref.entityId.empty():ref.entityId.empty(),"Missing or unexpected reference entity.");if(sketch)id(ref.entityId);
    require(body?!ref.signature.empty():ref.signature.empty(),"Missing or unexpected topology signature.");
    if(ref.source!=ReferenceSource::SketchPoint)require(ref.role==PointRole::First,"Unexpected reference point role.");
    if(ref.source==ReferenceSource::Origin){require(ref.index==0,"Invalid origin index.");return point({0,0,0});}
    if(ref.source==ReferenceSource::BasePlane){require(ref.index>=0&&ref.index<3,"Invalid base plane.");return {DatumKind::Plane,sketchAxes(SketchPlane(ref.index))};}
    if(ref.source==ReferenceSource::BaseAxis){require(ref.index>=0&&ref.index<3,"Invalid base axis.");return {DatumKind::Axis,gp_Ax3(gp_Pnt(0,0,0),ref.index==0?gp_Dir(1,0,0):ref.index==1?gp_Dir(0,1,0):gp_Dir(0,0,1))};}
    if(!body)require(ref.index==0,"Unexpected topology index.");
    if(ref.source==ReferenceSource::Datum){const auto found=datums.find(ref.featureId);require(found!=datums.end(),"Reference must identify an earlier reference feature.");return found->second;}
    if(sketch){const auto found=sketches.find(ref.featureId);require(found!=sketches.end(),"Reference sketch is missing or later in history.");
        if(ref.source==ReferenceSource::SketchPoint)return point(sketchPoint(found->second,sketchPointValue(found->second,{ref.entityId,ref.role})));
        const auto entity=std::find_if(found->second.entities.begin(),found->second.entities.end(),[&](const auto& e){return e.id==ref.entityId;});require(entity!=found->second.entities.end()&&entity->kind==EntityKind::Line,"Axis reference requires a sketch line.");const auto p=sketchPoint(found->second,entity->first),q=sketchPoint(found->second,entity->second);return {DatumKind::Axis,gp_Ax3(p,gp_Dir(gp_Vec(p,q)))};
    }
    const auto found=bodies.find(ref.featureId);require(found!=bodies.end(),"Reference body is missing or later in history.");require(referenceBodySignature(found->second)==ref.signature,"Referenced solid topology changed. Reselect the intended support; automatic reattachment is disabled.");
    const auto type=ref.source==ReferenceSource::BodyVertex?TopAbs_VERTEX:ref.source==ReferenceSource::BodyEdge?TopAbs_EDGE:TopAbs_FACE;
    TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(found->second,type,shapes);require(ref.index>0&&ref.index<=shapes.Extent(),"Referenced topology is unavailable.");
    if(type==TopAbs_VERTEX)return point(BRep_Tool::Pnt(TopoDS::Vertex(shapes(ref.index))));
    if(type==TopAbs_EDGE){BRepAdaptor_Curve curve(TopoDS::Edge(shapes(ref.index)));require(curve.GetType()==GeomAbs_Line,"Reference axis requires a straight edge.");return {DatumKind::Axis,gp_Ax3(curve.Line().Location(),curve.Line().Direction())};}
    const auto face=TopoDS::Face(shapes(ref.index));BRepAdaptor_Surface surface(face);
    if(surface.GetType()==GeomAbs_Plane)return {DatumKind::Plane,planarFaceAxes(face)};
    if(surface.GetType()==GeomAbs_Cylinder)return {DatumKind::Axis,surface.Cylinder().Position()};
    if(surface.GetType()==GeomAbs_Cone)return {DatumKind::Axis,surface.Cone().Position()};
    throw std::runtime_error("Select a planar, cylindrical or conical reference face.");
}
QJsonObject writeDatumDefinition(const DatumDefinition& d){QJsonArray refs;for(const auto& r:d.references)refs.append(QJsonObject{{"source",int(r.source)},{"feature",QString::fromStdString(r.featureId)},{"entity",QString::fromStdString(r.entityId)},{"role",int(r.role)},{"index",r.index},{"signature",QString::fromStdString(r.signature)}});return {{"method",datumMethodKey(d.method)},{"references",refs},{"position",QJsonArray{d.position.X(),d.position.Y(),d.position.Z()}},{"direction",QJsonArray{d.direction.X(),d.direction.Y(),d.direction.Z()}},{"value",d.value}};}
DatumDefinition readDatumDefinition(const QJsonObject& o){fields(o,{"method","references","position","direction","value"});DatumDefinition d;d.method=datumMethodFromKey(text(o["method"]));d.position=vectorValue(o["position"]);d.direction=gp_Vec(vectorValue(o["direction"]).XYZ());d.value=number(o["value"]);require(o["references"].isArray()&&o["references"].toArray().size()<=3,"Invalid reference list.");for(const auto& value:o["references"].toArray()){require(value.isObject(),"Invalid geometry reference.");const auto r=value.toObject();fields(r,{"source","feature","entity","role","index","signature"});GeometryReference ref;ref.source=ReferenceSource(integer(r["source"],8));ref.featureId=text(r["feature"]);ref.entityId=text(r["entity"]);ref.role=PointRole(integer(r["role"],int(PointRole::Midpoint)));ref.index=integer(r["index"],1000000);ref.signature=text(r["signature"]);d.references.push_back(ref);}return d;}
DatumGeometry PartDocument::resolveReference(const GeometryReference& ref) const{std::map<std::string,DatumGeometry> datums;std::map<std::string,Sketch> sketches;for(const auto& feature:m_features){if(feature.kind==FeatureKind::ReferenceGeometry)datums.emplace(feature.id,feature.datum);if(feature.kind==FeatureKind::Sketch)sketches.emplace(feature.id,feature.sketch);}return resolveGeometryReference(ref,datums,sketches,m_bodies);}
std::string PartDocument::addReferenceGeometry(const DatumDefinition& definition){Feature feature;feature.kind=FeatureKind::ReferenceGeometry;feature.id=nextId(feature.kind);feature.reference=definition;const auto kind=datumKind(definition.method);const auto count=std::count_if(m_features.begin(),m_features.end(),[&](const auto& f){return f.kind==FeatureKind::ReferenceGeometry&&f.datum.kind==kind;});feature.name=std::string(kind==DatumKind::Point?"Point":kind==DatumKind::Axis?"Axis":"Plane")+std::to_string(count+1);auto features=m_features;features.push_back(feature);apply(std::move(features));return feature.id;}
Sketch PartDocument::sketchOnReferencePlane(const std::string& id) const{GeometryReference ref;ref.source=ReferenceSource::Datum;ref.featureId=id;const auto datum=resolveReference(ref);require(datum.kind==DatumKind::Plane,"Sketch support must be a reference plane.");Sketch sketch;sketch.referencePlaneId=id;sketch.referencePlaneFrame=datum.frame;return sketch;}
PartDocument PartDocument::beforeFeature(const std::string& id) const{const auto found=std::find_if(m_features.begin(),m_features.end(),[&](const auto& f){return f.id==id;});require(found!=m_features.end(),"Feature is unavailable.");PartDocument result;result.apply(std::vector<Feature>(m_features.begin(),found));return result;}
std::vector<ReferenceOption> PartDocument::referenceOptions() const{
    std::map<std::string,DatumGeometry> datums;std::map<std::string,Sketch> sketches;for(const auto& feature:m_features){if(feature.kind==FeatureKind::ReferenceGeometry)datums.emplace(feature.id,feature.datum);if(feature.kind==FeatureKind::Sketch)sketches.emplace(feature.id,feature.sketch);}
    std::vector<ReferenceOption> result;const auto add=[&](std::string label,GeometryReference ref){try{
        const auto geometry=resolveGeometryReference(ref,datums,sketches,m_bodies);TopoDS_Shape shape=datumDisplayShape(geometry,50);
        if(int(ref.source)>=6){TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(m_bodies.at(ref.featureId),ref.source==ReferenceSource::BodyVertex?TopAbs_VERTEX:ref.source==ReferenceSource::BodyEdge?TopAbs_EDGE:TopAbs_FACE,shapes);shape=shapes(ref.index);}
        if(ref.source==ReferenceSource::SketchLine){const auto& sketch=std::find_if(m_features.begin(),m_features.end(),[&](const auto& f){return f.id==ref.featureId;})->sketch;const auto& e=*std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==ref.entityId;});shape=BRepBuilderAPI_MakeEdge(sketchPoint(sketch,e.first),sketchPoint(sketch,e.second));}
        result.push_back({std::move(label),ref,geometry,shape});
    }catch(const Standard_Failure&){}catch(const std::exception&){}};
    add("Origin",{});for(int i=0;i<3;++i){GeometryReference p;p.source=ReferenceSource::BasePlane;p.index=i;add(i==0?"Front plane":i==1?"Top plane":"Right plane",p);p.source=ReferenceSource::BaseAxis;add(i==0?"X axis":i==1?"Y axis":"Z axis",p);}
    std::string bodyId;
    for(const auto& feature:m_features){
        if(isSolidFeature(feature.kind))bodyId=feature.id;
        if(feature.kind==FeatureKind::ReferenceGeometry){GeometryReference ref;ref.source=ReferenceSource::Datum;ref.featureId=feature.id;add(feature.name,ref);}
        if(feature.kind==FeatureKind::Sketch){for(const auto& point:sketchPoints(feature.sketch)){if(point.first.role==PointRole::Origin)continue;GeometryReference ref;ref.source=ReferenceSource::SketchPoint;ref.featureId=feature.id;ref.entityId=point.first.entityId;ref.role=point.first.role;add(feature.name+" / "+ref.entityId+" / "+(ref.role==PointRole::Center?"center":ref.role==PointRole::Second?"end":"start"),ref);}for(const auto& e:feature.sketch.entities)if(e.kind==EntityKind::Line){GeometryReference ref;ref.source=ReferenceSource::SketchLine;ref.featureId=feature.id;ref.entityId=e.id;add(feature.name+" / "+e.id,ref);ref.source=ReferenceSource::SketchPoint;ref.role=PointRole::Midpoint;add(feature.name+" / "+e.id+" / midpoint",ref);}}
    }
    if(!bodyId.empty()){const auto body=m_bodies.at(bodyId);const auto signature=referenceBodySignature(body);for(const auto type:{TopAbs_VERTEX,TopAbs_EDGE,TopAbs_FACE}){TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(body,type,shapes);for(int i=1;i<=shapes.Extent();++i){GeometryReference ref;ref.source=type==TopAbs_VERTEX?ReferenceSource::BodyVertex:type==TopAbs_EDGE?ReferenceSource::BodyEdge:ReferenceSource::BodyFace;ref.featureId=bodyId;ref.index=i;ref.signature=signature;add(std::string(type==TopAbs_VERTEX?"Vertex ":type==TopAbs_EDGE?"Edge ":"Face ")+std::to_string(i),ref);}}}
    return result;
}
}
