#include "ui/CadViewport.h"
#include "modeling/SketchSplines.h"
#include <QUuid>
#include <QLineF>
#include <cmath>
#include <exception>

namespace thecad::ui {
void CadViewport::pickSpline(const QPointF& position)
{
    if(!sketchPosition(position))return;
    CurvedPick pick; pick.position=drawingPoint(position,pick.reference); pick.boundary=m_boundarySnap;
    if(!m_curvedPicks.empty() && std::hypot(pick.position.x-m_curvedPicks.back().position.x,
        pick.position.y-m_curvedPicks.back().position.y)<=1e-6)return;
    if(m_curvedPicks.size()>=128){emit sketchInteractionError("Finish the spline before adding more than 128 fit points.");return;}
    const auto start=m_curvedPicks.empty()?pick.position:m_curvedPicks.front().position;
    const bool close=m_curvedPicks.size()>=3 && QLineF(screenPoint(start),screenPoint(pick.position)).length()<=m_pickingTolerance;
    if(close){pick.position=start;pick.reference.reset();pick.boundary.reset();}
    m_curvedPicks.push_back(std::move(pick));
    if(close)finishSpline(); else previewSpline(position);
}
void CadViewport::previewSpline(const QPointF& position)
{
    m_curvedPreview.clear();
    if(!m_curvedPicks.empty() && sketchPosition(position)) {
        std::vector<modeling::Point2> points;
        for(const auto& pick:m_curvedPicks)points.push_back(pick.position);
        std::optional<modeling::SketchPointRef> reference;
        const auto next=drawingPoint(position,reference);
        if(std::hypot(next.x-points.back().x,next.y-points.back().y)>1e-6)points.push_back(next);
        try {
            if(points.size()>=2)m_curvedPreview.push_back(modeling::interpolatingSpline(points));
        } catch(const std::exception&) {} // A transient invalid curve is not committed.
    }
    updateSketchPresentation(); updateInteractionPresentation();
}
void CadViewport::finishSpline()
{
    if(!m_sketch || m_curvedPicks.size()<2)return;
    try {
        auto candidate=*m_sketch; std::vector<modeling::Point2> points;
        for(const auto& pick:m_curvedPicks)points.push_back(pick.position);
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        auto entity=modeling::interpolatingSpline(points,id); entity.construction=m_tools.construction();
        candidate.entities.push_back(entity);
        for(std::size_t i=0;i<m_curvedPicks.size();++i) {
            const auto role=i==0?modeling::PointRole::First:i+1==m_curvedPicks.size()?modeling::PointRole::Second:modeling::splinePointRole(i-1);
            const modeling::SketchPointRef point{id,role}; const auto& pick=m_curvedPicks[i];
            if(pick.reference) {
                auto reference=*pick.reference; const bool midpoint=reference.role==modeling::PointRole::Midpoint;
                if(midpoint)reference.role=modeling::PointRole::First;
                candidate.relations.push_back({id+"-fit-"+std::to_string(i),midpoint?modeling::RelationKind::Midpoint:modeling::RelationKind::Coincident,point,reference});
            } else if(pick.boundary)attachBoundarySnap(candidate,point,*pick.boundary);
        }
        if(std::hypot(entity.first.x-entity.second.x,entity.first.y-entity.second.y)<=1e-6)
            candidate.relations.push_back({id+"-closed",modeling::RelationKind::Coincident,{id,modeling::PointRole::First},{id,modeling::PointRole::Second}});
        candidate=modeling::requireSolvedSketch(modeling::solvePlanarSketch(candidate));
        clearArcOrSlot(); resetSketchInference(); emit sketchChanged(candidate);
    } catch(const std::exception& error) {emit sketchInteractionError(QString::fromUtf8(error.what()));}
}
}
