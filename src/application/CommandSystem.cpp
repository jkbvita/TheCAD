#include "application/CommandSystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thecad::application {
namespace {
std::string validateParameters(const CommandDescriptor& descriptor, const CommandRequest& request,
                               bool requireAll) {
    for (const auto& [name, value] : request.parameters) {
        const auto parameter = std::find_if(descriptor.parameters.begin(), descriptor.parameters.end(),
            [&](const auto& item) { return item.name == name; });
        if (parameter == descriptor.parameters.end()) return "Unknown parameter: " + name;
        const bool valid = (parameter->type == ParameterType::Length && std::holds_alternative<Length>(value) &&
                            std::isfinite(std::get<Length>(value).millimeters)) ||
            (parameter->type == ParameterType::Angle && std::holds_alternative<Angle>(value) &&
             std::isfinite(std::get<Angle>(value).radians)) ||
            (parameter->type == ParameterType::Count && std::holds_alternative<Count>(value)) ||
            (parameter->type == ParameterType::Direction && std::holds_alternative<Direction>(value)) ||
            (parameter->type == ParameterType::Boolean && std::holds_alternative<bool>(value)) ||
            ((parameter->type == ParameterType::Enumeration || parameter->type == ParameterType::FeatureId) &&
             std::holds_alternative<std::string>(value)) ||
            (parameter->type == ParameterType::Reference && std::holds_alternative<modeling::SelectionRef>(value)) ||
            (parameter->type == ParameterType::Regions && std::holds_alternative<modeling::RegionReferences>(value)) ||
            (parameter->type == ParameterType::DatumDefinition && std::holds_alternative<modeling::DatumDefinition>(value));
        if (!valid) return "Invalid parameter type or value: " + name;
    }
    if (requireAll) for (const auto& parameter : descriptor.parameters)
        if (parameter.required && !request.parameters.count(parameter.name))
            return "Missing parameter: " + parameter.name;
    return {};
}
}
bool CommandRegistry::add(CommandDescriptor descriptor) {
    if (descriptor.id.empty() || descriptor.name.empty() || descriptor.category.empty() ||
        descriptor.workspaces.empty()) return false;
    const auto id = descriptor.id;
    return m_commands.emplace(id, std::move(descriptor)).second;
}
void CommandRegistry::markMigrated(const std::string& id) {
    const auto it = m_commands.find(id);
    if (it == m_commands.end()) throw std::invalid_argument("Unknown command: " + id);
    it->second.legacyBacked = false;
}
const CommandDescriptor* CommandRegistry::find(const std::string& id) const {
    const auto it = m_commands.find(id);
    return it == m_commands.end() ? nullptr : &it->second;
}
std::vector<CommandDescriptor> CommandRegistry::enumerate() const {
    std::vector<CommandDescriptor> result;
    for (const auto& [id, descriptor] : m_commands) result.push_back(descriptor);
    return result;
}
CommandDispatcher::CommandDispatcher(ContextProvider context) : m_context(std::move(context)) {
    if (!m_context) throw std::invalid_argument("Command context provider is required");
}
void CommandDispatcher::bindImmediate(const std::string& id, Availability available, Immediate execute) {
    const auto* descriptor = m_registry.find(id);
    if (!descriptor || descriptor->interactive || !available || !execute)
        throw std::invalid_argument("Invalid immediate command binding: " + id);
    m_bindings[id] = {std::move(available), std::move(execute), {}};
    m_registry.markMigrated(id);
}
void CommandDispatcher::bindInteractive(const std::string& id, Availability available, Interactive begin) {
    const auto* descriptor = m_registry.find(id);
    if (!descriptor || !descriptor->interactive || !available || !begin)
        throw std::invalid_argument("Invalid interactive command binding: " + id);
    m_bindings[id] = {std::move(available), {}, std::move(begin)};
    m_registry.markMigrated(id);
}
void CommandDispatcher::bindSessionControls() {
    const auto ready = [this](const CommandContext&) {
        return m_active ? CommandAvailability{true, {}, {}} :
            CommandAvailability{false, "No active interactive command", {"session"}};
    };
    bindImmediate("command.confirm", ready,
        [this](const CommandRequest&, const CommandContext&) { return confirm(m_activeId); });
    bindImmediate("command.cancel", ready,
        [this](const CommandRequest&, const CommandContext&) { return cancel(m_activeId); });
}
CommandResult CommandDispatcher::rejected(std::string reason) {
    return {CommandState::Rejected, std::move(reason), {}, {}, false};
}
CommandAvailability CommandDispatcher::check(const std::string& id, const CommandContext& context) const {
    const auto* descriptor = m_registry.find(id);
    if (!descriptor) return {false, "Unknown command", {"command ID"}};
    if (std::find(descriptor->workspaces.begin(), descriptor->workspaces.end(), context.workspace) == descriptor->workspaces.end())
        return {false, "Command is unavailable in the current workspace", {"workspace"}};
    const auto binding = m_bindings.find(id);
    if (binding == m_bindings.end()) return {false, "Legacy UI command has no dispatcher handler", {"handler"}};
    const bool editDuringLine = m_activeCommandId == "sketch.line" &&
        (id == "edit.undo" || id == "edit.redo");
    if (m_active && id != "command.cancel" && id != "command.confirm" &&
        !editDuringLine && id.rfind("view.", 0) != 0)
        return {false, "Finish or cancel the active operation first", {"inactive operation"}};
    return binding->second.available(context);
}
CommandAvailability CommandDispatcher::availability(const std::string& id) const { return check(id, m_context()); }
CommandResult CommandDispatcher::execute(const CommandRequest& request) {
    const auto context = m_context();
    if (request.expectedRevision && *request.expectedRevision != context.revision)
        return rejected("Document changed since this command was prepared");
    if (request.id != "command.cancel" && request.id != "command.confirm")
        for (const auto& ref : context.selection)
            if (modeling::isTopology(ref.kind) && ref.generation != context.topologyGeneration)
                return rejected("Selected geometry is stale; select it again");
    const auto available = check(request.id, context);
    if (!available.available) return rejected(available.reason);
    const auto* descriptor = m_registry.find(request.id);
    if (const auto error = validateParameters(*descriptor, request, true); !error.empty()) return rejected(error);
    const auto& binding = m_bindings.at(request.id);
    try {
        if (!descriptor->interactive) return binding.immediate(request, context);
        auto session = binding.interactive(request, context);
        if (!session) return rejected("Command could not start");
        m_activeId = std::to_string(++m_nextSession);
        m_activeCommandId = request.id;
        m_activeDocumentId = context.documentId;
        m_activeRevision = context.revision;
        m_active = std::move(session);
        if (!request.parameters.empty()) {
            auto result = m_active->update(request, context);
            result.sessionId = m_activeId;
            return result;
        }
        return {CommandState::Started, {}, m_activeId, {}, false};
    } catch (const std::exception& error) { return {CommandState::Failed, error.what(), {}, {}, false}; }
}
CommandResult CommandDispatcher::update(const std::string& sessionId, const CommandRequest& request) {
    if (!m_active || sessionId != m_activeId) return rejected("No matching active command session");
    if (request.id != m_activeCommandId) return rejected("Update targets a different command");
    if (const auto error = validateParameters(*m_registry.find(request.id), request, false); !error.empty())
        return rejected(error);
    const auto context = m_context();
    if (request.expectedRevision && *request.expectedRevision != context.revision)
        return rejected("Document changed since this update was prepared");
    if (context.documentId != m_activeDocumentId || context.revision != m_activeRevision)
        return rejected("Document changed during the active command");
    for (const auto& [name, value] : request.parameters)
        if (std::holds_alternative<modeling::SelectionRef>(value)) {
            const auto& reference = std::get<modeling::SelectionRef>(value);
            if (modeling::isTopology(reference.kind) && reference.generation != context.topologyGeneration)
                return rejected("Selected geometry is stale; select it again");
        }
    try { auto result = m_active->update(request, context); result.sessionId = m_activeId; return result; }
    catch (const std::exception& error) { return {CommandState::Failed, error.what(), m_activeId, {}, false}; }
}
CommandResult CommandDispatcher::confirm(const std::string& sessionId) {
    if (!m_active || sessionId != m_activeId) return rejected("No matching active command session");
    const auto context = m_context();
    if (context.documentId != m_activeDocumentId || context.revision != m_activeRevision)
        return rejected("Document changed during the active command");
    try {
        auto result = m_active->confirm(context);
        result.sessionId = m_activeId;
        if (result.state == CommandState::Committed) { m_active.reset(); m_activeId.clear(); m_activeCommandId.clear(); }
        return result;
    } catch (const std::exception& error) { return {CommandState::Failed, error.what(), m_activeId, {}, false}; }
}
CommandResult CommandDispatcher::cancel(const std::string& sessionId) {
    if (!m_active || sessionId != m_activeId) return rejected("No matching active command session");
    const auto id = m_activeId;
    auto active = std::move(m_active);
    m_activeId.clear();
    m_activeCommandId.clear();
    try {
        auto result = active->cancel(); result.sessionId = id;
        return result;
    } catch (const std::exception& error) {
        return {CommandState::Failed, error.what(), id, {}, false};
    }
}
void registerBuiltInCommands(CommandRegistry& registry) {
    const auto add = [&](const char* id, const char* name, const char* category,
                         std::vector<Workspace> workspaces, bool interactive,
                         std::vector<ParameterDescriptor> parameters = {}) {
        if (!registry.add({id, name, name, category, std::move(workspaces), std::move(parameters), interactive, true}))
            throw std::logic_error(std::string("Duplicate or invalid command: ") + id);
    };
    const std::vector<Workspace> part{Workspace::Part}, sketch{Workspace::Sketch},
        assembly{Workspace::Assembly}, partSketch{Workspace::Part, Workspace::Sketch},
        all{Workspace::Part, Workspace::Sketch, Workspace::Assembly, Workspace::Drawing};
    add("view.isometric", "Isometric", "View", all, false);
    add("view.normal_to", "Normal To", "View", partSketch, false);
    add("view.fit_all", "Fit All", "View", all, false);
    add("view.reset", "Reset View", "View", all, false);
    add("view.maximize", "Maximize Window", "View", all, false);
    add("view.restore", "Restore Window", "View", all, false);
    add("view.show_sketch_relations", "Show Sketch Relations", "View", partSketch, false);
    for (const char* view : {"front", "back", "left", "right", "top", "bottom"})
        add((std::string("view.") + view).c_str(), view, "View", all, false);
    add("edit.undo", "Undo", "Edit", all, false);
    add("edit.redo", "Redo", "Edit", all, false);
    add("edit.delete", "Delete", "Edit", all, false);
    add("edit.feature", "Edit Feature", "Edit", part, false);
    add("edit.entity", "Edit Sketch Entity", "Edit", sketch, false);
    add("edit.delete_entity", "Delete Sketch Entity", "Edit", sketch, false);
    add("edit.reference_visibility", "Show Reference Geometry", "Edit", part, false);
    add("edit.repair_sketch", "Repair Sketch", "Edit", sketch, true);
    for (const char* file : {"new_part", "new_assembly", "open", "save", "save_as", "exit"})
        add((std::string("file.") + file).c_str(), file, "File", all, false);
    add("app.preferences", "Preferences", "Application", all, false);
    add("app.about", "About TheCAD", "Application", all, false);
    add("sketch.create", "Sketch", "Sketch", part, true);
    add("sketch.finish", "Exit Sketch", "Sketch", sketch, false);
    add("sketch.select", "Select Sketch Entity", "Sketch", sketch, false);
    for (const char* tool : {"line", "rectangle", "circle", "arc", "spline", "straight_slot", "point", "centerline", "polygon", "smart_dimension"})
        add((std::string("sketch.") + tool).c_str(), tool, "Sketch", sketch, true);
    for (const char* modifier : {"trim", "extend", "split", "convert", "offset", "mirror", "linear_pattern", "circular_pattern", "relations", "construction"})
        add((std::string("sketch.") + modifier).c_str(), modifier, "Sketch", sketch, true);
    add("feature.extrude", "Extruded Boss/Base", "Feature", partSketch, true, {{"depth", ParameterType::Length, false}, {"sketch", ParameterType::FeatureId, false}, {"regions", ParameterType::Regions, false}});
    add("feature.extruded_cut", "Extruded Cut", "Feature", partSketch, true, {{"depth", ParameterType::Length, false}, {"sketch", ParameterType::FeatureId, false}, {"regions", ParameterType::Regions, false}});
    for (const char* feature : {"revolve", "revolved_cut", "sweep", "swept_cut", "fillet", "chamfer", "linear_pattern", "circular_pattern", "mirror"})
        add((std::string("feature.") + feature).c_str(), feature, "Feature", partSketch, true);
    add("reference.plane", "Reference Plane", "Reference", part, true, {{"definition", ParameterType::DatumDefinition, false}});
    for (const char* datum : {"axis", "point"})
        add((std::string("reference.") + datum).c_str(), datum, "Reference", part, true);
    add("inspect.measure", "Measure", "Inspection", part, false, {{"active", ParameterType::Boolean, false}});
    add("inspect.selection_tolerance", "Sketch Picking Tolerance", "Inspection", sketch, false);
    add("selection.filter_toggle", "Toggle Selection Filters", "Selection", partSketch, false);
    add("selection.filter_clear", "Clear Selection Filters", "Selection", partSketch, false);
    add("command.confirm", "Confirm", "Interaction", all, false);
    add("command.cancel", "Cancel", "Interaction", all, false);
    add("assembly.fix_float", "Fix / Float", "Assembly", assembly, false, {{"component", ParameterType::FeatureId, false}});
    for (const char* operation : {"insert_component", "mate", "belt", "ground", "asset_publisher", "in_context_part"})
        add((std::string("assembly.") + operation).c_str(), operation, "Assembly", assembly, true);
    for (const char* operation : {"hide_show", "delete", "rebuild", "magnetic", "magnetic_snap_distance",
                                  "open", "save", "save_as", "close", "new", "new_part", "open_part",
                                  "edit_mate", "suppress_mates", "unsuppress_mates"})
        add((std::string("assembly.") + operation).c_str(), operation, "Assembly", assembly, false);
}
} // namespace thecad::application
