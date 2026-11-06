#include "application/DocumentSession.h"
#include "application/FeatureOperation.h"
#include "application/SketchToolController.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace thecad;
namespace
{
void require(bool ok, const char* why)
{
    if (!ok)
        throw std::runtime_error(why);
}
double volume(const TopoDS_Shape& shape)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(shape, p);
    return p.Mass();
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        application::DocumentSession session(document::PartDocument::createBox());
        session.setPath("saved.thepart");
        session.markSaved();
        require(!session.modified(), "Saved session dirty");
        for (int i = 1; i <= 12; ++i)
        {
            auto previous = session.snapshot(session.document().shape());
            auto feature = session.document().features().front();
            feature.box.width = 100 + i;
            session.document().replaceFeature(feature);
            session.record(std::move(previous));
        }
        require(session.modified(), "Edited session clean");
        int undo = 0;
        while (session.canUndo())
        {
            auto previous = session.snapshot(session.document().shape());
            const auto target = session.historyTarget(false);
            session.restore(target);
            session.commitTraversal(false, std::move(previous));
            ++undo;
        }
        require(undo == 10 && session.document().parameters().width == 102,
                "Ten-action history changed");
        while (session.canRedo())
        {
            auto previous = session.snapshot(session.document().shape());
            const auto target = session.historyTarget(true);
            session.restore(target);
            session.commitTraversal(true, std::move(previous));
        }
        require(session.document().parameters().width == 112, "Redo changed");
        auto previous = session.snapshot(session.document().shape());
        const auto target = session.historyTarget(false);
        session.restore(target);
        session.commitTraversal(false, std::move(previous));
        session.record(session.snapshot(session.document().shape()));
        require(!session.canRedo(), "Branched edit retains redo");
        session.clearHistory();
        require(!session.canUndo() && !session.canRedo(), "History reset failed");
        modeling::Sketch sketch;
        sketch.entities = {{modeling::EntityKind::Rectangle, {-10, -10}, {10, 10}, 1, "profile"}};
        session.editingSketch() = sketch;
        auto unchanged = session.snapshot({});
        require(!session.recordSketchChange(unchanged), "Unchanged sketch recorded");
        session.editingSketch()->entities.front().second.x = 15;
        require(session.recordSketchChange(unchanged), "Sketch edit omitted");
        auto source = document::PartDocument::createEmpty();
        const auto id = source.addSketch(sketch);
        document::Feature extrusion;
        extrusion.kind = document::FeatureKind::Extrude;
        extrusion.sketchId = id;
        extrusion.length = 12;
        extrusion.contour = {"profile"};
        auto result =
            application::FeatureOperation::preview(source, extrusion, {{"profile"}}, false);
        require(source.shape().IsNull(), "Preview changed source");
        require(std::abs(volume(result.document.shape()) - 4800) < 1e-6,
                "Preview geometry changed");
        const auto original = volume(result.document.shape());
        auto edited = result.document.features().back();
        edited.length = -1;
        bool rejected = false;
        try
        {
            (void)application::FeatureOperation::preview(result.document, edited, {{"profile"}},
                                                         true);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && std::abs(volume(result.document.shape()) - original) < 1e-6,
                "Failed preview changed source");
        QTemporaryDir dir;
        const auto path = dir.filePath("session.thepart");
        result.document.save(path);
        auto loaded = document::PartDocument::load(path);
        require(std::abs(volume(loaded.shape()) - original) < 1e-6,
                "Storage boundary changed geometry");
        application::SketchToolController tools;
        tools.draw(modeling::EntityKind::Line, true);
        require(tools.drawing() == modeling::EntityKind::Line && tools.construction(),
                "Construction tool changed");
        tools.dimension(true);
        require(!tools.drawing() && tools.dimensionPicking() && tools.constraintPicking(),
                "Dimension activation changed");
        tools.relations(true);
        require(!tools.dimensionPicking() && tools.constraintPicking(),
                "Relation activation changed");
        tools.select();
        require(!tools.drawing() && !tools.constraintPicking() && !tools.construction(),
                "Select activation changed");
        std::cout << "PASS: widget-independent session history, transactional feature previews, "
                     "storage and tool activation\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
