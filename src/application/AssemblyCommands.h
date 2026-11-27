#pragma once
#include "application/AssemblySession.h"
#include "application/CommandSystem.h"

namespace thecad::application {
class AssemblyCommands final {
public:
    explicit AssemblyCommands(document::AssemblyDocument document = {});
    CommandDispatcher& dispatcher() { return m_dispatcher; }
    const AssemblySession& session() const { return m_session; }
    AssemblySession& session() { return m_session; }
    void selectComponent(std::string id) { m_selected = std::move(id); }
private:
    CommandContext context() const;
    AssemblySession m_session;
    std::string m_selected;
    CommandDispatcher m_dispatcher;
};
} // namespace thecad::application
