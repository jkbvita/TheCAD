#include "document/PartDocument.h"
#include "application/DocumentSession.h"
#include "application/FeatureOperation.h"
#include "modeling/PlanarEdgeTreatment.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
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

void trimmedPlanarTerminations()
{
    for (const double slope : {0.5, 5.0}) {
        BRepBuilderAPI_MakePolygon outline(gp_Pnt(0, 0, 0), gp_Pnt(20, 0, 0),
                                          gp_Pnt(20 + 10 * slope, 10, 0),
                                          gp_Pnt(-10 * slope, 10, 0), true);
        const auto stock =
            BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(outline.Wire()).Face(), gp_Vec(0, 0, 10))
                .Shape();
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(stock, TopAbs_EDGE, edges);
        TopoDS_Shape crease;
        for (int i = 1; i <= edges.Extent(); ++i) {
            const BRepAdaptor_Curve curve(TopoDS::Edge(edges(i)));
            const auto midpoint = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2);
            if (midpoint.IsEqual(gp_Pnt(10, 0, 0), 1e-7)) crease = edges(i);
        }
        require(!crease.IsNull(), "Missing test crease");
        for (const bool chamfer : {false, true}) {
            modeling::TopologyNames names;
            names.add("crease", crease);
            auto legacyNames = names;
            const auto legacy = modeling::cutPlanarEdgeTreatments(stock, {crease}, 2, chamfer,
                                                                  legacyNames, "test", modeling::EdgeTermination::Legacy);
            const auto start = modeling::topologyKey("treatment-start", {"test", "crease"});
            const auto end = modeling::topologyKey("treatment-end", {"test", "crease"});
            require(!legacy.IsNull() && !legacyNames.images.at(start).empty() &&
                        !legacyNames.images.at(end).empty(),
                    "Fixture must expose both old cutter caps");
            const auto result =
                modeling::cutPlanarEdgeTreatments(stock, {crease}, 2, chamfer, names, "test",
                                                 modeling::EdgeTermination::Trimmed);
            if (slope > 1) {
                require(result.IsNull() && names.images.size() == 1,
                        "A cutter that ends inside the solid must fail transactionally");
                continue;
            }
            require(!result.IsNull() && BRepCheck_Analyzer(result).IsValid(),
                    "Inclined end faces must trim the treatment");
            require(names.images.count(start) == 0 && names.images.count(end) == 0,
                    "The result must not contain cutter caps");
            const auto face = names.resolve(modeling::topologyKey("treatment", {"test", "crease"}),
                                            TopAbs_FACE);
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(face, bounds, false, false);
            double xmin, ymin, zmin, xmax, ymax, zmax;
            bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            require(closeEnough(xmin, -1) && closeEnough(xmax, 21),
                    "The treatment must follow the inclined ends past the original edge tips");
            const BRepAdaptor_Surface surface(TopoDS::Face(face));
            require(chamfer ? surface.GetType() == GeomAbs_Plane
                            : surface.GetType() == GeomAbs_Cylinder &&
                                  closeEnough(surface.Cylinder().Radius(), 2),
                    "Corner trimming changed the requested treatment section");
        }
    }
}

void legacyEdgeTreatmentRoundTrip(const QString& path)
{
    auto box = document::PartDocument::createBox();
    box.addSolidFeature(treatment(box));
    box.save(path);
    auto older = QJsonDocument::fromJson(bytes(path)).object();
    auto features = older["features"].toArray();
    auto fillet = features.last().toObject();
    fillet.remove("termination");
    features[features.size() - 1] = fillet;
    older["features"] = features;
    older["version"] = 24;
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write legacy fixture");
    file.write(QJsonDocument(older).toJson());
    file.close();
    auto loaded = document::PartDocument::load(path);
    require(loaded.features().back().edgeTermination == modeling::EdgeTermination::Legacy &&
                closeEnough(volume(loaded), volume(box)),
            "Older edge treatments must keep their original replay mode");
    roundTrip(loaded, path);
    auto edit = loaded.features().back();
    edit.size = 1;
    loaded.replaceFeature(edit);
    require(loaded.features().back().edgeTermination == modeling::EdgeTermination::PreserveDraftJunction,
            "Editing an edge treatment must use the corrected termination");
    roundTrip(loaded, path);
}

