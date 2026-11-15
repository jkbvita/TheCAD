#include "application/AssemblySession.h"
#include <QFileInfo>
namespace thecad::application
{
void AssemblySession::open(const QString& path)
{
    auto document = document::AssemblyDocument::load(path);
    m_document = std::move(document);
    m_path = QFileInfo(path).absoluteFilePath();
    m_history.clear();
    m_saved = 0;
    m_revision = m_document.sourcesChangedOnLoad() ? 1 : 0;
    m_next = m_revision;
}
void AssemblySession::save(const QString& path)
{
    m_document.save(path);
    m_path = QFileInfo(path).absoluteFilePath();
    m_saved = m_revision;
}
void AssemblySession::reset()
{
    m_document = {};
    m_path.clear();
    m_history.clear();
    m_revision = m_saved = m_next = 0;
}
bool AssemblySession::commit(document::AssemblyDocument candidate)
{
    if (candidate.stateKey() == m_document.stateKey())
        return false;
    m_history.record({m_document, m_revision});
    m_document = std::move(candidate);
    m_revision = ++m_next;
    return true;
}
void AssemblySession::traverse(bool redo)
{
    if (redo ? !canRedo() : !canUndo())
        return;
    const auto target = redo ? m_history.redoTarget() : m_history.undoTarget();
    State previous{m_document, m_revision};
    m_document = target.document;
    m_revision = target.revision;
    if (redo)
        m_history.commitRedo(std::move(previous));
    else
        m_history.commitUndo(std::move(previous));
}
} // namespace thecad::application
