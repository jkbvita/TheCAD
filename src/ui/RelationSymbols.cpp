#include "ui/CadViewport.h"
#include "ui/RelationIcons.h"
#include "modeling/RelationCommands.h"
#include <Image_PixMap.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
namespace thecad::ui {
namespace {
Handle(Prs3d_PointAspect) sprite(modeling::RelationKind kind,int pixels,bool hover,bool selected){
    using Key=std::tuple<int,int,bool,bool>;static std::map<Key,Handle(Prs3d_PointAspect)> cache;const Key key{int(kind),pixels,hover,selected};if(const auto i=cache.find(key);i!=cache.end())return i->second;
    const auto image=relationArtwork(kind,pixels,true,hover,selected);Handle(Image_PixMap) data=new Image_PixMap;data->InitZero(Image_Format_RGBA,pixels,pixels);
    for(int y=0;y<pixels;++y)for(int x=0;x<pixels;++x){const auto c=image.pixelColor(x,y);data->SetPixelColor(x,y,Quantity_ColorRGBA(Quantity_Color(c.redF(),c.greenF(),c.blueF(),Quantity_TOC_RGB),float(c.alphaF())));}
    Handle(Prs3d_PointAspect) result=new Prs3d_PointAspect(new Graphic3d_AspectMarker3d(data));cache.emplace(key,result);return result;
}
modeling::Point2 anchor(const modeling::Sketch& sketch,const modeling::SketchRelation& r){
    const auto first=modeling::sketchPointValue(sketch,r.first);
    const auto e=std::find_if(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==r.first.entityId;});
    if(e!=sketch.entities.end()){
        if(e->kind==modeling::EntityKind::Circle)return {e->first.x+0.7071067811865475*e->radius,e->first.y+0.7071067811865475*e->radius};
        if(e->kind==modeling::EntityKind::Line && r.kind!=modeling::RelationKind::Coincident && r.kind!=modeling::RelationKind::PointOnLine && r.kind!=modeling::RelationKind::Midpoint)return {(e->first.x+e->second.x)/2,(e->first.y+e->second.y)/2};
    }
    if(r.kind==modeling::RelationKind::Horizontal || r.kind==modeling::RelationKind::Vertical){const auto second=modeling::sketchPointValue(sketch,r.second);return {(first.x+second.x)/2,(first.y+second.y)/2};}
    return first;
}
}
void CadViewport::updateRelationSymbols(){
    if(!isReady())return;for(auto& marker:m_relationMarkers)m_context->Remove(marker,Standard_False);m_relationMarkers.clear();m_relationHits.clear();
    if(!m_showRelations || !m_sketchEditing || !m_sketch)return;
    const int pixels=std::max(1,qRound(18*devicePixelRatioF()));std::vector<QRectF> placed;
    for(const auto& group:modeling::relationGroups(*m_sketch)){
        const auto& relation=*std::find_if(m_sketch->relations.begin(),m_sketch->relations.end(),[&](const auto& r){return r.id==group.id;});
        const bool perEntity=group.kind==modeling::RelationKind::Equal || group.kind==modeling::RelationKind::EqualCurveLength;
        const auto entities=perEntity?group.entityIds:std::vector<std::string>{relation.first.entityId};
        for(const auto& entityId:entities){
        auto r=relation;if(perEntity){const auto e=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==entityId;});r.first={entityId,e->kind==modeling::EntityKind::Circle?modeling::PointRole::Center:modeling::PointRole::First};}
        const auto origin=screenPoint(anchor(*m_sketch,r));if(!rect().adjusted(-20,-20,20,20).contains(origin.toPoint()))continue;
        QRectF area;bool found=false;
        for(int attempt=0;attempt<120;++attempt){
            const int column=attempt%8,row=attempt/8;const double dx=16+column*21,dy=-16-row*21;
            QPointF center=origin+QPointF(dx,dy);if(center.x()>width()-11)center.setX(origin.x()-dx);if(center.y()<11)center.setY(origin.y()-dy);
            center.setX(std::clamp(center.x(),11.0,std::max(11.0,width()-11.0)));center.setY(std::clamp(center.y(),11.0,std::max(11.0,height()-11.0)));area=QRectF(center-QPointF(9,9),QSizeF(18,18));
            if(std::any_of(placed.begin(),placed.end(),[&](const auto& old){return old.adjusted(-2,-2,2,2).intersects(area);}))continue;
            if(attempt<100 && (entityAt(center) || pointAt(center) || dimensionAt(center)))continue;found=true;break;
        }
        if(!found)continue;const auto model=sketchPosition(area.center());if(!model)continue;
        Handle(AIS_Point) marker=new AIS_Point(new Geom_CartesianPoint(modeling::sketchPoint(*m_sketch,*model)));
        marker->Attributes()->SetPointAspect(sprite(r.kind,pixels,r.id==m_hoveredRelation,r.id==m_selectedRelation));marker->SetZLayer(Graphic3d_ZLayerId_Topmost);marker->SetInfiniteState(Standard_True);
        m_context->Display(marker,0,-1,Standard_False);m_context->SetDisplayPriority(marker,Graphic3d_DisplayPriority_Topmost);m_relationMarkers.push_back(marker);m_relationHits.push_back({group.id,area,entityId});placed.push_back(area);
        }
    }
}
std::optional<std::string> CadViewport::relationAt(const QPointF& position) const {
    if(!m_showRelations)return {};for(const auto& hit:m_relationHits)if(hit.bounds.adjusted(-2,-2,2,2).contains(position))return hit.id;return {};
}
void CadViewport::showSketchRelations(bool visible){m_showRelations=visible;m_hoveredRelation.clear();if(!visible)m_selectedRelation.clear();updateRelationSymbols();update();}
void CadViewport::selectSketchRelation(const std::string& id){
    if(!m_sketch)return;const auto r=std::find_if(m_sketch->relations.begin(),m_sketch->relations.end(),[&](const auto& r){return r.id==id;});if(r==m_sketch->relations.end())return;
    const auto groups=modeling::relationGroups(*m_sketch);const auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& g){return std::find(g.relationIds.begin(),g.relationIds.end(),id)!=g.relationIds.end();});
    m_selectedRelation=group->id;m_selectedEntities=group->entityIds;m_selectedDimensions.clear();m_selectedPoint.reset();
    m_selectedEntity=m_selectedEntities.empty()?"":m_selectedEntities.front();emit sketchEntitySelected(QString::fromStdString(m_selectedEntity));emit sketchRelationSelected(QString::fromStdString(m_selectedRelation));emit sketchSelectionChanged();updatePointPresentation();updateConstraintPresentation();update();
}
}
