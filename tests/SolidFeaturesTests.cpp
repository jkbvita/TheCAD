#include "document/PartDocument.h"
#include "application/DocumentSession.h"
#include "application/FeatureOperation.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GProp_GProps.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad;
namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F function)
{
    bool rejected = false;
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "An invalid operation was accepted");
}
double volume(const document::PartDocument& doc)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(doc.shape(), p);
    return p.Mass();
}
bool closeEnough(double a, double b)
{
    return std::abs(a - b) < std::max(1e-6, std::abs(b) * 1e-8);
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read");
    return file.readAll();
}
modeling::Sketch profile(modeling::SketchPlane plane = modeling::SketchPlane::XY)
{
    modeling::Sketch s;
    s.plane = plane;
    s.entities.push_back({modeling::EntityKind::Rectangle, {10, 0}, {20, 30}, 1, "rectangle"});
    s.entities.push_back({modeling::EntityKind::Line, {0, 0}, {0, 40}, 1, "axis", true});
    return s;
}
document::Feature revolve(const std::string& sketchId, double angle = 360, bool cut = false)
{
    document::Feature f;
    f.kind = cut ? document::FeatureKind::RevolvedCut : document::FeatureKind::Revolve;
    f.sketchId = sketchId;
    f.contour = {"rectangle"};
    f.angle = angle;
    modeling::GeometryReference axis;
    axis.source = modeling::ReferenceSource::SketchLine;
    axis.featureId = sketchId;
    axis.entityId = "axis";
    f.axis = axis;
    return f;
}
document::Feature treatment(const document::PartDocument& doc, bool chamfer = false)
{
    document::Feature f;
    f.kind = chamfer ? document::FeatureKind::Chamfer : document::FeatureKind::Fillet;
    f.size = 2;
    const auto edges = doc.edgeOptions();
    require(!edges.empty(), "No named solid edges available");
    f.edges = {edges.front().reference};
    return f;
}
void roundTrip(document::PartDocument doc, const QString& path)
{
    doc.save(path);
    const auto saved = bytes(path);
    const auto loaded = document::PartDocument::load(path);
    require(closeEnough(volume(doc), volume(loaded)), "Reopened feature geometry changed");
    require(loaded.features().size() == doc.features().size(), "History not preserved");
    for (std::size_t i = 0; i < doc.features().size(); ++i)
    {
        require(loaded.features()[i].id == doc.features()[i].id, "ID changed");
        require(loaded.features()[i].dependencies == doc.features()[i].dependencies,
                "Dependencies changed");
    }
    loaded.save(path);
    require(saved == bytes(path), "Persistent feature parameters are not deterministic");
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir temp;
        require(temp.isValid(), "temp");
        const auto path = temp.filePath("solid-features.thepart");
        const double pi = std::acos(-1.0);
        for (const auto plane :
             {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ})
        {
            auto doc = document::PartDocument::createEmpty();
            const auto sketch = doc.addSketch(profile(plane));
            const auto id = doc.addSolidFeature(revolve(sketch));
            require(BRepCheck_Analyzer(doc.shape()).IsValid(), "Invalid full revolve");
            require(closeEnough(volume(doc), pi * 300 * 30), "Incorrect full revolve volume");
            require(doc.features().back().dependencies == std::vector<std::string>{sketch},
                    "Revolve dependencies wrong");
            roundTrip(doc, path);
            auto half = doc.features().back();
            half.angle = 180;
            half.direction = modeling::Direction::Negative;
            doc.replaceFeature(half);
            require(closeEnough(volume(doc), pi * 300 * 15), "Incorrect reverse half revolve");
            require(doc.features().back().id == id, "Revolve edit changed ID");
            roundTrip(doc, path);
            const auto before = doc.shape();
            half.angle = 361;
            rejects([&] { doc.replaceFeature(half); });
            require(doc.shape().IsSame(before), "Failed edit changed source");
            auto changed = doc.features().front();
            changed.sketch.entities[0].second.x = 25;
            doc.replaceFeature(changed);
            require(closeEnough(volume(doc), pi * (625 - 100) * 15),
                    "Revolve did not regenerate from sketch");
        }
        auto base = document::PartDocument::createEmpty();
        modeling::Sketch outer;
        outer.entities.push_back({modeling::EntityKind::Circle, {0, 0}, {}, 25, "outer"});
        const auto outerId = base.addSketch(outer);
        base.addExtrude(outerId, {"outer"}, 30, modeling::Direction::Positive);
        // Rotate in XZ around the sketch Y direction (world Z).
        auto cutProfile = profile(modeling::SketchPlane::XZ);
        const auto cutSketch = base.addSketch(cutProfile);
        auto cut = revolve(cutSketch, 180, true);
        const double full = volume(base);
        const auto source = base.shape();
        base.addSolidFeature(cut);
        require(closeEnough(volume(base), full - pi * 300 * 15),
                "Revolved Cut removed wrong material");
        GProp_GProps sourceProperties;
        BRepGProp::VolumeProperties(source, sourceProperties);
        require(closeEnough(sourceProperties.Mass(), full), "Source solid mutated by cut");
        roundTrip(base, path);
        auto cutEdit = base.features().back();
        cutEdit.direction = modeling::Direction::Negative;
        base.replaceFeature(cutEdit);
        require(closeEnough(volume(base), full - pi * 300 * 15), "Reverse cut wrong");
        auto invalid = document::PartDocument::createEmpty();
        const auto sid = invalid.addSketch(profile());
        rejects([&] { invalid.addSolidFeature(revolve(sid, 360, true)); });
        auto missing = revolve(sid);
        missing.axis.reset();
        rejects([&] { invalid.addSolidFeature(missing); });
        auto cross = invalid.features().front();
        cross.sketch.entities[0].first.x = -10;
        invalid.replaceFeature(cross);
        rejects([&] { invalid.addSolidFeature(revolve(sid)); });
        auto touching = document::PartDocument::createEmpty();
        auto onAxis = profile();
        onAxis.entities[0].first.x = 0;
        const auto touchingSketch = touching.addSketch(onAxis);
        auto touchingFeature = revolve(touchingSketch);
        modeling::DatumDefinition definition;
        definition.method = modeling::DatumMethod::AxisOnReference;
        definition.references = {*touchingFeature.axis};
        const auto datumId = touching.addReferenceGeometry(definition);
        modeling::GeometryReference axisRef;
        axisRef.source = modeling::ReferenceSource::Datum;
        axisRef.featureId = datumId;
        touchingFeature.axis = axisRef;
        touching.addSolidFeature(touchingFeature);
        require(closeEnough(volume(touching), pi * 400 * 30),
                "Axis-touching profile must produce a cylinder");
        roundTrip(touching, path);
        const auto dependent = touching.referenceDeletionDependents({datumId});
        require(dependent.size() == 1, "Axis dependency missing");
        rejects([&] { touching.deleteReferenceGeometry({datumId}); });
        auto torus = document::PartDocument::createEmpty();
        modeling::Sketch ring;
        ring.entities.push_back({modeling::EntityKind::Circle, {10, 0}, {}, 2, "circle"});
        const auto ringId = torus.addSketch(ring);
        auto ringFeature = revolve(ringId);
        ringFeature.contour = {"circle"};
        axisRef = {};
        axisRef.source = modeling::ReferenceSource::BaseAxis;
        axisRef.index = 1;
        ringFeature.axis = axisRef;
        torus.addSolidFeature(ringFeature);
        require(closeEnough(volume(torus), 2 * pi * pi * 10 * 4),
                "Circle revolve must produce a torus");
        roundTrip(torus, path);
        auto multiple = document::PartDocument::createEmpty();
        auto separated = profile();
        separated.entities.push_back(
            {modeling::EntityKind::Rectangle, {10, 40}, {20, 50}, 1, "second"});
        const auto multipleSketch = multiple.addSketch(separated);
        auto multipleFeature = revolve(multipleSketch);
        multipleFeature.contour.clear();
        multipleFeature.contours = {{"rectangle"}, {"second"}};
        multiple.addSolidFeature(multipleFeature);
        require(closeEnough(volume(multiple), pi * 300 * 40),
                "Multiple revolved regions have wrong volume");
        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(multiple.shape(), TopAbs_SOLID, solids);
        require(solids.Extent() == 2, "Disjoint revolved regions must retain both solids");
        roundTrip(multiple, path);
        for (const bool chamfer : {false, true})
        {
            auto box = document::PartDocument::createBox({40, 30, 20});
            const auto original = box.shape();
            const double old = volume(box);
            auto feature = treatment(box, chamfer);
            const auto selectedName = feature.edges.front().topologyName;
            const auto id = box.addSolidFeature(feature);
            require(volume(box) < old && volume(box) > old * 0.9, "Treatment volume invalid");
            require(BRepCheck_Analyzer(original).IsValid(), "Source box mutated");
            roundTrip(box, path);
            auto edit = box.features().back();
            edit.size = 1;
            box.replaceFeature(edit);
            require(volume(box) > old * 0.99, "Treatment edit did not update");
            auto sourceBox = box.features().front();
            sourceBox.box.width = 50;
            box.replaceFeature(sourceBox);
            require(box.features().back().edges.front().topologyName == selectedName,
                    "Persistent edge changed after source resize");
            require(box.features().back().id == id, "Treatment ID changed");
            roundTrip(box, path);
            const auto oldShape = box.shape();
            edit = box.features().back();
            edit.size = 1e6;
            rejects([&] { box.replaceFeature(edit); });
            require(box.shape().IsSame(oldShape), "Invalid treatment corrupted document");
            edit = box.features().back();
            edit.edges.push_back(edit.edges.front());
            rejects([&] { box.replaceFeature(edit); });
            edit = box.features().back();
            edit.edges[0].topologyName = "tn1:missing";
            rejects([&] { box.replaceFeature(edit); });
            // Generated faces/edges remain usable by downstream features.
            require(!box.edgeOptions().empty(), "Treatment failed to name downstream topology");
        }
        auto multi = document::PartDocument::createBox({40, 30, 20});
        auto fillet = treatment(multi);
        const auto options = multi.edgeOptions();
        fillet.edges.push_back(options[1].reference);
        multi.addSolidFeature(fillet);
        roundTrip(multi, path);
        // Circular solid edges (not just straight box edges) support both tools.
        for (bool chamfer : {false, true})
        {
            auto cylinder = touching;
            auto feature = treatment(cylinder, chamfer);
            feature.edges.clear();
            feature.size = 1;
            for (const auto& edge : cylinder.edgeOptions())
            {
                BRepAdaptor_Curve curve(TopoDS::Edge(edge.selectionShape));
                if (curve.GetType() == GeomAbs_Circle)
                {
                    feature.edges = {edge.reference};
                    break;
                }
            }
            require(!feature.edges.empty(), "Cylinder circular edge has no persistent name");
            cylinder.addSolidFeature(feature);
            roundTrip(cylinder, path);
        }
        application::DocumentSession session;
        session.document() = multi;
        session.markSaved();
        auto previous = session.snapshot(multi.shape());
        auto edited = multi.features().back();
        edited.size = 1;
        auto candidate = multi;
        candidate.replaceFeature(edited);
        session.record(previous);
        session.document() = candidate;
        require(session.canUndo(), "Feature edit missing undo");
        const auto undone = session.historyTarget(false);
        require(closeEnough(volume(undone.document), volume(multi)), "Undo geometry wrong");
        // Preview candidates preserve source document bytes and active BRep.
        multi.save(path);
        const auto originalBytes = bytes(path);
        const auto originalShape = multi.shape();
        const auto preview = application::FeatureOperation::preview(multi, edited, {}, true);
        require(preview.featureId == edited.id, "Preview ID wrong");
        multi.save(path);
        require(bytes(path) == originalBytes && multi.shape().IsSame(originalShape),
                "Preview mutated committed model");
        std::cout << "PASS: revolve/cut angles and planes, named edge fillets/chamfers, "
                     "regeneration, roundtrip, preview/undo and transactional failure\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
