#include "application/AssemblySession.h"
#include "document/storage/AssemblyMateData.h"
#include "document/storage/PartFileStore.h"
#include "modeling/assembly/BeltGeometry.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <gp_Quaternion.hxx>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace thecad;
using namespace modeling::assembly;
namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
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
    throw std::runtime_error("Invalid transaction was accepted");
}
document::AssemblyReference plane(const std::string& id, int index = 0)
{
    modeling::GeometryReference r;
    r.source = modeling::ReferenceSource::BasePlane;
    r.index = index;
    return {id, r};
}
document::AssemblyMate mate(MateKind kind, const std::string& a, const std::string& b)
{
    document::AssemblyMate m;
    m.kind = kind;
    m.first = plane(a);
    m.second = plane(b);
    m.definition = document::AssemblyMate::Definition{};
    m.definition->groups = {{"first", {m.first}}, {"second", {m.second}}};
    return m;
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir temp;
        require(temp.isValid(), "Temporary directory failed");
        const auto source = temp.filePath("block.thepart");
        document::PartDocument::createBox({20, 10, 5}).save(source);
        document::AssemblyDocument doc;
        const auto a = doc.insertComponent(source), b = doc.insertComponent(source);
        auto m = mate(MateKind::Parallel, a, b);
        m.id = "parallel";
        m.name = "Parallel1";
        doc.setMate(m);
        const auto path = temp.filePath("typed.theasmb");
        doc.save(path);
        auto loaded = document::AssemblyDocument::load(path);
        require(loaded.stateKey() == doc.stateKey(),
                "Typed mate round-trip changed document state");
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "Cannot read saved assembly");
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        require(root["version"].toInt() == 2, "Typed assembly did not use schema 2");
        auto bad = mate(MateKind::Parallel, a, b);
        bad.id = "bad";
        bad.name = "Bad";
        bad.definition->groups[1].role = "wrong";
        bad.status = MateStatus::Suppressed;
        const auto before = doc.stateKey();
        rejects([&] { doc.setMate(bad); });
        require(before == doc.stateKey(), "Rejected suppressed record mutated model");
        auto seat = mate(MateKind::Coincident, a, b);
        seat.id = "seat";
        seat.name = "Seat";
        doc.setMate(seat);
        auto distance = mate(MateKind::Distance, a, b);
        distance.id = "distance";
        distance.name = "Distance";
        distance.value = 8;
        const auto seated = doc.stateKey();
        rejects([&] { doc.setMate(distance); });
        require(doc.stateKey() == seated, "Failed mate changed assembly");
        doc.keepUnsolvedMate(distance);
        require(doc.mates().back().status == MateStatus::Unsolved &&
                    !doc.mates().back().diagnostic.empty(),
                "Unsolved mate was not retained");
        doc.save(path);
        loaded = document::AssemblyDocument::load(path);
        require(loaded.stateKey() == doc.stateKey(), "Unsolved mate state lost on reload");
        rejects([&] { doc.setMateStatus({"distance"}, MateStatus::Active); });
        doc.forceMate(distance, {"seat"});
        require(doc.mates()[1].status == MateStatus::Unsolved &&
                    doc.mates()[2].status == MateStatus::Active,
                "Explicit force did not retain affected mate status");
        doc.setMateStatus({"distance"}, MateStatus::Suppressed);
        doc.setMateStatus({"seat"}, MateStatus::Active);
        require(doc.solution().degreesOfFreedom == 3,
                "Suppression did not release distance constraints");
        application::AssemblySession session;
        session.commit(doc);
        auto edit = doc;
        edit.setMateStatus({"seat"}, MateStatus::Suppressed);
        session.commit(edit);
        session.traverse(false);
        require(session.document().stateKey() == doc.stateKey(), "Mate status undo failed");
        session.traverse(true);
        require(session.document().stateKey() == edit.stateKey(), "Mate status redo failed");
        auto grouped = mate(MateKind::Symmetric, a, b);
        grouped.id = "symmetric";
        grouped.name = "Symmetry";
        grouped.definition->groups = {{"plane", {plane(a)}},
                                      {"entities", {plane(a, 1), plane(b, 1)}}};
        grouped.status = MateStatus::Suppressed;
        doc.setMate(grouped);
        require(doc.componentDependents({b}).size() == 4, "Grouped dependencies were not found");
        rejects([&] { doc.deleteComponents({b}); });
        doc.deleteComponents({b}, true);
        require(doc.mates().empty(), "Grouped dependent mates remained after component deletion");
        for (const auto& option : loaded.referenceOptions(b))
        {
            const auto g = loaded.mateGeometry(option.reference);
            require(g.kind == option.mateGeometry.kind,
                    "Cached named geometry resolves incorrectly");
        }
        auto corrupted = root;
        auto mates = corrupted["mates"].toArray();
        auto record = mates[0].toObject();
        auto definition = record["definition"].toObject();
        auto motion = definition["motion"].toObject();
        motion["coordinates"] = QJsonArray{1, 2, 3};
        definition["motion"] = motion;
        record["definition"] = definition;
        mates[0] = record;
        corrupted["mates"] = mates;
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "Cannot write malformed fixture");
        file.write(QJsonDocument(corrupted).toJson());
        file.close();
        rejects([&] { document::AssemblyDocument::load(path); });
        document::AssemblyDocument drive;
        const auto first = drive.insertComponent(source, {}, false),
                   second = drive.insertComponent(source, {}, false);
        for (const auto& component : {first, second})
        {
            auto concentric = mate(MateKind::Concentric, {}, component);
            concentric.first.geometry.source = concentric.second.geometry.source =
                modeling::ReferenceSource::BaseAxis;
            concentric.first.geometry.index = concentric.second.geometry.index = 2;
            concentric.definition->groups = {{"first", {concentric.first}},
                                             {"second", {concentric.second}}};
            drive.setMate(concentric);
            drive.setMate(mate(MateKind::Coincident, {}, component));
        }
        auto gear = mate(MateKind::Gear, first, second);
        gear.first.geometry.source = gear.second.geometry.source =
            modeling::ReferenceSource::BaseAxis;
        gear.first.geometry.index = gear.second.geometry.index = 2;
        gear.definition->groups = {{"first", {gear.first}}, {"second", {gear.second}}};
        gear.definition->parameters.coupler.firstRatio = 2;
        drive.setMate(gear);
        for (int step = 1; step <= 80; ++step)
        {
            const double angle = step * 0.2;
            gp_Trsf pose;
            pose.SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), angle));
            drive.setPlacement(first, pose);
            const auto actual = gp_Dir(1, 0, 0).Transformed(drive.component(second).placement);
            require(actual.IsEqual(gp_Dir(std::cos(-2 * angle), std::sin(-2 * angle), 0), 1e-6),
                    "Gear did not follow the driven component");
        }
        require(std::abs(drive.mates().back().definition->motion.coordinates[0] - 16) < 1e-5,
                "Gear input phase lost complete revolutions");
        drive.save(path);
        const auto reopened = document::AssemblyDocument::load(path);
        require(std::abs(reopened.mates().back().definition->motion.coordinates[0] - 16) < 1e-5 &&
                    std::abs(reopened.mates().back().definition->motion.coordinates[1] + 32) < 1e-5,
                "Continuous gear phase lost on reopen");
        for (const auto& id : {first, second})
            for (int row = 1; row <= 3; ++row)
                for (int col = 1; col <= 4; ++col)
                    require(std::abs(reopened.component(id).placement.Value(row, col) -
                                     drive.component(id).placement.Value(row, col)) < 1e-10,
                            "Reopened gear placement changed");
        const auto belt = beltGeometry({{gp_Ax3({}, gp_Dir(0, 0, 1)), 5, 1},
                                        {gp_Ax3(gp_Pnt(30, 0, 0), gp_Dir(0, 0, 1)), 5, 1}},
                                       1);
        require(std::abs(belt.length - (60 + 10 * 3.141592653589793)) < 1e-6,
                "Belt routing length is incorrect");
        auto beltPart = document::PartDocument::createEmpty();
        const auto sketchId = beltPart.addSketch(belt.boundary);
        const auto& segment =
            *std::find_if(belt.centerline.entities.begin(), belt.centerline.entities.end(),
                          [](const auto& e) { return e.kind == modeling::EntityKind::Line; });
        const modeling::Point2 middle{(segment.first.x + segment.second.x) / 2,
                                      (segment.first.y + segment.second.y) / 2};
        const auto contours = modeling::closedContours(belt.boundary);
        const auto region =
            std::find_if(contours.begin(), contours.end(), [&](const auto& contour)
                         { return modeling::contourContains(contour, belt.boundary, middle); });
        require(region != contours.end(), "Generated belt annulus has no valid extrusion region");
        beltPart.addExtrude(sketchId, region->entityIds, 5, modeling::Direction::Positive);
        GProp_GProps properties;
        BRepGProp::VolumeProperties(beltPart.shape(), properties);
        require(std::abs(properties.Mass() - belt.length * 5) < 1e-5,
                "Generated belt solid has incorrect volume");
        const auto data = document::PartFileStore::encode(beltPart);
        const auto decoded = document::PartFileStore::decode(data);
        require(document::PartFileStore::encode(decoded) == data,
                "Embedded generated part data changed on decode");
        std::cout << "PASS: schema 2, typed geometry, conflict transactions, suppression, force, "
                     "grouped dependencies and history\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
