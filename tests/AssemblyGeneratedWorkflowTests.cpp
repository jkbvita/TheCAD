#include "application/AssemblySession.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <gp_Quaternion.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad;
using namespace modeling::assembly;
namespace
{
constexpr double pi = 3.14159265358979323846;
void require(bool value, const char* error)
{
    if (!value)
        throw std::runtime_error(error);
}
template <class F> void rejects(F action)
{
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        return;
    }
    throw std::runtime_error("Invalid workflow transaction accepted");
}
modeling::GeometryReference base(bool axis, int index)
{
    modeling::GeometryReference ref;
    ref.source = axis ? modeling::ReferenceSource::BaseAxis : modeling::ReferenceSource::BasePlane;
    ref.index = index;
    return ref;
}
modeling::GeometryReference datum(const std::string& id)
{
    modeling::GeometryReference ref;
    ref.source = modeling::ReferenceSource::Datum;
    ref.featureId = id;
    return ref;
}
document::AssemblyMate pair(MateKind kind, document::AssemblyReference a,
                            document::AssemblyReference b)
{
    document::AssemblyMate m;
    m.kind = kind;
    m.first = a;
    m.second = b;
    m.definition = document::AssemblyMate::Definition{};
    m.definition->groups = {{"first", {a}}, {"second", {b}}};
    return m;
}
document::AssemblyReference cylinder(const document::AssemblyDocument& doc, const std::string& id)
{
    for (const auto& option : doc.referenceOptions(id))
        if (option.reference.geometry.source == modeling::ReferenceSource::BodyFace &&
            option.mateGeometry.kind == MateGeometryKind::Cylinder)
            return option.reference;
    throw std::runtime_error("Cylinder reference missing");
}
void close(const gp_Trsf& a, const gp_Trsf& b)
{
    for (int r = 1; r <= 3; ++r)
        for (int c = 1; c <= 4; ++c)
            require(std::abs(a.Value(r, c) - b.Value(r, c)) < 1e-7,
                    "Rigid placement changed on workflow reload");
}
void belts(const QString& folder)
{
    const auto wheelPath = folder + "/wheel.thepart", standPath = folder + "/stand.thepart",
               assemblyPath = folder + "/belt.theasmb";
    auto wheel = document::PartDocument::createEmpty();
    modeling::Sketch sketch;
    modeling::SketchEntity circle;
    circle.kind = modeling::EntityKind::Circle;
    circle.id = "rim";
    circle.radius = 5;
    sketch.entities.push_back(circle);
    const auto sid = wheel.addSketch(sketch);
    wheel.addExtrude(sid, {"rim"}, 10, modeling::Direction::Positive);
    wheel.save(wheelPath);
    auto stand = document::PartDocument::createBox({40, 10, 5});
    modeling::DatumDefinition definition;
    definition.method = modeling::DatumMethod::AxisXYZ;
    definition.position = {0, 0, 0};
    const auto axisA = stand.addReferenceGeometry(definition);
    definition.position = {30, 0, 0};
    const auto axisB = stand.addReferenceGeometry(definition);
    definition.method = modeling::DatumMethod::PlaneXYZ;
    definition.position = {0, 0, 10};
    const auto contextPlane = stand.addReferenceGeometry(definition);
    stand.save(standPath);
    document::AssemblyDocument doc;
    const auto mount = doc.insertComponent(standPath), a = doc.insertComponent(wheelPath);
    gp_Trsf pose;
    pose.SetTranslationPart({30, 0, 0});
    const auto b = doc.insertComponent(wheelPath, pose);
    doc.setMate(pair(MateKind::Concentric, {mount, datum(axisA)}, {a, base(true, 2)}));
    const auto slideMate =
        doc.setMate(pair(MateKind::Concentric, {mount, datum(axisB)}, {b, base(true, 2)}));
    doc.setMate(pair(MateKind::Coincident, {mount, base(false, 0)}, {a, base(false, 0)}));
    doc.setMate(pair(MateKind::Coincident, {mount, base(false, 0)}, {b, base(false, 0)}));
    document::AssemblyBelt belt;
    belt.pulleys = {cylinder(doc, a), cylinder(doc, b)};
    belt.createPart = true;
    belt.useThickness = true;
    belt.thickness = 1;
    const auto id = doc.setBelt(belt);
    const auto owned = doc.belts()[0].componentId;
    require(doc.components().size() == 4 && !doc.component(owned).embeddedPart.isEmpty() &&
                !doc.component(owned).shape().IsNull(),
            "Belt part was not generated");
    require(std::abs(doc.belts()[0].length - (60 + 11 * pi)) < 1e-6,
            "Thickness-adjusted belt length is wrong");
    const auto partIdentity = doc.component(owned).part;
    for (int step = 1; step <= 20; ++step)
    {
        const double angle = step * 0.2;
        gp_Trsf rotated;
        rotated.SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), angle));
        doc.setPlacement(a, rotated);
        const auto direction = gp_Dir(1, 0, 0).Transformed(doc.component(b).placement);
        require(direction.IsEqual(gp_Dir(std::cos(angle), std::sin(angle), 0), 1e-6),
                "Engaged belt did not rotate its second pulley");
        require(doc.component(owned).part == partIdentity,
                "Rigid pulley rotation rebuilt the generated belt sketch");
    }
    doc.setMateStatus({"mate-rotation-" + id}, MateStatus::Suppressed);
    require(!doc.belts()[0].engaged, "Suppressing the belt mate did not disengage the feature");
    const auto stationary = doc.component(b).placement;
    gp_Trsf rotated;
    rotated.SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), 4.8));
    doc.setPlacement(a, rotated);
    close(doc.component(b).placement, stationary);
    doc.setMateStatus({"mate-rotation-" + id}, MateStatus::Active);
    doc.deleteMates({slideMate});
    auto editing = doc.belts()[0];
    editing.driving = true;
    editing.length += 20;
    doc.setBelt(editing);
    require(std::abs(doc.component(b).placement.TranslationPart().Modulus() - 40) < 1e-4,
            "Driving belt length did not reposition the free pulley");
    const auto ownedPart = doc.createInPlacePart(document::PartDocument::createBox({2, 3, 4}),
                                                 {mount, datum(contextPlane)});
    require(std::abs(doc.component(ownedPart).placement.TranslationPart().Z() - 10) < 1e-7,
            "In-context part did not use its support");
    auto plane = *std::find_if(stand.features().begin(), stand.features().end(),
                               [&](const auto& f) { return f.id == contextPlane; });
    plane.reference.position.SetZ(17);
    stand.replaceFeature(plane);
    stand.save(standPath);
    doc.reloadSources();
    require(std::abs(doc.component(ownedPart).placement.TranslationPart().Z() - 17) < 1e-7,
            "In-context source update was not associative");
    auto detached = doc;
    detached.deleteMates({"mate-rotation-" + id, "mate-length-" + id, "mate-lock-" + id,
                          "mate-inplace-" + detached.inPlaceParts()[0].id});
    require(!detached.belts()[0].engaged && !detached.belts()[0].driving &&
                !detached.belts()[0].lockPart && !detached.inPlaceParts()[0].linked,
            "Deleting generated mates did not detach their workflows");
    require(std::none_of(detached.mates().begin(), detached.mates().end(),
                         [&](const auto& m)
                         {
                             return m.id == "mate-rotation-" + id || m.id == "mate-length-" + id ||
                                    m.id == "mate-lock-" + id || m.kind == MateKind::InPlace;
                         }),
            "Deleted generated mates were recreated");
    const auto detachedPath = folder + "/detached.theasmb";
    detached.save(detachedPath);
    auto detachedReload = document::AssemblyDocument::load(detachedPath);
    require(!detachedReload.belts()[0].engaged && !detachedReload.inPlaceParts()[0].linked,
            "Detached state lost on reopen");
    const auto detachedPose = detachedReload.component(ownedPart).placement;
    detachedReload.deleteComponents({mount}, true);
    close(detachedReload.component(ownedPart).placement, detachedPose);
    doc.updateOwnedPart(ownedPart, document::PartDocument::createBox({3, 4, 5}));
    doc.save(assemblyPath);
    const auto loaded = document::AssemblyDocument::load(assemblyPath);
    require(loaded.belts().size() == 1 && loaded.inPlaceParts().size() == 1,
            "Generated workflow records lost on reload");
    for (const auto& c : doc.components())
        close(loaded.component(c.id).placement, c.placement);
    require(loaded.component(ownedPart).embeddedPart == doc.component(ownedPart).embeddedPart,
            "Owned part data changed on reload");
    const auto before = doc.stateKey();
    rejects([&] { doc.deleteComponents({mount}); });
    require(doc.stateKey() == before, "Rejected dependency deletion changed assembly");
    const auto names = doc.componentDependencyNames({mount});
    require(std::any_of(names.begin(), names.end(),
                        [](const auto& n) { return n.find("in-context") != std::string::npos; }),
            "Dependency review omits in-context components");
    auto removed = doc;
    removed.deleteBelt(id);
    require(removed.belts().empty() && removed.components().size() == 4,
            "Belt deletion did not remove its owned part");
    doc.deleteComponents({mount}, true);
    require(doc.inPlaceParts().empty() && doc.components().size() == 3,
            "Confirmed support deletion did not clean up dependent owned parts");
}
void magnetic(const QString& folder)
{
    const auto path = folder + "/asset.thepart";
    auto part = document::PartDocument::createBox({20, 10, 5});
    modeling::DatumDefinition d;
    d.method = modeling::DatumMethod::PointXYZ;
    d.position = {20, 5, 0};
    const auto right = part.addReferenceGeometry(d);
    d.position = {0, 5, 0};
    const auto left = part.addReferenceGeometry(d);
    d.method = modeling::DatumMethod::AxisXYZ;
    d.direction = {1, 0, 0};
    const auto positive = part.addReferenceGeometry(d);
    d.direction = {-1, 0, 0};
    const auto negative = part.addReferenceGeometry(d);
    part.save(path);
    document::AssemblyDocument doc;
    const auto a = doc.insertComponent(path);
    gp_Trsf pose;
    pose.SetTranslationPart({35, 0, 0});
    const auto b = doc.insertComponent(path, pose);
    document::PublishedAsset first;
    first.componentId = a;
    first.groundPlane = base(false, 0);
    first.connectors.push_back({"right", "Right connector", datum(right), datum(positive), {}});
    doc.publishAsset(first);
    document::PublishedAsset second;
    second.componentId = b;
    second.groundPlane = base(false, 0);
    second.connectors.push_back({"left", "Left connector", datum(left), datum(negative), {}});
    doc.publishAsset(second);
    doc.setGroundPlane(document::AssemblyReference{{}, base(false, 0)});
    require(doc.snapMagnetic(b, 20), "Published assets did not snap");
    require(std::abs(doc.component(b).placement.TranslationPart().X() - 20) < 1e-7,
            "Magnetic connector frame did not align");
    application::AssemblySession session;
    session.commit(doc);
    pose.SetTranslationPart({35, 0, 0});
    auto moved = doc;
    moved.setPlacement(b, pose);
    require(std::any_of(moved.mates().begin(), moved.mates().end(),
                        [](const auto& m)
                        {
                            return m.kind == MateKind::Magnetic &&
                                   m.status == MateStatus::Suppressed && !m.diagnostic.empty();
                        }),
            "Unlocked magnetic connection was not retained as disconnected");
    session.commit(moved);
    session.traverse(false);
    close(session.document().component(b).placement, doc.component(b).placement);
    session.traverse(true);
    close(session.document().component(b).placement, moved.component(b).placement);
    require(moved.snapMagnetic(b, 20, 0, 0, true), "Locked magnetic snap failed");
    const auto assemblyPath = folder + "/magnetic.theasmb";
    moved.save(assemblyPath);
    const auto loaded = document::AssemblyDocument::load(assemblyPath);
    require(loaded.assets().size() == 2 && loaded.groundPlane(),
            "Published references or ground plane lost on reopen");
    require(
        std::any_of(
            loaded.mates().begin(), loaded.mates().end(), [](const auto& m)
            { return m.kind == MateKind::Magnetic && m.definition->parameters.magnetic.locked; }),
        "Magnetic lock state lost on reopen");
    auto broken = first;
    broken.connectors.clear();
    const auto key = moved.stateKey();
    rejects([&] { moved.publishAsset(broken); });
    require(moved.stateKey() == key, "Removing a used connector corrupted the document");
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir folder;
        require(folder.isValid(), "Temporary directory failed");
        belts(folder.path());
        magnetic(folder.path());
        std::cout << "PASS: Belt/Chain, engagement, driving length, generated ownership, "
                     "in-context updates, published assets and magnetic locks\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
