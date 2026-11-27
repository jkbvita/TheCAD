#include "ui/CadViewport.h"
#include "modeling/SketchArcs.h"

#include <QUuid>
#include <cmath>
#include <exception>

namespace thecad::ui
{
using modeling::EntityKind;
using modeling::PointRole;

bool CadViewport::drawingArcOrSlot() const
{
    return m_tools.drawing() == EntityKind::Arc || m_tools.drawing() == EntityKind::StraightSlot;
}

void CadViewport::clearArcOrSlot()
{
    m_curvedPicks.clear();
    m_curvedPreview.clear();
}

void CadViewport::pickArcOrSlot(const QPointF& position, modeling::Point2 raw)
{
    if (m_curvedPicks.size() < 2)
    {
        CurvedPick pick;
        pick.position = drawingPoint(position, pick.reference);
        pick.boundary = m_boundarySnap;
        if (!m_curvedPicks.empty())
        {
            const auto start = m_curvedPicks.front().position;
            if (std::hypot(start.x - pick.position.x, start.y - pick.position.y) <= 1e-7)
                return;
        }
        m_curvedPicks.push_back(std::move(pick));
        previewArcOrSlot(position);
        return;
    }

    auto candidate = *m_sketch;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto& a = m_curvedPicks[0];
    const auto& b = m_curvedPicks[1];
    const auto attach = [&](const CurvedPick& pick, modeling::SketchPointRef point, const char* suffix) {
        if (pick.reference)
        {
            auto reference = *pick.reference;
            const auto midpoint = reference.role == PointRole::Midpoint;
            if (midpoint)
                reference.role = PointRole::First;
            candidate.relations.push_back({id + suffix, midpoint ? modeling::RelationKind::Midpoint
                                                                : modeling::RelationKind::Coincident,
                                            point, reference});
        }
        else if (pick.boundary)
            attachBoundarySnap(candidate, point, *pick.boundary);
    };
    try
    {
        if (m_tools.drawing() == EntityKind::Arc)
        {
            candidate.entities.push_back(modeling::threePointArc(a.position, b.position, raw, id));
            attach(a, {id, PointRole::First}, "-start");
            attach(b, {id, PointRole::Second}, "-end");
        }
        else
        {
            const auto outline = modeling::straightSlot(a.position, b.position, raw, id);
            modeling::addStraightSlot(candidate, outline, id);
            attach(a, {outline[3].id, PointRole::Center}, "-start");
            attach(b, {outline[1].id, PointRole::Center}, "-end");
        }
        candidate = modeling::requireSolvedSketch(modeling::solvePlanarSketch(candidate));
    }
    catch (const std::exception& error)
    {
        emit sketchInteractionError(QString::fromUtf8(error.what()));
        return;
    }
    clearArcOrSlot();
    resetSketchInference();
    emit sketchChanged(candidate);
}

void CadViewport::previewArcOrSlot(const QPointF& position)
{
    m_curvedPreview.clear();
    const auto raw = sketchPosition(position);
    if (raw && !m_curvedPicks.empty())
    {
        const auto first = m_curvedPicks.front().position;
        if (m_curvedPicks.size() == 1)
        {
            std::optional<modeling::SketchPointRef> reference;
            const auto next = drawingPoint(position, reference);
            if (std::hypot(first.x - next.x, first.y - next.y) > 1e-7)
                m_curvedPreview.push_back({EntityKind::Line, first, next, 1, {}, true});
        }
        else
        {
            const auto second = m_curvedPicks.back().position;
            try
            {
                if (m_tools.drawing() == EntityKind::Arc)
                    m_curvedPreview.push_back(modeling::threePointArc(first, second, *raw, {}));
                else
                {
                    const auto edges = modeling::straightSlot(first, second, *raw, "preview");
                    m_curvedPreview.assign(edges.begin(), edges.end());
                    for (auto& edge : m_curvedPreview)
                        edge.id.clear();
                }
            }
            catch (const std::exception&)
            {
                m_curvedPreview.push_back({EntityKind::Line, first, second, 1, {}, true});
            }
        }
    }
    else if (raw)
    {
        std::optional<modeling::SketchPointRef> reference;
        (void)drawingPoint(position, reference);
    }
    updateSketchPresentation();
    updateInteractionPresentation();
}
}
