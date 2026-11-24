#pragma once
#include "document/AssemblyDocument.h"
#include "document/UndoHistory.h"
namespace thecad::application
{
class AssemblySession final
{
public:
    const document::AssemblyDocument& document() const { return m_document; }
    const QString& path() const { return m_path; }
    bool modified() const { return m_path.isEmpty() || m_revision != m_saved; }
    void open(const QString& path);
    void save(const QString& path);
    void reset();
    void replaceInitial(document::AssemblyDocument document);
    bool commit(document::AssemblyDocument candidate);
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    std::size_t revision() const { return m_revision; }
    std::size_t identity() const { return m_identity; }
    void traverse(bool redo);

private:
    struct State
    {
        document::AssemblyDocument document;
        std::size_t revision = 0;
    };
    document::AssemblyDocument m_document;
    document::UndoHistory<State> m_history{10};
    QString m_path;
    std::size_t m_revision = 0, m_saved = 0, m_next = 0;
    std::size_t m_identity = 1;
};
} // namespace thecad::application
