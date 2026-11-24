#include "application/AssemblyCommands.h"
#include <algorithm>

namespace thecad::application {
AssemblyCommands::AssemblyCommands(document::AssemblyDocument document)
    : m_dispatcher([this] { return context(); }) {
    m_session.replaceInitial(std::move(document));
    registerBuiltInCommands(m_dispatcher.registry());
    m_dispatcher.bindSessionControls();
    m_dispatcher.bindImmediate("assembly.fix_float",
        [this](const CommandContext&) {
            if (m_selected.empty()) return CommandAvailability{false, "Select a component", {"component"}};
            try { (void)m_session.document().component(m_selected); }
            catch (...) { return CommandAvailability{false, "Selected component is unavailable", {"component"}}; }
            return CommandAvailability{true, {}, {}};
        },
        [this](const CommandRequest& request, const CommandContext&) {
            const auto param = request.parameters.find("component");
            const auto id = param == request.parameters.end() ? m_selected : std::get<std::string>(param->second);
            auto candidate = m_session.document();
            candidate.setFixed(id, !candidate.component(id).fixed);
            const bool changed = m_session.commit(std::move(candidate));
            return CommandResult{CommandState::Completed, "Component Fix / Float updated", {}, {id}, changed};
        });
    m_dispatcher.bindImmediate("edit.undo",
        [](const CommandContext& c) { return c.canUndo ? CommandAvailability{true, {}, {}} :
            CommandAvailability{false, "No undo entry", {"history"}}; },
        [this](const CommandRequest&, const CommandContext&) {
            m_session.traverse(false);
            return CommandResult{CommandState::Completed, "Undone", {}, {}, true};
        });
    m_dispatcher.bindImmediate("edit.redo",
        [](const CommandContext& c) { return c.canRedo ? CommandAvailability{true, {}, {}} :
            CommandAvailability{false, "No redo entry", {"history"}}; },
        [this](const CommandRequest&, const CommandContext&) {
            m_session.traverse(true);
            return CommandResult{CommandState::Completed, "Redone", {}, {}, true};
        });
}
CommandContext AssemblyCommands::context() const {
    CommandContext c;
    c.documentId = "assembly:" + std::to_string(m_session.identity());
    c.revision = m_session.revision(); c.documentType = DocumentType::Assembly;
    c.workspace = Workspace::Assembly; c.canUndo = m_session.canUndo(); c.canRedo = m_session.canRedo();
    return c;
}
} // namespace thecad::application
