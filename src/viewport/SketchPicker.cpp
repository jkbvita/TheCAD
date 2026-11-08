#include "viewport/SketchPicker.h"
#include "viewport/DimensionPresenter.h"
#include "modeling/SketchSnapping.h"
#include "modeling/SketchArcs.h"
#include "modeling/DimensionAnnotation.h"
#include <QFontMetricsF>
#include <QLineF>
#include <algorithm>
#include <array>
#include <cmath>
namespace thecad::viewport
{
std::optional<SketchPicker::SnapHit> SketchPicker::pointAt(const QPointF& position,
                                                           bool includeMidpoints) const
{

    std::vector<modeling::ProjectedSketchPoint> projected;
    const double dpr = m_ratio;
    for (const auto& entry : modeling::sketchPoints(m_sketch))
    {
        const auto pixel = screenPoint(entry.second);
        projected.push_back({entry.first, entry.second, pixel.x() * dpr, pixel.y() * dpr});
    }
    if (includeMidpoints)
        for (const auto& e : m_sketch.entities)
            if (e.kind == modeling::EntityKind::Line)
            {
                const modeling::SketchPointRef ref{e.id, modeling::PointRole::Midpoint};
                const auto point = modeling::sketchPointValue(m_sketch, ref);
                const auto pixel = screenPoint(point);
                projected.push_back({ref, point, pixel.x() * dpr, pixel.y() * dpr});
            }
    // Logical pixels become native pixels exactly once, like mouse input.
    const auto hit = modeling::nearestSketchPoint(projected, position.x() * dpr, position.y() * dpr,
                                                  m_tolerance * dpr);
    if (hit)
        return SnapHit{hit->reference, hit->model};
    return {};
}

std::optional<SketchPicker::SnapHit> SketchPicker::selectionPointAt(const QPointF& position) const
{
    if (!m_policy.allows(modeling::SelectionKind::SketchPoint))
        return {};
    auto hit = pointAt(position);
    if (hit && hit->reference.role == modeling::PointRole::Origin)
    {
        for (const auto& e : m_sketch.entities)
            if (e.kind == modeling::EntityKind::Point &&
                QLineF(position, screenPoint(e.first)).length() <=
                    QLineF(position, screenPoint(hit->point)).length() + 0.1)
                return SnapHit{{e.id, modeling::PointRole::First}, e.first};
    }
    // A small circle's center tolerance must not swallow its circumference.
    // Exact center picks and the existing drawing/snapping policy are retained.
    if (hit && hit->reference.role == modeling::PointRole::Center)
    {
        const auto found =
            std::find_if(m_sketch.entities.begin(), m_sketch.entities.end(),
                         [&](const auto& e) { return e.id == hit->reference.entityId; });
        if (found != m_sketch.entities.end() && found->kind == modeling::EntityKind::Circle)
        {
            const auto center = screenPoint(found->first),
                       edge = screenPoint({found->first.x + found->radius, found->first.y});
            const double centerDistance = QLineF(position, center).length(),
                         radius = QLineF(center, edge).length();
            if (std::abs(centerDistance - radius) < centerDistance)
                return {};
        }
    }
    return hit;
}

std::vector<modeling::SelectionCandidate>
SketchPicker::sketchEntityCandidates(const QPointF& position) const
{

    const auto distanceToSegment = [&](modeling::Point2 a, modeling::Point2 b)
    {
        const auto first = screenPoint(a), second = screenPoint(b);
        const auto d = second - first;
        const auto offset = position - first;
        const double length2 = QPointF::dotProduct(d, d);
        const double t =
            length2 > 0 ? std::clamp(QPointF::dotProduct(offset, d) / length2, 0.0, 1.0) : 0.0;
        const auto nearest = first + d * t;
        return std::hypot(nearest.x() - position.x(), nearest.y() - position.y());
    };
    std::vector<modeling::SelectionCandidate> result;
    for (const auto& entity : m_sketch.entities)
    {
        double distance = 1e100;
        if (entity.kind == modeling::EntityKind::Line)
            distance = distanceToSegment(entity.first, entity.second);
        else if (entity.kind == modeling::EntityKind::Point)
            distance = QLineF(position, screenPoint(entity.first)).length();
        else if (entity.kind == modeling::EntityKind::Rectangle)
        {
            const std::array<modeling::Point2, 4> corners{entity.first,
                                                          {entity.second.x, entity.first.y},
                                                          entity.second,
                                                          {entity.first.x, entity.second.y}};
            for (int n = 0; n < 4; ++n)
                distance = std::min(distance, distanceToSegment(corners[n], corners[(n + 1) % 4]));
        }
        else if (entity.kind == modeling::EntityKind::Arc)
        {
            const auto center = screenPoint(entity.center);
            const double radius = std::max(QLineF(center, screenPoint({entity.center.x + entity.radius, entity.center.y})).length(),
                                           QLineF(center, screenPoint({entity.center.x, entity.center.y + entity.radius})).length());
            const int segments = std::clamp(int(std::ceil(std::abs(modeling::arcAngles(entity).sweep) *
                                                          std::sqrt(std::max(1.0, radius)))), 8, 2048);
            auto first = entity.first;
            for (int i = 1; i <= segments; ++i)
            {
                const auto next = modeling::arcPoint(entity, double(i) / segments);
                distance = std::min(distance, distanceToSegment(first, next));
                first = next;
            }
        }
        else if (entity.kind == modeling::EntityKind::Circle)
        {
            const auto center = screenPoint(entity.first);
            const auto edge = screenPoint({entity.first.x + entity.radius, entity.first.y});
            const double radius = std::hypot(edge.x() - center.x(), edge.y() - center.y());
            distance =
                std::abs(std::hypot(position.x() - center.x(), position.y() - center.y()) - radius);
        }
        if (distance < m_tolerance)
            result.push_back(
                {{modeling::SelectionKind::SketchEntity, m_featureId, entity.id}, distance});
    }
    return result;
}

std::vector<modeling::SelectionCandidate>
SketchPicker::dimensionCandidates(const QPointF& position) const
{
    if (!m_policy.allows(modeling::SelectionKind::Dimension))
        return {};

    std::vector<modeling::SelectionCandidate> result;
    QFont textFont("Arial");
    textFont.setPixelSize(14);
    const QFontMetricsF metrics(textFont);
    for (const auto& d : m_sketch.dimensions)
    {
        const auto graphics = modeling::dimensionGraphics(m_sketch, d, m_unit);
        const auto anchor = screenPoint(graphics.text);
        // AIS text uses a left baseline anchor; use the actual string extent.
        const double width = metrics.horizontalAdvance(DimensionPresenter::text(m_sketch, d));
        const double dx =
            std::max({anchor.x() - position.x(), 0.0, position.x() - (anchor.x() + width)});
        const double dy = std::max({anchor.y() - metrics.ascent() - position.y(), 0.0,
                                    position.y() - (anchor.y() + metrics.descent())});
        double distance = std::hypot(dx, dy);
        for (const auto& segment : graphics.segments)
        {
            const auto a = screenPoint(segment.first), v = screenPoint(segment.second) - a,
                       delta = position - a;
            const double n = QPointF::dotProduct(v, v);
            const double t = n > 0 ? std::clamp(QPointF::dotProduct(delta, v) / n, 0.0, 1.0) : 0;
            const auto offset = delta - v * t;
            distance = std::min(distance, std::hypot(offset.x(), offset.y()));
        }
        if (distance < m_tolerance)
            result.push_back({{modeling::SelectionKind::Dimension, m_featureId, d.id}, distance});
    }
    return result;
}
} // namespace thecad::viewport
