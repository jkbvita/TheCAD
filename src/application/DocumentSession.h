#pragma once
#include "document/PartDocument.h"
#include "document/UndoHistory.h"
#include <optional>
namespace thecad::application
{
// Application state and edit history are independent of widgets and GPU objects.
class DocumentSession final
{
public:
    struct State
    {
        document::PartDocument document;
        std::optional<modeling::Sketch> sketch;
        std::string sketchId, selectedId;
        TopoDS_Shape displayedShape;
        std::size_t revision = 0;
    };
    explicit DocumentSession(
        document::PartDocument document = document::PartDocument::createEmpty());
    document::PartDocument& document() { return m_document; }
    const document::PartDocument& document() const { return m_document; }
    std::optional<modeling::Sketch>& editingSketch() { return m_sketch; }
    const std::optional<modeling::Sketch>& editingSketch() const { return m_sketch; }
    std::string& editingSketchId() { return m_sketchId; }
    const std::string& editingSketchId() const { return m_sketchId; }
    std::string& selectedFeatureId() { return m_selectedId; }
    const std::string& selectedFeatureId() const { return m_selectedId; }
    const QString& path() const { return m_path; }
    void setPath(QString path) { m_path = std::move(path); }
    State snapshot(const TopoDS_Shape& displayedShape) const;
    void record(State previous);
    bool recordSketchChange(State previous);
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    const State& historyTarget(bool redo) const
    {
        return redo ? m_history.redoTarget() : m_history.undoTarget();
    }
    // The caller validates display of the target before committing traversal.
    void restore(const State& target);
    void commitTraversal(bool redo, State previous);
    void clearHistory();
    void markSaved() { m_savedRevision = m_revision; }
    bool modified() const { return m_path.isEmpty() || m_revision != m_savedRevision; }

private:
    document::PartDocument m_document;
    std::optional<modeling::Sketch> m_sketch;
    std::string m_sketchId, m_selectedId;
    QString m_path;
    document::UndoHistory<State> m_history{10};
    std::size_t m_revision = 0, m_nextRevision = 0, m_savedRevision = 0;
};
} // namespace thecad::application
