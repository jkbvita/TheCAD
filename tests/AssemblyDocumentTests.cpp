#include "application/AssemblySession.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "Invalid assembly operation accepted");
}
document::AssemblyReference base(const std::string& id, bool axis, int index)
{
    modeling::GeometryReference r;
    r.source = axis ? modeling::ReferenceSource::BaseAxis : modeling::ReferenceSource::BasePlane;
    r.index = index;
    return {id, r};
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir directory;
        const auto partPath = directory.filePath("box.thepart"),
                   assemblyPath = directory.filePath("mechanism.theasmb");
        auto part = document::PartDocument::createBox({40, 30, 20});
        part.save(partPath);
        document::AssemblyDocument assembly;
        const auto first = assembly.insertComponent(partPath);
        gp_Trsf placement;
        placement.SetTranslationPart({55, 3, 8});
        const auto second = assembly.insertComponent(partPath, placement);
        require(assembly.component(first).fixed && !assembly.component(second).fixed,
                "First component should be fixed by default");
        require(assembly.component(first).part == assembly.component(second).part,
                "Part instances do not share immutable source data");
        require(assembly.solution().degreesOfFreedom == 6,
                "Floating component must start with six degrees of freedom");
        document::AssemblyMate mate;
        mate.kind = document::AssemblyMateKind::Concentric;
        mate.first = base(first, true, 2);
        mate.second = base(second, true, 2);
        assembly.setMate(mate);
        mate = {};
        mate.first = base(first, false, 0);
        mate.second = base(second, false, 0);
        assembly.setMate(mate);
        mate = {};
        mate.kind = document::AssemblyMateKind::Angle;
        mate.first = base(first, true, 0);
        mate.second = base(second, true, 0);
        mate.value = 47;
        assembly.setMate(mate);
        require(assembly.solution().degreesOfFreedom == 0,
                "Mates failed to fully define component");
        const auto original = assembly.stateKey();
        mate = {};
        mate.kind = document::AssemblyMateKind::Distance;
        mate.first = base(first, false, 0);
        mate.second = base(second, false, 0);
        mate.value = 15;
        rejects([&] { assembly.setMate(mate); });
        require(assembly.stateKey() == original, "Failed mate corrupted the assembly");
        rejects([&] { assembly.deleteComponents({first}); });
        require(assembly.stateKey() == original, "Unconfirmed dependent deletion changed assembly");
        require(assembly.componentDependents({first}).size() == 3, "Dependent mate list is wrong");
        rejects([&] { assembly.setPlacement(first, placement); });
        assembly.setVisible(second, false);
        assembly.save(assemblyPath);
        auto loaded = document::AssemblyDocument::load(assemblyPath);
        require(loaded.components().size() == 2 && loaded.mates().size() == 3 &&
                    !loaded.component(second).visible && loaded.solution().degreesOfFreedom == 0,
                "Assembly roundtrip failed");
        require(loaded.components()[0].part == loaded.components()[1].part,
                "Reopened instances duplicate source parts");
        const auto moved = directory.filePath("moved");
        QDir().mkpath(moved);
        require(QFile::copy(partPath, moved + "/box.thepart") &&
                    QFile::copy(assemblyPath, moved + "/mechanism.theasmb"),
                "Cannot make relative-link fixture");
        auto relocated = document::AssemblyDocument::load(moved + "/mechanism.theasmb");
        require(relocated.mates().size() == 3,
                "Relative source paths failed after moving assembly");
        auto named = document::AssemblyDocument{};
        const auto a = named.insertComponent(partPath), b = named.insertComponent(partPath);
        for (const auto& option : named.referenceOptions(a))
            if (option.reference.geometry.source == modeling::ReferenceSource::BodyFace &&
                option.geometry.kind == modeling::DatumKind::Plane &&
                std::abs(option.geometry.frame.Location().X() - 40) < 1e-6)
            {
                mate = {};
                mate.first = option.reference;
                mate.second = base(b, false, 2);
                named.setMate(mate);
                break;
            }
        require(named.mates().size() == 1, "Named planar face mate was not created");
        auto box = part.features().front();
        box.box.width = 50;
        part.replaceFeature(box);
        part.save(partPath);
        named.reloadSources();
        require(
            std::abs(named.referenceGeometry(named.mates()[0].first, false).frame.Location().X() -
                     50) < 1e-6,
            "Named source face did not update associatively");
        const auto beforeMissing = named.stateKey();
        auto replacement = document::PartDocument::createEmpty();
        modeling::Sketch circle;
        circle.entities.push_back({modeling::EntityKind::Circle, {0, 0}, {}, 10, "circle"});
        const auto sketchId = replacement.addSketch(circle);
        replacement.addExtrude(sketchId, {"circle"}, 20, modeling::Direction::Positive);
        replacement.save(partPath);
        rejects([&] { named.reloadSources(); });
        require(named.stateKey() == beforeMissing,
                "Missing named face silently reattached or corrupted assembly");
        document::AssemblyDocument cylinders;
        const auto shaft = cylinders.insertComponent(partPath),
                   bore = cylinders.insertComponent(partPath, placement);
        mate = {};
        mate.kind = document::AssemblyMateKind::Concentric;
        for (const auto& option : cylinders.referenceOptions(shaft))
            if (option.reference.geometry.source == modeling::ReferenceSource::BodyFace &&
                option.geometry.kind == modeling::DatumKind::Axis)
                mate.first = option.reference;
        for (const auto& option : cylinders.referenceOptions(bore))
            if (option.reference.geometry.source == modeling::ReferenceSource::BodyFace &&
                option.geometry.kind == modeling::DatumKind::Axis)
                mate.second = option.reference;
        require(!mate.first.componentId.empty() && !mate.second.componentId.empty(),
                "Cylindrical faces not exposed as mate axes");
        cylinders.setMate(mate);
        require(cylinders.solution().degreesOfFreedom == 2,
                "Cylindrical face mate did not preserve axial freedom");
        const auto cylinderPath = directory.filePath("cylinders.theasmb");
        cylinders.save(cylinderPath);
        const auto reopenedCylinders = document::AssemblyDocument::load(cylinderPath);
        require(reopenedCylinders.solution().degreesOfFreedom == 2,
                "Cylindrical mate persistence failed");
        QFile cylinderFile(cylinderPath);
        require(cylinderFile.open(QIODevice::ReadOnly), "Cannot read assembly fixture");
        auto malformed = QJsonDocument::fromJson(cylinderFile.readAll()).object();
        cylinderFile.close();
        auto entries = malformed["components"].toArray();
        auto entry = entries[0].toObject();
        auto matrix = entry["placement"].toArray();
        matrix[0] = 2;
        entry["placement"] = matrix;
        entries[0] = entry;
        malformed["components"] = entries;
        const auto malformedPath = directory.filePath("scaled.theasmb");
        QFile malformedFile(malformedPath);
        require(malformedFile.open(QIODevice::WriteOnly), "Cannot write malformed fixture");
        malformedFile.write(QJsonDocument(malformed).toJson());
        malformedFile.close();
        rejects([&] { document::AssemblyDocument::load(malformedPath); });
        loaded.deleteComponents({second}, true);
        require(loaded.components().size() == 1 && loaded.mates().empty(),
                "Confirmed component/mate deletion failed");
        application::AssemblySession session;
        session.commit(assembly);
        require(session.canUndo(), "Assembly insertion missing undo");
        session.traverse(false);
        require(session.document().components().empty() && session.canRedo(),
                "Assembly undo failed");
        session.traverse(true);
        require(session.document().mates().size() == 3, "Assembly redo failed");
        session.reset();
        auto free = document::AssemblyDocument{};
        const auto freeId = free.insertComponent(partPath, {}, false);
        session.commit(free);
        for (int i = 1; i <= 15; ++i)
        {
            auto candidate = session.document();
            gp_Trsf pose;
            pose.SetTranslationPart({double(i), 0, 0});
            candidate.setPlacement(freeId, pose);
            session.commit(std::move(candidate));
        }
        int undone = 0;
        while (session.canUndo())
        {
            session.traverse(false);
            ++undone;
        }
        require(undone == 10, "Assembly history must retain ten actions");
        const auto invalid = directory.filePath("invalid.theasmb");
        QFile output(invalid);
        require(output.open(QIODevice::WriteOnly), "Cannot make invalid fixture");
        output.write("{\"format\":\"TheCAD.Assembly\",\"version\":99,\"units\":\"mm\","
                     "\"components\":[],\"mates\":[]}");
        output.close();
        const auto preserved = session.document().stateKey();
        rejects([&] { session.open(invalid); });
        require(session.document().stateKey() == preserved,
                "Failed open replaced the active assembly");
        std::cout << "PASS: linked component instances, mates, transactional failures, relative "
                     "files, source regeneration, deletion and ten-action history\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