void expectCrispDraftFillet(const document::PartDocument& part, const std::string& id, double size)
{
    const auto fillet = part.edgeTreatmentFaces(id);
    require(fillet.size() == 2, "Both fillet runs must retain their own face");
    for (const auto& face : fillet) {
        const BRepAdaptor_Surface surface(TopoDS::Face(face));
        require(surface.GetType() == GeomAbs_Cylinder && closeEnough(surface.Cylinder().Radius(), size),
                "The two draft junction runs must retain the requested radius");
    }
    TopTools_IndexedMapOfShape boundary;
    TopExp::MapShapes(fillet[0], TopAbs_EDGE, boundary);
    TopTools_ListOfShape sharpJoins;
    for (TopExp_Explorer edges(fillet[1], TopAbs_EDGE); edges.More(); edges.Next())
        if (boundary.Contains(edges.Current()) &&
            BRep_Tool::Continuity(TopoDS::Edge(edges.Current()), TopoDS::Face(fillet[0]),
                                 TopoDS::Face(fillet[1])) == GeomAbs_C0)
            sharpJoins.Append(edges.Current());
    BRepBuilderAPI_MakeWire seam;
    seam.Add(sharpJoins);
    require(!sharpJoins.IsEmpty() && seam.IsDone() && !seam.Wire().Closed(),
            "The straight and drafted fillets must meet at a connected sharp seam");
}

void draftJunctionFillet(const QString& path)
{
    auto part = document::PartDocument::createEmpty();
    modeling::Sketch sketch;
    sketch.entities.push_back({modeling::EntityKind::Rectangle, {-45, 0}, {0, 64}, 1, "profile"});
    const auto sketchId = part.addSketch(sketch);
    part.addExtrude(sketchId, {"profile"}, 20, modeling::Direction::Positive);
    document::Feature draft;
    draft.kind = document::FeatureKind::Extrude;
    draft.sketchId = sketchId;
    draft.contour = {"profile"};
    draft.extrusion.emplace();
    draft.extrusion->start = modeling::ExtrudeStart::Offset;
    draft.extrusion->startOffset = 20;
    draft.extrusion->first.depth = 10;
    draft.extrusion->first.draftAngle = 20;
    part.addSolidFeature(draft);
    auto fillet = treatment(part);
    fillet.edges.clear();
    const gp_Pnt shoulder(-45, 0, 20);
    for (const auto& edge : part.edgeOptions(true)) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(edge.selectionShape));
        const auto first = curve.Value(curve.FirstParameter());
        const auto last = curve.Value(curve.LastParameter());
        if ((first.IsEqual(shoulder, 1e-7) || last.IsEqual(shoulder, 1e-7)) &&
            std::abs(first.Z() - last.Z()) > 1)
            fillet.edges.push_back(edge.reference);
    }
    require(fillet.edges.size() == 2, "Fixture must have a straight and drafted edge at the shoulder");
    const auto id = part.addSolidFeature(fillet);
    require(BRepCheck_Analyzer(part.shape()).IsValid(), "The draft fillet must leave a valid solid");
    expectCrispDraftFillet(part, id, 2);
    roundTrip(part, path);
}

void savedDraftJunction(const QString& source, const QString& path)
{
    auto part = document::PartDocument::load(source);
    const auto original = part.features();
    const auto firstFillet = std::find_if(original.begin(), original.end(), [](const auto& f) {
        return f.kind == document::FeatureKind::Fillet && f.edges.size() == 2;
    });
    require(firstFillet != original.end(), "Missing draft-junction fillet");
    part.replaceFeature(*firstFillet);
    expectCrispDraftFillet(part, firstFillet->id, firstFillet->size);
    require(part.features().size() == original.size() && BRepCheck_Analyzer(part.shape()).IsValid(),
            "Rebuilding the corner must preserve the downstream solid");
    for (std::size_t i = 0; i < original.size(); ++i)
        require(part.features()[i].id == original[i].id &&
                    part.features()[i].dependencies == original[i].dependencies,
                "Rebuilding the corner changed the downstream history");
    roundTrip(part, path);
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
        trimmedPlanarTerminations();
        legacyEdgeTreatmentRoundTrip(path);
        draftJunctionFillet(path);
        const auto savedDraft = qEnvironmentVariable("THECAD_TEST_DRAFT_JUNCTION_PART");
        if (!savedDraft.isEmpty()) savedDraftJunction(savedDraft, path);
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
