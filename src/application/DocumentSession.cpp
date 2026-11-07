#include "application/DocumentSession.h"
#include <QDataStream>
#include <QIODevice>
namespace thecad::application
{
namespace
{
// Exact edit identity, independent of solver diagnostics and generated topology.
QByteArray sketchKey(const modeling::Sketch& sketch)
{
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    const auto point = [&](modeling::Point2 p) { out << p.x << p.y; };
    const auto ref = [&](const modeling::SketchPointRef& r)
    { out << QString::fromStdString(r.entityId) << int(r.role); };
    out << int(sketch.plane) << int(sketch.space) << bool(sketch.attachment);
    out << QString::fromStdString(sketch.referencePlaneId);
    if (sketch.attachment)
    {
        const auto& a = *sketch.attachment;
        out << QString::fromStdString(a.sourceFeature) << a.faceIndex
            << QString::fromStdString(a.sourceSignature);
        const auto& f = a.frame;
        out << f.Location().X() << f.Location().Y() << f.Location().Z() << f.XDirection().X()
            << f.XDirection().Y() << f.XDirection().Z() << f.Direction().X() << f.Direction().Y()
            << f.Direction().Z();
    }
    out << quint64(sketch.entities.size());
    for (const auto& e : sketch.entities)
    {
        out << QString::fromStdString(e.id) << int(e.kind) << e.construction;
        point(e.first);
        point(e.second);
        out << e.radius;
    }
    out << quint64(sketch.projections.size());
    for (const auto& link : sketch.projections)
        out << QString::fromStdString(link.entityId) << QString::fromStdString(link.sourceFeature)
            << QString::fromStdString(link.edgeName) << QString::fromStdString(link.startVertex);
    out << quint64(sketch.replications.size());
    for (const auto& copy : sketch.replications)
    {
        out << QString::fromStdString(copy.source) << QString::fromStdString(copy.target)
            << QString::fromStdString(copy.mirrorLine);
        point(copy.offset);
        out << copy.angle;
    }
    out << quint64(sketch.relations.size());
    for (const auto& r : sketch.relations)
    {
        out << QString::fromStdString(r.id) << int(r.kind);
        ref(r.first);
        ref(r.second);
        point(r.fixedPosition);
        point(r.fixedSecond);
        out << r.fixedRadius << QString::fromStdString(r.groupId);
    }
    out << quint64(sketch.dimensions.size());
    for (const auto& d : sketch.dimensions)
    {
        out << QString::fromStdString(d.id) << int(d.kind);
        ref(d.first);
        ref(d.second);
        out << d.value << d.driving << d.sense;
        point(d.annotation);
        out << bool(d.placement);
        if (d.placement)
            point(*d.placement);
    }
    out << quint64(sketch.polygons.size());
    for (const auto& g : sketch.polygons)
    {
        out << QString::fromStdString(g.id) << QString::fromStdString(g.centerId)
            << quint64(g.edges.size());
        for (const auto& id : g.edges)
            out << QString::fromStdString(id);
    }
    return key;
}
} // namespace
DocumentSession::DocumentSession(document::PartDocument document) : m_document(std::move(document))
{
}
DocumentSession::State DocumentSession::snapshot(const TopoDS_Shape& displayedShape) const
{
    return {m_document, m_sketch, m_sketchId, m_selectedId, displayedShape, m_revision};
}
void DocumentSession::record(State previous)
{
    m_history.record(std::move(previous));
    m_revision = ++m_nextRevision;
}
bool DocumentSession::recordSketchChange(State previous)
{
    if (!previous.sketch || !m_sketch || sketchKey(*previous.sketch) == sketchKey(*m_sketch))
        return false;
    record(std::move(previous));
    return true;
}
void DocumentSession::restore(const State& target)
{
    m_document = target.document;
    m_sketch = target.sketch;
    m_sketchId = target.sketchId;
    m_selectedId = target.selectedId;
    m_revision = target.revision;
}
void DocumentSession::commitTraversal(bool redo, State previous)
{
    if (redo)
        m_history.commitRedo(std::move(previous));
    else
        m_history.commitUndo(std::move(previous));
}
void DocumentSession::clearHistory()
{
    m_history.clear();
    m_revision = 0;
    m_nextRevision = 0;
    m_savedRevision = 0;
}
} // namespace thecad::application
