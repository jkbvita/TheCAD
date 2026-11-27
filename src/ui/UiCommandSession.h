#pragma once
#include "application/CommandSystem.h"
#include <functional>

namespace thecad::ui {
// The UI adapter owns only gesture and widget binding. The dispatcher owns the
// session identity and rejects updates addressed to another operation.
class UiCommandSession final : public application::CommandSession {
public:
    using Update = std::function<application::CommandResult(const application::CommandRequest&)>;
    using Confirm = std::function<application::CommandResult()>;
    using Cancel = std::function<application::CommandResult()>;
    UiCommandSession(Update update, Confirm confirm, Cancel cancel)
        : m_update(std::move(update)), m_confirm(std::move(confirm)), m_cancel(std::move(cancel)) {}
    application::CommandResult update(const application::CommandRequest& request,
                                      const application::CommandContext&) override { return m_update(request); }
    application::CommandResult confirm(const application::CommandContext&) override { return m_confirm(); }
    application::CommandResult cancel() override { return m_cancel(); }
private:
    Update m_update;
    Confirm m_confirm;
    Cancel m_cancel;
};
} // namespace thecad::ui
