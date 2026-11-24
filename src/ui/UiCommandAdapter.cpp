#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/MeasurePanel.h"
#include <QAction>
#include <QLabel>
#include <QStatusBar>

namespace thecad::ui {
namespace {
application::CommandAvailability yes() { return {true, {}, {}}; }
application::CommandAvailability no(const char* why, const char* missing) {
    return {false, why, {missing}};
}
class LineSession final : public application::CommandSession {
public:
    explicit LineSession(std::function<void()> exit) : m_exit(std::move(exit)) {}
    application::CommandResult update(const application::CommandRequest&, const application::CommandContext&) override {
        return {application::CommandState::WaitingForSelection, "Choose line points in the viewport"};
    }
    application::CommandResult confirm(const application::CommandContext&) override {
        return {application::CommandState::WaitingForSelection, "Line tool remains active"};
    }
    application::CommandResult cancel() override {
        m_exit(); return {application::CommandState::Canceled, "Line tool canceled"};
    }
private:
    std::function<void()> m_exit;
};
}
application::CommandContext MainWindow::commandContext() const {
    application::CommandContext context;
    context.documentId = "part:" + std::to_string(m_session.identity());
    context.revision = m_session.revision();
    context.documentType = application::DocumentType::Part;
    context.workspace = m_session.editingSketch() ? application::Workspace::Sketch : application::Workspace::Part;
    context.activeSketchId = m_session.editingSketchId();
    context.activeOperationId = m_commands.activeSessionId();
    context.modelingMode = featureDraftActive() ? "feature-draft" :
        m_session.editingSketch() && m_viewport && m_viewport->sketchTool() ? "sketch.draw" :
        m_session.editingSketch() && m_viewport && m_viewport->smartDimensionActive() ? "sketch.dimension" :
        m_session.editingSketch() ? "sketch.select" : "part";
    context.selection = m_viewport ? m_viewport->selection() : std::vector<modeling::SelectionRef>{};
    if (!context.selection.empty()) context.topologyGeneration = context.selection.front().generation;
    context.hasViewport = m_viewport != nullptr;
    context.canUndo = m_session.canUndo(); context.canRedo = m_session.canRedo();
    return context;
}
void MainWindow::bindCommandHandlers() {
    m_commands.bindImmediate("view.isometric",
        [](const auto& context) { return context.hasViewport ? yes() : no("Viewport is unavailable", "viewport"); },
        [this](const auto&, const auto&) {
            m_viewport->setStandardView(CadViewport::StandardView::Isometric);
            return application::CommandResult{application::CommandState::Completed};
        });
    m_commands.bindImmediate("edit.undo",
        [this](const auto& context) {
            return context.canUndo && !featureDraftActive() && !m_selectingPlane &&
                !(m_smartDimension && m_smartDimension->editor) ? yes() : no("Undo is unavailable in the current state", "history or inactive draft");
        },
        [this](const auto&, const auto&) {
            traverseHistory(false);
            return application::CommandResult{application::CommandState::Completed, "Undone", {}, {}, true};
        });
    m_commands.bindImmediate("edit.redo",
        [this](const auto& context) {
            return context.canRedo && !featureDraftActive() && !m_selectingPlane &&
                !(m_smartDimension && m_smartDimension->editor) ? yes() : no("Redo is unavailable in the current state", "history or inactive draft");
        },
        [this](const auto&, const auto&) {
            traverseHistory(true);
            return application::CommandResult{application::CommandState::Completed, "Redone", {}, {}, true};
        });
    m_commands.bindInteractive("sketch.line",
        [this](const auto& context) { return !context.activeSketchId.empty() && !featureDraftActive() ?
            yes() : no("An active sketch is required", "sketch"); },
        [this](const auto&, const auto&) -> std::unique_ptr<application::CommandSession> {
            m_viewport->setSketchTool(modeling::EntityKind::Line);
            return std::make_unique<LineSession>([this] {
                if (m_viewport && m_viewport->sketchTool() == modeling::EntityKind::Line &&
                    !m_viewport->constructionTool()) m_viewport->selectSketchTool();
            });
        });
    m_commands.bindImmediate("inspect.measure",
        [this](const auto&) { return !featureDraftActive() && !m_selectingPlane &&
            !m_constraints && !m_sketchModifier &&
            (m_session.editingSketch() || !document().features().empty()) ? yes() :
            no("An active sketch or existing part geometry is required", "part or sketch"); },
        [this](const application::CommandRequest& request, const auto&) {
            const auto it = request.parameters.find("active");
            const bool active = it == request.parameters.end() ? true : std::get<bool>(it->second);
            setMeasureActive(active);
            return application::CommandResult{application::CommandState::Completed,
                m_measurePanel ? m_measurePanel->readingText().toStdString() : std::string{}};
        });
}
application::CommandResult MainWindow::dispatchCommand(const std::string& id) {
    if (m_commands.activeCommandId() == "sketch.line" &&
        (id == "sketch.line" || (id != "edit.undo" && id != "edit.redo" && id.rfind("view.", 0) != 0)))
        m_commands.cancel(m_commands.activeSessionId());
    const auto result = m_commands.execute({id});
    if (result.state == application::CommandState::Rejected && m_viewport)
        statusBar()->showMessage(QString::fromStdString(result.message), 3000);
    return result;
}
} // namespace thecad::ui
