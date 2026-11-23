#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/AssemblyViewport.h"
#include <algorithm>

namespace thecad::ui {
application::CommandContext AssemblyWindow::commandContext() const {
    application::CommandContext context;
    context.documentId = "assembly:" + std::to_string(m_session.identity());
    context.revision = m_session.revision();
    context.documentType = application::DocumentType::Assembly;
    context.workspace = application::Workspace::Assembly;
    context.activeOperationId = m_commands.activeSessionId();
    context.modelingMode = draftActive() ? "assembly-draft" : "assembly";
    if (!m_selected.empty()) {
        modeling::SelectionRef component;
        component.kind = modeling::SelectionKind::Body;
        component.featureId = m_selected;
        context.selection.push_back(std::move(component));
    }
    context.hasViewport = m_viewport != nullptr;
    context.canUndo = m_session.canUndo(); context.canRedo = m_session.canRedo();
    return context;
}
void AssemblyWindow::bindCommandHandlers() {
    const auto yes = [] { return application::CommandAvailability{true, {}, {}}; };
    const auto no = [](const char* why, const char* missing) {
        return application::CommandAvailability{false, why, {missing}};
    };
    m_commands.bindImmediate("edit.undo",
        [this, yes, no](const auto& c) { return !draftActive() && c.canUndo ? yes() : no("Undo is unavailable", "history or inactive draft"); },
        [this](const auto&, const auto&) {
            m_session.traverse(false); refresh();
            return application::CommandResult{application::CommandState::Completed, "Undone", {}, {}, true};
        });
    m_commands.bindImmediate("edit.redo",
        [this, yes, no](const auto& c) { return !draftActive() && c.canRedo ? yes() : no("Redo is unavailable", "history or inactive draft"); },
        [this](const auto&, const auto&) {
            m_session.traverse(true); refresh();
            return application::CommandResult{application::CommandState::Completed, "Redone", {}, {}, true};
        });
    m_commands.bindImmediate("assembly.fix_float",
        [this, yes, no](const auto&) {
            if (draftActive() || m_selected.empty()) return no("Select a component outside a draft", "component");
            const auto found = std::find_if(document().components().begin(), document().components().end(),
                [this](const auto& component) { return component.id == m_selected; });
            return found != document().components().end() ? yes() : no("Selected component is unavailable", "component");
        },
        [this](const auto&, const auto&) {
            const auto id = m_selected;
            const auto revision = m_session.revision();
            mutate([&](auto& doc) { doc.setFixed(id, !doc.component(id).fixed); });
            const bool changed = m_session.revision() != revision;
            return application::CommandResult{changed ? application::CommandState::Completed : application::CommandState::Failed,
                changed ? "Component Fix / Float updated" : "Component change was rejected", {}, {id}, changed};
        });
}
} // namespace thecad::ui
