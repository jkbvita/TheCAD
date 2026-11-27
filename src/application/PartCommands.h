#pragma once
#include "application/CommandSystem.h"
#include "application/DocumentSession.h"
#include "application/SketchToolController.h"

namespace thecad::application {
// A QWidget-free host for part commands. Existing MainWindow remains a separate
// presentation adapter while its commands are migrated to this contract.
class PartCommands final {
public:
    explicit PartCommands(document::PartDocument document = document::PartDocument::createEmpty());
    CommandDispatcher& dispatcher() { return m_dispatcher; }
    const DocumentSession& session() const { return m_session; }
    DocumentSession& session() { return m_session; }
    const SketchToolController& sketchTool() const { return m_sketchTool; }
    void setViewCallback(std::function<void()> isometric) { m_isometric = std::move(isometric); }
    void setSelection(std::vector<modeling::SelectionRef> selection) { m_selection = std::move(selection); }
private:
    CommandContext context() const;
    DocumentSession m_session;
    SketchToolController m_sketchTool;
    std::function<void()> m_isometric;
    std::vector<modeling::SelectionRef> m_selection;
    CommandDispatcher m_dispatcher;
};
} // namespace thecad::application
