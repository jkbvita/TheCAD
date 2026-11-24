#include "application/PartCommands.h"
#include "application/FeatureOperation.h"
#include "modeling/Sketch.h"
#include <algorithm>
#include <stdexcept>

namespace thecad::application {
namespace {
CommandResult ready() { return {CommandState::Completed, {}, {}, {}, false}; }
CommandAvailability yes() { return {true, {}, {}}; }
CommandAvailability no(std::string reason, std::string missing) {
    return {false, std::move(reason), {std::move(missing)}};
}
template<class T> std::optional<T> value(const CommandRequest& request, const char* key) {
    const auto it = request.parameters.find(key);
    if (it == request.parameters.end()) return {};
    if (!std::holds_alternative<T>(it->second)) throw std::invalid_argument(std::string("Invalid parameter: ") + key);
    return std::get<T>(it->second);
}
void commit(DocumentSession& session, document::PartDocument candidate, const std::string& id) {
    auto before = session.snapshot(session.document().shape());
    session.document() = std::move(candidate);
    session.selectedFeatureId() = id;
    session.record(std::move(before));
}
std::vector<TopoDS_Shape> measurementShapes(const document::PartDocument& document,
                                             const std::vector<modeling::SelectionRef>& selection) {
    if (selection.empty() || selection.size() > 2)
        throw std::invalid_argument("Select one or two vertices, edges, or faces");
    const auto options = document.namedTopologyOptions();
    std::vector<TopoDS_Shape> shapes;
    for (const auto& selected : selection) {
        const auto source = selected.kind == modeling::SelectionKind::Vertex ? modeling::ReferenceSource::BodyVertex :
                            selected.kind == modeling::SelectionKind::Edge ? modeling::ReferenceSource::BodyEdge :
                            selected.kind == modeling::SelectionKind::Face ? modeling::ReferenceSource::BodyFace :
                            throw std::invalid_argument("Measure requires a vertex, edge, or face");
        const auto option = std::find_if(options.begin(), options.end(), [&](const auto& item) {
            return item.reference.source == source && item.reference.featureId == selected.featureId &&
                   item.reference.index == selected.index;
        });
        if (option == options.end()) throw std::invalid_argument("Selected geometry is no longer available");
        shapes.push_back(option->selectionShape);
    }
    return shapes;
}
class FeatureSession final : public CommandSession {
public:
    FeatureSession(DocumentSession& session, bool cut, std::string sketchId)
        : m_session(session), m_source(session.document()), m_cut(cut), m_sketchId(std::move(sketchId)) {}
    CommandResult update(const CommandRequest& request, const CommandContext&) override {
        if (auto depth = value<Length>(request, "depth")) m_depth = depth->millimeters;
        if (auto sketch = value<std::string>(request, "sketch")) {
            if (*sketch != m_sketchId) m_regions.clear();
            m_sketchId = *sketch;
        }
        if (auto regions = value<modeling::RegionReferences>(request, "regions")) m_regions = *regions;
        m_candidate.reset();
        if (m_depth <= 0) return {CommandState::Rejected, "Extrusion depth must be positive"};
        const auto it = std::find_if(m_source.features().begin(), m_source.features().end(),
            [this](const auto& f) { return f.id == m_sketchId && f.kind == document::FeatureKind::Sketch; });
        if (it == m_source.features().end()) return {CommandState::WaitingForSelection, "Select a source sketch"};
        if (m_regions.empty()) {
            const auto contours = modeling::closedContours(it->sketch);
            if (contours.size() == 1) m_regions.push_back(contours.front().entityIds);
            else return {CommandState::WaitingForSelection, "Select a closed contour"};
        }
        document::Feature feature;
        feature.kind = m_cut ? document::FeatureKind::Cut : document::FeatureKind::Extrude;
        feature.sketchId = m_sketchId; feature.length = m_depth;
        auto result = FeatureOperation::preview(m_source, feature, m_regions, false);
        m_candidate = std::move(result.document); m_candidateId = std::move(result.featureId);
        return {CommandState::PreviewReady, "Preview ready"};
    }
    CommandResult confirm(const CommandContext&) override {
        if (!m_candidate) return {CommandState::Rejected, "No valid preview to commit"};
        const auto id = m_candidateId;
        commit(m_session, std::move(*m_candidate), id);
        m_candidate.reset();
        return {CommandState::Committed, "Feature committed", {}, {id}, true};
    }
    CommandResult cancel() override { m_candidate.reset(); return {CommandState::Canceled, "Feature canceled"}; }
private:
    DocumentSession& m_session;
    document::PartDocument m_source;
    bool m_cut;
    std::string m_sketchId, m_candidateId;
    double m_depth = 10;
    modeling::RegionReferences m_regions;
    std::optional<document::PartDocument> m_candidate;
};
class PlaneSession final : public CommandSession {
public:
    explicit PlaneSession(DocumentSession& session) : m_session(session), m_source(session.document()) {}
    CommandResult update(const CommandRequest& request, const CommandContext&) override {
        m_candidate.reset();
        const auto definition = value<modeling::DatumDefinition>(request, "definition");
        if (!definition) return {CommandState::WaitingForSelection, "Select references or supply a plane definition"};
        if (modeling::datumKind(definition->method) != modeling::DatumKind::Plane)
            return {CommandState::Rejected, "Definition is not a plane"};
        auto candidate = m_source;
        m_candidateId = candidate.addReferenceGeometry(*definition);
        m_candidate = std::move(candidate);
        return {CommandState::PreviewReady, "Plane preview ready"};
    }
    CommandResult confirm(const CommandContext&) override {
        if (!m_candidate) return {CommandState::Rejected, "No valid plane preview to commit"};
        const auto id = m_candidateId;
        commit(m_session, std::move(*m_candidate), id);
        m_candidate.reset();
        return {CommandState::Committed, "Plane committed", {}, {id}, true};
    }
    CommandResult cancel() override { m_candidate.reset(); return {CommandState::Canceled, "Plane canceled"}; }
private:
    DocumentSession& m_session;
    document::PartDocument m_source;
    std::optional<document::PartDocument> m_candidate;
    std::string m_candidateId;
};
class LineSession final : public CommandSession {
public:
    explicit LineSession(SketchToolController& tool) : m_tool(tool) { m_tool.draw(modeling::EntityKind::Line, false); }
    CommandResult update(const CommandRequest&, const CommandContext&) override {
        return {CommandState::WaitingForSelection, "Choose line points in the sketch"};
    }
    CommandResult confirm(const CommandContext&) override {
        return {CommandState::WaitingForSelection, "Line tool remains active for another line"};
    }
    CommandResult cancel() override { m_tool.select(); return {CommandState::Canceled, "Line tool canceled"}; }
private:
    SketchToolController& m_tool;
};
} // namespace

PartCommands::PartCommands(document::PartDocument document)
    : m_session(std::move(document)), m_dispatcher([this] { return context(); }) {
    registerBuiltInCommands(m_dispatcher.registry());
    m_dispatcher.bindSessionControls();
    m_dispatcher.bindImmediate("view.isometric",
        [this](const CommandContext&) { return m_isometric ? yes() : no("Viewport is unavailable", "viewport"); },
        [this](const CommandRequest&, const CommandContext&) { m_isometric(); return ready(); });
    m_dispatcher.bindImmediate("edit.undo",
        [](const CommandContext& c) { return c.canUndo ? yes() : no("No undo entry", "history"); },
        [this](const CommandRequest&, const CommandContext&) {
            auto before = m_session.snapshot(m_session.document().shape());
            const auto target = m_session.historyTarget(false);
            m_session.restore(target); m_session.commitTraversal(false, std::move(before));
            return CommandResult{CommandState::Completed, "Undone", {}, {}, true};
        });
    m_dispatcher.bindImmediate("edit.redo",
        [](const CommandContext& c) { return c.canRedo ? yes() : no("No redo entry", "history"); },
        [this](const CommandRequest&, const CommandContext&) {
            auto before = m_session.snapshot(m_session.document().shape());
            const auto target = m_session.historyTarget(true);
            m_session.restore(target); m_session.commitTraversal(true, std::move(before));
            return CommandResult{CommandState::Completed, "Redone", {}, {}, true};
        });
    m_dispatcher.bindImmediate("inspect.measure",
        [this](const CommandContext& c) {
            if (c.workspace != Workspace::Part || m_session.document().shape().IsNull())
                return no("A solid part is required", "solid part");
            for (const auto& reference : c.selection)
                if (reference.generation != c.topologyGeneration)
                    return no("Selected geometry is stale; select it again", "current selection");
            try { (void)measurementShapes(m_session.document(), c.selection); }
            catch (const std::invalid_argument& error) { return no(error.what(), "valid selection"); }
            return yes();
        },
        [this](const CommandRequest&, const CommandContext& c) {
            const auto shapes = measurementShapes(m_session.document(), c.selection);
            auto measured = modeling::measureGeometry(shapes.front(),
                shapes.size() == 2 ? shapes.back() : TopoDS_Shape{});
            if (!measured) return CommandResult{CommandState::Rejected, "Could not measure this selection"};
            CommandResult result{CommandState::Completed, "Measurement ready"};
            result.measurement = std::move(measured);
            return result;
        });
    m_dispatcher.bindInteractive("sketch.line",
        [](const CommandContext& c) { return c.activeSketchId.empty() ? no("An active sketch is required", "sketch") : yes(); },
        [this](const CommandRequest&, const CommandContext&) { return std::make_unique<LineSession>(m_sketchTool); });
    for (const auto& id : {"feature.extrude", "feature.extruded_cut"})
        m_dispatcher.bindInteractive(id,
            [this, id](const CommandContext& c) {
                if (id == std::string("feature.extruded_cut") && m_session.document().shape().IsNull())
                    return no("A solid body is required for Cut", "body");
                return c.workspace == Workspace::Part || c.workspace == Workspace::Sketch ? yes() :
                    no("A part is required", "part");
            },
            [this, id](const CommandRequest& request, const CommandContext& c) {
                const auto sketch = value<std::string>(request, "sketch");
                return std::make_unique<FeatureSession>(m_session, id == std::string("feature.extruded_cut"),
                    sketch.value_or(c.activeSketchId.empty() ? m_session.selectedFeatureId() : c.activeSketchId));
            });
    m_dispatcher.bindInteractive("reference.plane",
        [](const CommandContext& c) { return c.documentType == DocumentType::Part ? yes() : no("A part is required", "part"); },
        [this](const CommandRequest&, const CommandContext&) { return std::make_unique<PlaneSession>(m_session); });
}
CommandContext PartCommands::context() const {
    CommandContext c;
    c.documentId = "part:" + std::to_string(m_session.identity());
    c.revision = m_session.revision();
    c.documentType = DocumentType::Part;
    c.workspace = m_session.editingSketch() ? Workspace::Sketch : Workspace::Part;
    c.activeSketchId = m_session.editingSketchId();
    c.modelingMode = m_sketchTool.mode() == SketchToolController::Mode::Draw ? "sketch.draw" :
        m_sketchTool.mode() == SketchToolController::Mode::Dimension ? "sketch.dimension" :
        m_sketchTool.mode() == SketchToolController::Mode::Relation ? "sketch.relation" :
        c.workspace == Workspace::Sketch ? "sketch.select" : "part";
    c.canUndo = m_session.canUndo(); c.canRedo = m_session.canRedo();
    c.selection = m_selection;
    c.topologyGeneration = m_session.revision();
    c.hasViewport = bool(m_isometric);
    return c;
}
} // namespace thecad::application
