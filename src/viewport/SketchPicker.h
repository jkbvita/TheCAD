#pragma once
#include "modeling/Sketch.h"
#include "modeling/Selection.h"
#include <QPointF>
#include <functional>
namespace thecad::viewport
{
// Read-only screen-space picking. Projection is supplied by the view adapter.
class SketchPicker final
{
public:
    struct SnapHit
    {
        modeling::SketchPointRef reference;
        modeling::Point2 point;
    };
    using Projector = std::function<QPointF(modeling::Point2)>;
    SketchPicker(const modeling::Sketch& sketch, const modeling::SelectionPolicy& policy,
                 std::string featureId, double ratio, double tolerance, double unit,
                 Projector project)
        : m_sketch(sketch), m_policy(policy), m_featureId(std::move(featureId)), m_ratio(ratio),
          m_tolerance(tolerance), m_unit(unit), m_project(std::move(project))
    {
    }
    std::optional<SnapHit> pointAt(const QPointF& position, bool includeMidpoints = false) const;
    std::optional<SnapHit> selectionPointAt(const QPointF& position) const;
    std::vector<modeling::SelectionCandidate> sketchEntityCandidates(const QPointF& position) const;
    std::vector<modeling::SelectionCandidate> dimensionCandidates(const QPointF& position) const;

private:
    QPointF screenPoint(modeling::Point2 p) const { return m_project(p); }
    const modeling::Sketch& m_sketch;
    const modeling::SelectionPolicy& m_policy;
    std::string m_featureId;
    double m_ratio, m_tolerance, m_unit;
    Projector m_project;
};
} // namespace thecad::viewport
