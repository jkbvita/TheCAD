#include "application/PartCommands.h"
#include "application/AssemblyCommands.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace thecad;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double volume(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return 0;
    GProp_GProps properties; BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        application::PartCommands part;
        auto& commands = part.dispatcher();
        const auto items = commands.registry().enumerate();
        require(items.size() > 40, "Command catalog is incomplete");
        require(!commands.registry().add(items.front()), "Duplicate command ID was accepted");
        require(commands.registry().find("assembly.mate") &&
                commands.registry().find("assembly.mate")->interactive &&
                commands.registry().find("assembly.save") &&
                !commands.registry().find("assembly.save")->interactive,
                "Assembly command lifecycle metadata is wrong");
        require(!commands.availability("feature.revolve").available,
                "Legacy command falsely advertised as dispatchable");
        require(commands.registry().find("app.welcome") && commands.registry().find("file.open_recent") &&
                !commands.availability("app.welcome").available && !commands.availability("file.open_recent").available,
                "Welcome commands must be cataloged without advertising headless UI support");
        require(!commands.availability("sketch.line").available, "Line available without sketch");
        require(commands.availability("sketch.line").reason.find("workspace") != std::string::npos,
                "Unavailable command has no reason");
        bool viewed = false;
        require(!commands.availability("view.isometric").available, "View available without viewport");
        part.setViewCallback([&] { viewed = true; });
        require(bool(commands.execute({"view.isometric"})), "Isometric dispatch failed");
        require(viewed && !part.session().canUndo(), "View command modified history");
        application::CommandContext stale;
        stale.documentId = "stale-selection"; stale.workspace = application::Workspace::Part;
        stale.topologyGeneration = 2;
        modeling::SelectionRef staleFace;
        staleFace.kind = modeling::SelectionKind::Face; staleFace.generation = 1;
        stale.selection.push_back(staleFace);
        application::CommandDispatcher staleDispatcher([&] { return stale; });
        application::registerBuiltInCommands(staleDispatcher.registry());
        staleDispatcher.bindImmediate("view.isometric", [](const auto&) {
            return application::CommandAvailability{true, {}, {}};
        }, [](const auto&, const auto&) {
            return application::CommandResult{application::CommandState::Completed};
        });
        require(!staleDispatcher.execute({"view.isometric"}), "Stale face selection accepted");

        modeling::Sketch sketch;
        sketch.entities = {{modeling::EntityKind::Rectangle, {-10, -10}, {10, 10}, 1, "profile"}};
        application::PartCommands drawing;
        drawing.session().editingSketch() = sketch;
        drawing.session().editingSketchId() = "active-sketch";
        const auto line = drawing.dispatcher().execute({"sketch.line"});
        require(line.state == application::CommandState::Started &&
                drawing.sketchTool().drawing() == modeling::EntityKind::Line,
                "Line tool did not activate");
        require(drawing.dispatcher().execute({"command.cancel"}).state == application::CommandState::Canceled &&
                drawing.sketchTool().mode() == application::SketchToolController::Mode::Select,
                "Line cancel did not restore select mode");
        auto document = document::PartDocument::createEmpty();
        const auto sketchId = document.addSketch(sketch);
        application::PartCommands feature(std::move(document));
        feature.session().selectedFeatureId() = sketchId;
        auto& dispatcher = feature.dispatcher();
        require(!dispatcher.execute({"feature.extrude", std::uint64_t{999}}),
                "Stale document revision accepted");
        const auto start = dispatcher.execute({"feature.extrude"});
        require(start.state == application::CommandState::Started, "Extrude did not start");
        require(!dispatcher.execute({"feature.extrude"}), "Second interactive command replaced active session");
        application::CommandRequest wrong{"feature.extrude"};
        wrong.parameters["unknown"] = application::Length{1};
        require(!dispatcher.update(start.sessionId, wrong), "Unknown parameter was accepted");
        wrong.id = "reference.plane";
        require(!dispatcher.update(start.sessionId, wrong), "Wrong command updated active session");
        application::CommandRequest depth{"feature.extrude"};
        depth.parameters["depth"] = application::Length{25};
        require(dispatcher.update(start.sessionId, depth).state == application::CommandState::PreviewReady,
                "Typed depth did not generate preview");
        require(feature.session().document().shape().IsNull() && !feature.session().canUndo(),
                "Preview mutated committed document/history");
        require(!dispatcher.confirm("wrong-session"), "Wrong session confirmed");
        require(dispatcher.execute({"command.cancel"}).state == application::CommandState::Canceled,
                "Cancel failed");
        require(feature.session().document().shape().IsNull() && !feature.session().canUndo(),
                "Cancel changed document/history");
        const auto second = dispatcher.execute({"feature.extrude"});
        require(dispatcher.update(second.sessionId, depth).state == application::CommandState::PreviewReady,
                "Extrude preview after cancel failed");
        require(dispatcher.execute({"command.confirm"}).state == application::CommandState::Committed,
                "Extrude did not commit");
        require(std::abs(volume(feature.session().document().shape()) - 10000) < 1e-6,
                "Extrude used incorrect depth");
        require(feature.session().canUndo(), "Extrude did not record undo");
        require(bool(dispatcher.execute({"edit.undo"})), "Undo dispatch failed");
        require(feature.session().document().shape().IsNull(), "Undo did not restore part");
        require(bool(dispatcher.execute({"edit.redo"})), "Redo dispatch failed");
        require(std::abs(volume(feature.session().document().shape()) - 10000) < 1e-6,
                "Redo did not restore extrusion");
        require(!dispatcher.availability("inspect.measure").available,
                "Measure was available without a selected shape");
        const auto topology = feature.session().document().namedTopologyOptions();
        const auto edge = std::find_if(topology.begin(), topology.end(), [](const auto& item) {
            return item.reference.source == modeling::ReferenceSource::BodyEdge;
        });
        require(edge != topology.end(), "Extruded part has no measurable edge");
        modeling::SelectionRef selectedEdge;
        selectedEdge.kind = modeling::SelectionKind::Edge;
        selectedEdge.featureId = edge->reference.featureId;
        selectedEdge.index = edge->reference.index;
        selectedEdge.generation = feature.session().revision();
        feature.setSelection({selectedEdge});
        require(dispatcher.availability("inspect.measure").available,
                "Selected edge could not be measured");
        const auto measured = dispatcher.execute({"inspect.measure"});
        require(measured.measurement && measured.measurement->length && !measured.documentChanged,
                "Measure did not return a read-only length");
        selectedEdge.generation = 0;
        feature.setSelection({selectedEdge});
        require(!dispatcher.availability("inspect.measure").available,
                "Stale edge reference was available for measurement");
        selectedEdge.generation = feature.session().revision();
        selectedEdge.index = 9999;
        feature.setSelection({selectedEdge});
        require(!dispatcher.availability("inspect.measure").available,
                "Invalid edge reference was available for measurement");

        application::PartCommands reference;
        auto& refs = reference.dispatcher();
        const auto plane = refs.execute({"reference.plane"});
        modeling::DatumDefinition definition;
        definition.method = modeling::DatumMethod::PlaneXYZ;
        definition.position = gp_Pnt(0, 0, 12);
        definition.direction = gp_Vec(0, 0, 1);
        application::CommandRequest planeInput{"reference.plane"};
        planeInput.parameters["definition"] = definition;
        require(refs.update(plane.sessionId, planeInput).state == application::CommandState::PreviewReady,
                "Plane preview failed");
        require(reference.session().document().features().empty(), "Plane preview committed early");
        refs.cancel(plane.sessionId);
        require(reference.session().document().features().empty() && !reference.session().canUndo(),
                "Canceled plane recorded history");
        const auto plane2 = refs.execute({"reference.plane"});
        require(refs.update(plane2.sessionId, planeInput).state == application::CommandState::PreviewReady,
                "Plane preview retry failed");
        require(refs.confirm(plane2.sessionId).state == application::CommandState::Committed,
                "Plane commit failed");
        require(reference.session().document().features().size() == 1, "Plane absent after commit");
        refs.execute({"edit.undo"});
        require(reference.session().document().features().empty(), "Plane undo failed");
        const auto stalePlane = refs.execute({"reference.plane"});
        reference.session().record(reference.session().snapshot(reference.session().document().shape()));
        require(!refs.update(stalePlane.sessionId, planeInput),
                "Active command accepted a changed document revision");
        require(!refs.confirm(stalePlane.sessionId), "Stale plane command committed");
        require(refs.cancel(stalePlane.sessionId).state == application::CommandState::Canceled,
                "Stale plane session could not be canceled");

        QTemporaryDir directory;
        const auto path = directory.filePath("component.thepart");
        document::PartDocument::createBox().save(path);
        document::AssemblyDocument assembly;
        const auto first = assembly.insertComponent(path);
        const auto secondComponent = assembly.insertComponent(path);
        application::AssemblyCommands assemblyCommands(std::move(assembly));
        assemblyCommands.selectComponent(secondComponent);
        auto& assemblyDispatcher = assemblyCommands.dispatcher();
        require(!assemblyDispatcher.availability("sketch.line").available,
                "Sketch command available in assembly");
        const bool oldFixed = assemblyCommands.session().document().component(secondComponent).fixed;
        require(bool(assemblyDispatcher.execute({"assembly.fix_float"})), "Assembly command failed");
        require(assemblyCommands.session().document().component(secondComponent).fixed != oldFixed,
                "Assembly component unchanged");
        require(bool(assemblyDispatcher.execute({"edit.undo"})), "Assembly undo failed");
        require(assemblyCommands.session().document().component(secondComponent).fixed == oldFixed,
                "Assembly undo did not restore component");
        (void)first;
        std::cout << "PASS: registry, context, typed preview, cancel, commit, undo/redo and assembly dispatch\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
