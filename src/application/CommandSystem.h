#pragma once

#include "modeling/Selection.h"
#include "modeling/ReferenceGeometry.h"
#include "modeling/Measurement.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace thecad::application {

enum class DocumentType { Part, Assembly, Drawing };
enum class Workspace { Part, Sketch, Assembly, Drawing };
enum class ParameterType { Length, Angle, Count, Direction, Boolean, Enumeration, Reference, FeatureId, FilePath, Regions, DatumDefinition };
enum class CommandState { Rejected, Completed, Started, WaitingForSelection, WaitingForParameters,
                          PreviewReady, Committed, Canceled, Failed };

struct ParameterDescriptor {
    std::string name;
    ParameterType type;
    bool required = false;
};
struct CommandDescriptor {
    std::string id, name, description, category;
    std::vector<Workspace> workspaces;
    std::vector<ParameterDescriptor> parameters;
    bool interactive = false;
    bool legacyBacked = true;
};
struct Length { double millimeters = 0; };
struct Angle { double radians = 0; };
struct Count { int value = 0; };
struct Direction { int value = 1; };
using CommandValue = std::variant<Length, Angle, Count, Direction, bool, std::string,
                                  modeling::SelectionRef, modeling::RegionReferences,
                                  modeling::DatumDefinition>;
struct CommandContext {
    std::string documentId;
    std::uint64_t revision = 0;
    DocumentType documentType = DocumentType::Part;
    Workspace workspace = Workspace::Part;
    std::string activeSketchId, activeOperationId, modelingMode;
    std::vector<modeling::SelectionRef> selection;
    std::uint64_t topologyGeneration = 0;
    std::string units = "mm";
    bool hasViewport = false, canUndo = false, canRedo = false;
};
struct CommandRequest {
    std::string id;
    std::optional<std::uint64_t> expectedRevision;
    std::map<std::string, CommandValue> parameters;
};
struct CommandAvailability {
    bool available = false;
    std::string reason;
    std::vector<std::string> missing;
};
struct CommandResult {
    CommandState state = CommandState::Rejected;
    std::string message, sessionId;
    std::vector<std::string> affectedIds;
    bool documentChanged = false;
    std::optional<modeling::Measurement> measurement;
    explicit operator bool() const { return state != CommandState::Rejected && state != CommandState::Failed; }
};

class CommandSession {
public:
    virtual ~CommandSession() = default;
    virtual CommandResult update(const CommandRequest&, const CommandContext&) = 0;
    virtual CommandResult confirm(const CommandContext&) = 0;
    virtual CommandResult cancel() = 0;
};

class CommandRegistry final {
public:
    bool add(CommandDescriptor descriptor);
    void markMigrated(const std::string& id);
    const CommandDescriptor* find(const std::string& id) const;
    std::vector<CommandDescriptor> enumerate() const;
private:
    std::map<std::string, CommandDescriptor> m_commands;
};

class CommandDispatcher final {
public:
    using ContextProvider = std::function<CommandContext()>;
    using Availability = std::function<CommandAvailability(const CommandContext&)>;
    using Immediate = std::function<CommandResult(const CommandRequest&, const CommandContext&)>;
    using Interactive = std::function<std::unique_ptr<CommandSession>(const CommandRequest&, const CommandContext&)>;
    explicit CommandDispatcher(ContextProvider context);
    CommandRegistry& registry() { return m_registry; }
    const CommandRegistry& registry() const { return m_registry; }
    void bindImmediate(const std::string& id, Availability available, Immediate execute);
    void bindInteractive(const std::string& id, Availability available, Interactive begin);
    void bindSessionControls();
    CommandAvailability availability(const std::string& id) const;
    CommandResult execute(const CommandRequest& request);
    CommandResult update(const std::string& sessionId, const CommandRequest& request);
    CommandResult confirm(const std::string& sessionId);
    CommandResult cancel(const std::string& sessionId);
    const std::string& activeSessionId() const { return m_activeId; }
    const std::string& activeCommandId() const { return m_activeCommandId; }
private:
    struct Binding { Availability available; Immediate immediate; Interactive interactive; };
    CommandAvailability check(const std::string& id, const CommandContext& context) const;
    static CommandResult rejected(std::string reason);
    CommandRegistry m_registry;
    ContextProvider m_context;
    std::map<std::string, Binding> m_bindings;
    std::unique_ptr<CommandSession> m_active;
    std::string m_activeId;
    std::string m_activeCommandId;
    std::string m_activeDocumentId;
    std::uint64_t m_activeRevision = 0, m_nextSession = 0;
};

// Registers metadata only. Legacy commands are intentionally unavailable to the dispatcher
// until a handler is bound by a document/viewport adapter.
void registerBuiltInCommands(CommandRegistry& registry);
} // namespace thecad::application
