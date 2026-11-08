#include "application/FeatureOperation.h"
#include "document/PartDocument.h"
#include "modeling/SketchArcs.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace thecad;
using namespace thecad::modeling;
namespace {
void check(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

void expectVolume(const document::PartDocument& part, double expected)
{
    const auto actual = volume(part.shape());
    if (std::abs(actual - expected) > std::max(1e-5, expected * 1e-8))
        throw std::runtime_error("Expected volume " + std::to_string(expected) + ", actual " + std::to_string(actual));
    check(BRepCheck_Analyzer(part.shape()).IsValid(), "Extrusion must produce valid topology.");
}

std::string profile(document::PartDocument& part, Point2 low = {1, 1}, Point2 high = {5, 4})
{
    Sketch sketch;
    sketch.entities.push_back({EntityKind::Rectangle, low, high, 1, "profile"});
    return part.addSketch(sketch);
}

document::Feature feature(const std::string& sketch, bool cut = false)
{
    document::Feature result;
    result.kind = cut ? document::FeatureKind::Cut : document::FeatureKind::Extrude;
    result.sketchId = sketch;
    result.contour = {"profile"};
    result.extrusion.emplace();
    return result;
}

GeometryReference datum(document::PartDocument& part, DatumMethod method,
                         gp_Pnt position, gp_Vec direction = {0, 0, 1})
{
    DatumDefinition definition;
    definition.method = method;
    definition.position = position;
    definition.direction = direction;
    GeometryReference reference;
    reference.source = ReferenceSource::Datum;
    reference.featureId = part.addReferenceGeometry(definition);
    return reference;
}

GeometryReference face(const document::PartDocument& part, GeomAbs_SurfaceType type, double z = 0)
{
    for (const auto& option : part.referenceOptions(false, true)) {
        if (option.reference.source != ReferenceSource::BodyFace || option.reference.topologyName.empty()) continue;
        BRepAdaptor_Surface surface(TopoDS::Face(option.selectionShape));
        if (surface.GetType() == type && (type != GeomAbs_Plane ||
            (std::abs(surface.Plane().Axis().Direction().Z()) > .999 &&
             std::abs(surface.Plane().Location().Z() - z) < 1e-6)))
            return option.reference;
    }
    throw std::runtime_error("No named target face found.");
}

void offsetAndDirections(const QString& path)
{
    auto part = document::PartDocument::createEmpty();
    const auto point = datum(part, DatumMethod::PointXYZ, {0, 0, 9});
    auto f = feature(profile(part));
    f.extrusion->start = ExtrudeStart::Offset;
    f.extrusion->startOffset = 2;
    f.extrusion->first.end = ExtrudeEnd::Vertex;
    f.extrusion->first.target = point;
    f.extrusion->second.emplace().depth = 3;
    const auto result = application::FeatureOperation::preview(part, f, {f.contour}, false);
    check(part.shape().IsNull(), "Preview committed the part.");
    expectVolume(result.document, 120);
    result.document.save(path);
    auto loaded = document::PartDocument::load(path);
    expectVolume(loaded, 120);
    const auto& saved = loaded.features().back();
    check(saved.extrusion && saved.extrusion->second && saved.extrusion->first.target &&
          saved.extrusion->first.target->featureId == point.featureId, "Extrusion options did not survive reopening.");
    check(std::find(saved.dependencies.begin(), saved.dependencies.end(), point.featureId) != saved.dependencies.end(),
          "The target datum was not registered as a dependency.");
    auto reference = loaded.features().front();
    reference.reference.position.SetZ(11);
    loaded.replaceFeature(reference);
    expectVolume(loaded, 144);
    const double before = volume(loaded.shape());
    reference.reference.position.SetZ(0);
    bool rejected = false;
    try { loaded.replaceFeature(reference); } catch (const std::exception&) { rejected = true; }
    check(rejected && volume(loaded.shape()) == before, "A failed limit edit must be transactional.");
}

void planarLimits()
{
    auto part = document::PartDocument::createEmpty();
    const auto plane = datum(part, DatumMethod::PlaneXYZ, {0, 0, 10}, {-1, 0, 1});
    const auto from = datum(part, DatumMethod::PointXYZ, {0, 0, 2});
    auto f = feature(profile(part));
    f.extrusion->start = ExtrudeStart::Vertex;
    f.extrusion->startReference = from;
    f.extrusion->first.end = ExtrudeEnd::SurfaceOffset;
    f.extrusion->first.target = plane;
    f.extrusion->first.translateSurface = true;
    f.extrusion->first.offset = 3;
    part.addSolidFeature(f);
    expectVolume(part, 96);

    part = document::PartDocument::createEmpty();
    const auto axis = datum(part, DatumMethod::AxisXYZ, {0, 0, 0}, {1, 0, 1});
    f = feature(profile(part));
    f.extrusion->direction = axis;
    f.extrusion->first.end = ExtrudeEnd::MidPlane;
    f.extrusion->first.depth = 8;
    part.addSolidFeature(f);
    expectVolume(part, 96 / std::sqrt(2.0));
}

void cutLimitsAndFlip()
{
    auto original = document::PartDocument::createBox({20, 20, 10});
    const auto top = face(original, GeomAbs_Plane, 10);
    auto f = feature(profile(original), true);
    for (const auto end : {ExtrudeEnd::Surface, ExtrudeEnd::Body, ExtrudeEnd::UpToNext, ExtrudeEnd::ThroughAllBoth}) {
        auto part = original;
        f.extrusion->first.end = end;
        f.extrusion->first.target = end == ExtrudeEnd::Surface || end == ExtrudeEnd::Body ?
            std::optional<GeometryReference>(top) : std::nullopt;
        part.addSolidFeature(f);
        expectVolume(part, 4000 - 120);
    }
    f.extrusion->first = {};
    f.extrusion->first.depth = 2;
    f.extrusion->flipCut = true;
    original.addSolidFeature(f);
    expectVolume(original, 4000 - (400 - 12) * 2);
}

void draftAndThin()
{
    auto part = document::PartDocument::createEmpty();
    auto f = feature(profile(part, {0, 0}, {10, 10}));
    f.extrusion->first.depth = 5;
    f.extrusion->first.draftAngle = 5;
    auto outward = part;
    outward.addSolidFeature(f);
    const double tapered = volume(outward.shape());
    check(tapered < 500 && tapered > 400, "Inward draft did not shrink the profile.");
    outward = part;
    f.extrusion->first.draftOutward = true;
    outward.addSolidFeature(f);
    check(volume(outward.shape()) > 500, "Outward draft did not enlarge the profile.");

    f.extrusion->first.draftAngle = 0;
    f.extrusion->thin.emplace();
    f.extrusion->thin->mode = ThinMode::MidPlane;
    f.extrusion->thin->thickness = 2;
    auto uncapped = part;
    uncapped.addSolidFeature(f);
    expectVolume(uncapped, (12 * 12 - 8 * 8) * 5);
    f.extrusion->thin->endCap = 1;
    part.addSolidFeature(f);
    expectVolume(part, (12 * 12 - 8 * 8) * 5 + 8 * 8 * 2);

    part = document::PartDocument::createEmpty();
    Sketch open;
    open.entities.push_back({EntityKind::Line, {0, 0}, {10, 0}, 1, "line"});
    f = feature(part.addSketch(open));
    f.contour = {"line"};
    f.extrusion->thin.emplace();
    f.extrusion->thin->mode = ThinMode::TwoDirections;
    f.extrusion->thin->thickness = 2;
    f.extrusion->thin->secondThickness = 3;
    f.extrusion->first.depth = 4;
    part.addSolidFeature(f);
    expectVolume(part, 200);

    part = document::PartDocument::createEmpty();
    open.entities.push_back({EntityKind::Line, {10, 0}, {10, 10}, 1, "turn"});
    f = feature(part.addSketch(open));
    f.contour = {"line", "turn"};
    f.extrusion->thin.emplace().cornerRadius = 1;
    part.addSolidFeature(f);
    check(BRepCheck_Analyzer(part.shape()).IsValid(), "Rounded thin wall is invalid.");
    bool rounded = false;
    for (TopExp_Explorer faces(part.shape(), TopAbs_FACE); faces.More(); faces.Next())
        rounded |= BRepAdaptor_Surface(TopoDS::Face(faces.Current())).GetType() == GeomAbs_Cylinder;
    check(rounded, "Auto-fillet did not create rounded walls.");

    part = document::PartDocument::createEmpty();
    open.entities = {threePointArc({5, 0}, {-5, 0}, {0, 5}, "arc")};
    f = feature(part.addSketch(open));
    f.contour = {"arc"};
    f.extrusion->thin.emplace().mode = ThinMode::MidPlane;
    f.extrusion->thin->thickness = 2;
    f.extrusion->first.depth = 3;
    part.addSolidFeature(f);
    expectVolume(part, 30 * std::acos(-1.0));
}

void mergeAndScope()
{
    auto part = document::PartDocument::createBox({20, 20, 10});
    auto f = feature(profile(part, {30, 0}, {50, 20}));
    f.extrusion->merge = false;
    part.addSolidFeature(f);
    expectVolume(part, 8000);
    int bodies = 0;
    for (TopExp_Explorer solids(part.shape(), TopAbs_SOLID); solids.More(); solids.Next()) ++bodies;
    check(bodies == 2, "Merge off must retain separate bodies.");
    const auto target = face(part, GeomAbs_Plane, 10);
    f = feature(profile(part, {1, 1}, {49, 19}), true);
    f.extrusion->scope.push_back(target);
    f.extrusion->first.end = ExtrudeEnd::ThroughAll;
    part.addSolidFeature(f);
    expectVolume(part, 8000 - 19 * 18 * 10);
}

void addCurvedFeature(document::PartDocument& part, const document::Feature& f, const char* label)
{
    try {
        part.addSolidFeature(f);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(label) + ": " + error.what());
    }
}

void curvedLimit()
{
    auto part = document::PartDocument::createEmpty();
    Sketch cylinder;
    cylinder.plane = SketchPlane::XZ;
    cylinder.entities.push_back({EntityKind::Circle, {0, 15}, {}, 5, "circle"});
    const auto cylinderSketch = part.addSketch(cylinder);
    part.addExtrude(cylinderSketch, {"circle"}, 20, Direction::Negative);
    const auto limit = face(part, GeomAbs_Cylinder);
    auto f = feature(profile(part, {-2, 2}, {2, 6}));
    f.extrusion->merge = false;
    f.extrusion->first.end = ExtrudeEnd::Surface;
    f.extrusion->first.target = limit;
    auto offset = part;
    auto normalOffset = part;
    auto bodyLimit = part;
    auto fromSurface = part;
    part.addSolidFeature(f);
    const auto cylinderVolume = 500 * std::acos(-1.0);
    const auto toolVolume = (15 * 4 - (2 * std::sqrt(21.0) + 25 * std::asin(0.4))) * 4;
    expectVolume(part, cylinderVolume + toolVolume);
    f.extrusion->first.end = ExtrudeEnd::SurfaceOffset;
    f.extrusion->first.translateSurface = true;
    f.extrusion->first.offset = 2;
    offset.addSolidFeature(f);
    expectVolume(offset, cylinderVolume + toolVolume - 32);

    f.extrusion->first.translateSurface = false;
    addCurvedFeature(normalOffset, f, "Normal offset from a cylinder");
    const auto wideTool = (15 * 4 - (2 * std::sqrt(45.0) + 49 * std::asin(2.0 / 7))) * 4;
    expectVolume(normalOffset, cylinderVolume + wideTool);

    f.extrusion->first = {};
    f.extrusion->first.end = ExtrudeEnd::Body;
    f.extrusion->first.target = limit;
    addCurvedFeature(bodyLimit, f, "Up to a cylindrical body");
    expectVolume(bodyLimit, cylinderVolume + toolVolume);

    f.extrusion->first = {};
    f.extrusion->first.depth = 30;
    f.extrusion->start = ExtrudeStart::Surface;
    f.extrusion->startReference = limit;
    addCurvedFeature(fromSurface, f, "Start from a cylindrical surface");
    expectVolume(fromSurface, cylinderVolume + toolVolume);
}

void treatDraftedLip(document::PartDocument part, double side, const QString& path)
{
    const auto options = part.edgeOptions();
    const auto edge = std::find_if(options.begin(), options.end(), [&](const auto& option) {
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(option.selectionShape, bounds, false, false);
        double x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        return std::abs(z0) < 1e-6 && std::abs(z1) < 1e-6 &&
               side * (y0 + y1) / 2 > 30 && x1 - x0 > 60;
    });
    check(edge != options.end(), "The outer drafted lip must have a persistent edge reference.");
    for (const auto kind : {document::FeatureKind::Chamfer, document::FeatureKind::Fillet}) {
        auto candidate = part;
        document::Feature treatment;
        treatment.kind = kind;
        treatment.size = 0.5;
        treatment.edges.push_back(edge->reference);
        candidate.addSolidFeature(treatment);
        const auto resultVolume = volume(candidate.shape());
        check(resultVolume < volume(part.shape()), "The drafted edge treatment must remove material.");
        candidate.save(path);
        auto reopened = document::PartDocument::load(path);
        expectVolume(reopened, resultVolume);
        auto draft = reopened.features().at(1);
        draft.extrusion->second->draftAngle = 12;
        reopened.replaceFeature(draft);
        check(BRepCheck_Analyzer(reopened.shape()).IsValid(), "Changing the draft must retain the edge treatment.");
    }
}

void draftedEdgeTreatments(const QString& path)
{
    auto part = document::PartDocument::createEmpty();
    auto boss = feature(profile(part, {-80, 0}, {0, 40}));
    boss.direction = Direction::Negative;
    boss.extrusion->start = ExtrudeStart::Offset;
    boss.extrusion->startOffset = -10;
    boss.extrusion->first.depth = 8;
    boss.extrusion->second.emplace().draftAngle = 10;
    const auto id = part.addSolidFeature(boss);
    treatDraftedLip(part, 1, path);
    document::Feature mirror;
    mirror.kind = document::FeatureKind::Mirror;
    mirror.pattern.seeds.push_back(id);
    GeometryReference plane;
    plane.source = ReferenceSource::BasePlane;
    plane.index = 1;
    mirror.pattern.reference = plane;
    part.addSolidFeature(mirror);
    treatDraftedLip(part, -1, path);

    std::vector<GeometryReference> junction;
    for (const auto& option : part.edgeOptions()) {
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(option.selectionShape, bounds, false, false);
        double x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        if (std::abs(x0) < 1e-6 && std::abs(x1) < 1e-6 &&
            std::abs(z0 + 10) < 1e-6 && std::abs(z1 + 10) < 1e-6 &&
            y1 - y0 > 30)
            junction.push_back(option.reference);
    }
    check(junction.size() == 2, "Expected both halves of the straight mirror junction.");
    for (const auto kind : {document::FeatureKind::Chamfer, document::FeatureKind::Fillet}) {
        document::Feature treatment;
        treatment.kind = kind;
        treatment.size = 0.5;
        treatment.edges = junction;
        const auto preview = application::FeatureOperation::preview(part, treatment, {}, false);
        check(preview.changes.added.IsNull() && !preview.changes.removed.IsNull(),
              "The mirror-junction preview must show removed material.");
        check(BRepCheck_Analyzer(preview.previewShape).IsValid(), "The mirror junction must stay a valid solid.");
        preview.document.save(path);
        expectVolume(document::PartDocument::load(path), volume(preview.previewShape));
        if (kind == document::FeatureKind::Fillet) {
            bool exactRadius = false;
            for (TopExp_Explorer faces(preview.previewShape, TopAbs_FACE); faces.More(); faces.Next()) {
                const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
                if (surface.GetType() == GeomAbs_Cylinder &&
                    std::abs(surface.Cylinder().Radius() - treatment.size) < 1e-7) exactRadius = true;
            }
            check(exactRadius, "The fallback must keep the requested radius.");
        }
    }
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "Could not create test directory.");
        offsetAndDirections(directory.filePath("extrusion.thepart"));
        planarLimits();
        cutLimitsAndFlip();
        draftAndThin();
        mergeAndScope();
        curvedLimit();
        draftedEdgeTreatments(directory.filePath("drafted-edge.thepart"));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
