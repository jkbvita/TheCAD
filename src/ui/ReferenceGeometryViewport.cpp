#include "presentation/SelectionAppearance.h"
#include "ui/Theme.h"
#include "ui/CadViewport.h"
#include <AIS_Point.hxx>
#include <AIS_TextLabel.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <algorithm>
namespace thecad::ui {
using namespace presentation;
namespace {
int pick(const std::vector<ReferencePickItem>& items,std::optional<modeling::DatumKind> filter,const QPointF& position,const Handle(V3d_View)& view,double tolerance,double ratio){
    int best=-1,bestPriority=3,bestSource=4;double bestDistance=tolerance,bestDepth=1e100;
    const auto project=[&](const gp_Pnt& point){int x,y;view->Convert(point.X(),point.Y(),point.Z(),x,y);return QPointF(x/ratio,y/ratio);};
    double x,y,z,dx,dy,dz;view->ConvertWithProj(qRound(position.x()*ratio),qRound(position.y()*ratio),x,y,z,dx,dy,dz);const gp_Dir direction(dx,dy,dz);const gp_Pnt projected(x,y,z);const double eyeDepth=gp_Vec(view->Camera()->Eye(),projected).Dot(gp_Vec(direction));const gp_Lin ray(projected.Translated(-gp_Vec(direction)*eyeDepth),direction);
    for(int i=0;i<int(items.size());++i){const auto& item=items[std::size_t(i)];if(filter&&item.geometry.kind!=*filter)continue;const int priority=int(item.geometry.kind);double distance=0,depth=0;
        try{
            if(item.geometry.kind==modeling::DatumKind::Point&&item.reference.source!=modeling::ReferenceSource::BodyCircleCenter){const auto delta=project(item.geometry.frame.Location())-position;distance=std::hypot(delta.x(),delta.y());depth=item.geometry.frame.Location().Distance(view->Camera()->Eye());if(distance>tolerance)continue;}
            else if(!item.shape.IsNull()&&item.shape.ShapeType()==TopAbs_EDGE){
                BRepAdaptor_Curve curve(TopoDS::Edge(item.shape));const double first=curve.FirstParameter(),last=curve.LastParameter();const auto center=item.geometry.frame.Location();
                // Lines use their exact projected segment. Circular edges are
                // sampled finely in screen space, including the closed seam.
                int segments=1;if(curve.GetType()==GeomAbs_Circle){const auto c=project(curve.Circle().Location());double radius=1;for(int k=0;k<4;++k){const auto r=project(curve.Value(first+(last-first)*k/4));radius=std::max(radius,std::hypot(r.x()-c.x(),r.y()-c.y()));}segments=std::clamp(int(std::ceil(6.284*std::sqrt(std::max(1.0,radius)))),64,4096);}
                distance=1e100;auto p=curve.Value(first);for(int j=1;j<=segments;++j){const auto q=curve.Value(first+(last-first)*j/segments);const auto a=project(p),v=project(q)-a;const double square=QPointF::dotProduct(v,v),t=square>1e-12?std::clamp(QPointF::dotProduct(position-a,v)/square,0.0,1.0):0;const auto delta=position-a-v*t;const double d=std::hypot(delta.x(),delta.y());if(d<distance){distance=d;depth=p.Translated(gp_Vec(p,q)*t).Distance(view->Camera()->Eye());}p=q;}
                if(item.geometry.kind==modeling::DatumKind::Point){const auto delta=project(center)-position;const double d=std::hypot(delta.x(),delta.y());if(d<distance){distance=d;depth=center.Distance(view->Camera()->Eye());}}if(distance>tolerance)continue;
            }
            else{IntCurvesFace_ShapeIntersector intersector;intersector.Load(item.shape,1e-7);intersector.Perform(ray,0,1e15);if(!intersector.IsDone()||intersector.NbPnt()==0)continue;depth=1e100;for(int p=1;p<=intersector.NbPnt();++p)depth=std::min(depth,intersector.Pnt(p).Distance(view->Camera()->Eye()));}
            const int source=item.reference.source==modeling::ReferenceSource::Datum?0:int(item.reference.source)>=6?1:int(item.reference.source)>=4?2:3;
            if(priority<bestPriority || (priority==bestPriority && (distance<bestDistance-0.25 || (std::abs(distance-bestDistance)<=0.25 && (depth<bestDepth-1e-7 || (std::abs(depth-bestDepth)<=1e-7&&source<bestSource)))))){best=i;bestPriority=priority;bestDistance=distance;bestDepth=depth;bestSource=source;}
        }catch(const Standard_Failure&){}
    }
    return best;
}
}
void CadViewport::setDatums(const std::vector<DatumVisual>& datums){
    m_datums=datums;if(!isReady())return;
    for(auto& item:m_datumPresentations)m_graphics.context()->Remove(item,Standard_False);m_datumPresentations.clear();
    if(std::none_of(datums.begin(),datums.end(),[&](const auto& d){return d.id==m_selectedDatum;}))m_selectedDatum.clear();
    for(const auto& datum:datums){
        Handle(AIS_InteractiveObject) presentation;
        if(datum.geometry.kind==modeling::DatumKind::Point){auto marker=new AIS_Point(new Geom_CartesianPoint(datum.geometry.frame.Location()));marker->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_POINT,viewportColor(ThemeColor::Defined),7*devicePixelRatioF()));presentation=marker;m_graphics.context()->Display(presentation,0,-1,Standard_False);}
        else{auto shape=new AIS_Shape(modeling::datumDisplayShape(datum.geometry,50));presentation=shape;
            if(datum.geometry.kind==modeling::DatumKind::Plane){styleReferencePlane(shape,false,false,int(m_datumPresentations.size()/2));m_graphics.context()->Display(shape,AIS_Shaded,-1,Standard_False);}
            else{shape->SetWidth(1.5*devicePixelRatioF());shape->Attributes()->SetWireAspect(new Prs3d_LineAspect(viewportColor(ThemeColor::Defined),Aspect_TOL_DOTDASH,1.5*devicePixelRatioF()));m_graphics.context()->Display(shape,AIS_WireFrame,-1,Standard_False);}
        }
        if(datum.geometry.kind!=modeling::DatumKind::Plane)presentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
        m_datumPresentations.push_back(presentation);
        Handle(AIS_TextLabel) label=new AIS_TextLabel;label->SetText(TCollection_ExtendedString(datum.name.c_str()));
        auto position=datum.geometry.frame.Location();
        if(datum.geometry.kind==modeling::DatumKind::Axis)position.Translate(gp_Vec(datum.geometry.frame.Direction())*40);
        else if(datum.geometry.kind==modeling::DatumKind::Plane){position.Translate((gp_Vec(datum.geometry.frame.XDirection())+gp_Vec(datum.geometry.frame.YDirection()))*50);label->SetOrientation3D(gp_Ax2(position,datum.geometry.frame.Direction(),datum.geometry.frame.XDirection()));label->SetFlipping(true);label->SetHJustification(Graphic3d_HTA_RIGHT);label->SetVJustification(Graphic3d_VTA_TOP);}
        else{const auto up=gp_Vec(m_graphics.view()->Camera()->Up()),right=gp_Vec(m_graphics.view()->Camera()->Direction()).Crossed(up);position.Translate((up+right)*(6*m_graphics.view()->Camera()->Scale()/std::max(1,height())));}
        label->SetPosition(position);label->SetHeight(13*devicePixelRatioF());label->SetColor(viewportColor(ThemeColor::Reference));label->SetInfiniteState(true);m_graphics.context()->Display(label,0,-1,Standard_False);m_datumPresentations.push_back(label);
    }
    updateDatumStyles();for(std::size_t i=0;i<m_datums.size();++i)if(m_sketchEditing||!m_datums[i].visible){m_graphics.context()->Erase(m_datumPresentations[i*2],Standard_False);m_graphics.context()->Erase(m_datumPresentations[i*2+1],Standard_False);}update();
}
void CadViewport::setSelectedDatum(const std::string& id){m_selectedDatum=id;updateDatumStyles();update();}
void CadViewport::updateDatumStyles(){if(!isReady())return;for(std::size_t i=0;i<m_datums.size();++i){if(i*2>=m_datumPresentations.size())break;const auto& d=m_datums[i];const bool selected=d.id==m_selectedDatum||std::any_of(m_selection.items().begin(),m_selection.items().end(),[&](const auto& r){return !modeling::isTopology(r.kind)&&r.featureId==d.id;}),hovered=d.id==m_hoveredDatum;const Quantity_Color color=selected?selectionBlue():hovered?hoverOrange():viewportColor(d.geometry.kind==modeling::DatumKind::Plane?ThemeColor::Reference:ThemeColor::Defined);auto item=m_datumPresentations[i*2];item->SetColor(color);if(d.geometry.kind==modeling::DatumKind::Axis)item->Attributes()->SetWireAspect(new Prs3d_LineAspect(color,Aspect_TOL_DOTDASH,1.5*devicePixelRatioF()));if(d.geometry.kind==modeling::DatumKind::Plane)styleReferencePlane(Handle(AIS_Shape)::DownCast(item),selected,hovered,int(i));m_graphics.context()->Redisplay(item,Standard_False);}}
std::vector<std::string> CadViewport::datumIdsAt(const QPointF& position){std::vector<std::string> ids;if(!isReady()||!m_selectionPolicy.allows(modeling::SelectionKind::ReferencePoint))return ids;for(const auto& d:m_datums)if(d.visible&&pick({{{},d.geometry,modeling::datumDisplayShape(d.geometry,50)}},{},position,m_graphics.view(),m_pickingTolerance,devicePixelRatioF())>=0)ids.push_back(d.id);return ids;}
std::string CadViewport::datumAt(const QPointF& position,std::optional<modeling::DatumKind> kind){if(!isReady()||!m_selectionPolicy.allows(modeling::SelectionKind::ReferencePoint))return {};std::vector<ReferencePickItem> items;std::vector<std::string> ids;for(const auto& d:m_datums)if(d.visible){items.push_back({{},d.geometry,modeling::datumDisplayShape(d.geometry,50)});ids.push_back(d.id);}const int index=pick(items,kind,position,m_graphics.view(),m_pickingTolerance,devicePixelRatioF());return index<0?"":ids[std::size_t(index)];}
void CadViewport::setReferenceCandidates(const std::vector<ReferencePickItem>& candidates,std::optional<modeling::DatumKind> kind){
    m_referenceCandidates=candidates;m_referenceCandidateKind=kind;if(!isReady())return;
    for(auto& item:m_referenceCandidates)if(item.reference.source==modeling::ReferenceSource::BasePlane)item.shape=modeling::datumDisplayShape(item.geometry,m_planeHalfExtent);
    for(auto& item:m_referenceCandidatePresentations)m_graphics.context()->Remove(item,Standard_False);m_referenceCandidatePresentations.clear();
    if(!m_referenceHover.IsNull()){m_graphics.context()->Remove(m_referenceHover,Standard_False);m_referenceHover.Nullify();}
    for(const auto& item:candidates){if((kind&&item.geometry.kind!=*kind)||item.geometry.kind==modeling::DatumKind::Plane)continue;Handle(AIS_InteractiveObject) presentation;
        if(item.geometry.kind==modeling::DatumKind::Point){auto marker=new AIS_Point(new Geom_CartesianPoint(item.geometry.frame.Location()));marker->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_POINT,viewportColor(ThemeColor::Reference),6*devicePixelRatioF()));presentation=marker;m_graphics.context()->Display(marker,0,-1,Standard_False);}
        else{presentation=new AIS_Shape(item.shape);presentation->SetColor(viewportColor(ThemeColor::Reference));m_graphics.context()->Display(presentation,AIS_WireFrame,-1,Standard_False);}
        presentation->SetZLayer(Graphic3d_ZLayerId_Topmost);m_referenceCandidatePresentations.push_back(presentation);
    }
    update();
}
int CadViewport::referenceCandidateAt(const QPointF& position){
    if(!isReady()||m_referenceCandidates.empty())return -1;std::vector<ReferencePickItem> items;std::vector<int> indices;
    for(int i=0;i<int(m_referenceCandidates.size());++i){const auto& item=m_referenceCandidates[std::size_t(i)];using K=modeling::SelectionKind;using S=modeling::ReferenceSource;
        const auto kind=item.reference.source==S::BodyFace?K::Face:(item.reference.source==S::BodyEdge||item.reference.source==S::BodyCircleCenter)?K::Edge:item.reference.source==S::BodyVertex?K::Vertex:item.reference.source==S::SketchPoint?K::SketchPoint:item.reference.source==S::SketchLine?K::SketchEntity:K::ReferencePoint;
        if(m_selectionPolicy.allows(kind)){items.push_back(item);indices.push_back(i);}}
    const int index=pick(items,m_referenceCandidateKind,position,m_graphics.view(),m_pickingTolerance,devicePixelRatioF());return index<0?-1:indices[std::size_t(index)];
}
void CadViewport::updateReferenceHover(const QPointF& position){
    if(!m_referenceHover.IsNull()){m_graphics.context()->Remove(m_referenceHover,Standard_False);m_referenceHover.Nullify();}
    if(!m_referenceCandidates.empty()){const int index=referenceCandidateAt(position);if(index>=0){m_referenceHover=new AIS_Shape(selectionOutline(m_referenceCandidates[std::size_t(index)].shape));m_referenceHover->SetDisplayMode(AIS_WireFrame);m_referenceHover->SetColor(hoverOrange());m_referenceHover->SetWidth(3*devicePixelRatioF());m_referenceHover->SetZLayer(Graphic3d_ZLayerId_Topmost);m_graphics.context()->Display(m_referenceHover,AIS_WireFrame,-1,Standard_False);}update();return;}
    const auto hover=datumAt(position);if(hover!=m_hoveredDatum){m_hoveredDatum=hover;updateDatumStyles();update();}
}
}
