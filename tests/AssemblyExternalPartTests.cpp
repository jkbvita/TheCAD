#include "application/AssemblySession.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
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

modeling::GeometryReference datum(const std::string& id)
{
    modeling::GeometryReference ref;
    ref.source = modeling::ReferenceSource::Datum;
    ref.featureId = id;
    return ref;
}

modeling::GeometryReference assemblyPlane()
{
    modeling::GeometryReference ref;
    ref.source = modeling::ReferenceSource::BasePlane;
    ref.index = 0;
    return ref;
}

double translationZ(const gp_Trsf& transform)
{
    return transform.TranslationPart().Z();
}

void equalPose(const gp_Trsf& a, const gp_Trsf& b)
{
    for (int row = 1; row <= 3; ++row)
        for (int column = 1; column <= 4; ++column)
            require(std::abs(a.Value(row, column) - b.Value(row, column)) < 1e-7,
                    "Assembly transform differs after history traversal.");
}

void externalPartWorkflow(const QString& folder)
{
    const auto supportPath = folder + "/Housing.thepart";
    const auto bracketPath = folder + "/Bracket.thepart";
    const auto assemblyPath = folder + "/Mount.theasmb";

    auto housing = document::PartDocument::createBox({30, 20, 5});
    modeling::DatumDefinition supportPlane;
    supportPlane.method = modeling::DatumMethod::PlaneXYZ;
    supportPlane.position = {0, 0, 10};
    const auto supportId = housing.addReferenceGeometry(supportPlane);
    housing.save(supportPath);

    auto bracket = document::PartDocument::createEmpty();
    bracket.save(bracketPath);

    document::AssemblyDocument assembly;
    const auto housingInstance = assembly.insertComponent(supportPath);
    const auto bracketInstance = assembly.insertComponent(bracketPath, gp_Trsf{}, false);
    assembly.linkInPlacePart(bracketInstance, {housingInstance, datum(supportId)});
    require(assembly.component(bracketInstance).embeddedPart.isEmpty(),
            "An in-place part copied its definition into the assembly.");
    require(assembly.component(bracketInstance).part->features().empty(),
            "The blank external part should start with its own empty history.");
    require(std::abs(translationZ(assembly.component(bracketInstance).placement) - 10) < 1e-7,
            "The new component origin did not align to its support plane.");

    assembly.save(assemblyPath);
    auto reopened = document::AssemblyDocument::load(assemblyPath);
    require(reopened.component(bracketInstance).path == QFileInfo(bracketPath).absoluteFilePath(),
            "The assembly did not retain the external part path.");
    require(reopened.inPlaceParts().size() == 1 && reopened.inPlaceParts().front().linked,
            "The associative in-place support did not survive save/reopen.");

    auto editedBracket = document::PartDocument::load(bracketPath);
    modeling::Sketch sketch;
    modeling::SketchEntity profile;
    profile.kind = modeling::EntityKind::Circle;
    profile.id = "mounting-rim";
    profile.radius = 4;
    sketch.entities.push_back(profile);
    const auto sketchId = editedBracket.addSketch(sketch);
    editedBracket.addExtrude(sketchId, {profile.id}, 6, modeling::Direction::Positive);
    editedBracket.save(bracketPath);
    require(document::PartDocument::load(bracketPath).features().size() == 2,
            "The created .thepart could not be opened and modified independently.");

    auto changedHousing = document::PartDocument::load(supportPath);
    auto changedPlane = *std::find_if(changedHousing.features().begin(), changedHousing.features().end(),
                                     [&](const auto& feature) { return feature.id == supportId; });
    changedPlane.reference.position.SetZ(17);
    changedHousing.replaceFeature(changedPlane);
    changedHousing.save(supportPath);

    application::AssemblySession session;
    session.replaceInitial(reopened);
    auto updated = session.document();
    updated.reloadSources();
    require(updated.component(bracketInstance).part->features().size() == 2 &&
                !updated.component(bracketInstance).shape().IsNull(),
            "Saving the part did not update its linked assembly geometry.");
    require(std::abs(translationZ(updated.component(bracketInstance).placement) - 17) < 1e-7,
            "The linked component did not follow its updated datum support.");
    require(session.commit(updated), "The linked assembly refresh was not undoable.");
    session.traverse(false);
    require(std::abs(translationZ(session.document().component(bracketInstance).placement) - 10) < 1e-7,
            "Undo did not restore the last committed assembly pose.");
    session.traverse(true);
    require(std::abs(translationZ(session.document().component(bracketInstance).placement) - 17) < 1e-7,
            "Redo did not restore the source-updated assembly pose.");
    equalPose(session.document().component(bracketInstance).placement,
              updated.component(bracketInstance).placement);

    const auto beforeInvalidRefresh = session.document().stateKey();
    changedHousing.deleteReferenceGeometry({supportId});
    changedHousing.save(supportPath);
    bool rejected = false;
    try
    {
        auto invalid = session.document();
        invalid.reloadSources();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "An unavailable external support reference was silently accepted.");
    require(session.document().stateKey() == beforeInvalidRefresh,
            "A failed source refresh corrupted the committed assembly.");
}

void dependencyCycleRejected(const QString& folder)
{
    const auto firstPath = folder + "/First.thepart";
    const auto secondPath = folder + "/Second.thepart";
    document::PartDocument::createEmpty().save(firstPath);
    document::PartDocument::createEmpty().save(secondPath);
    document::AssemblyDocument assembly;
    const auto first = assembly.insertComponent(firstPath);
    const auto second = assembly.insertComponent(secondPath, gp_Trsf{}, false);
    const document::AssemblyReference firstToSecond{second, assemblyPlane()};
    const document::AssemblyReference secondToFirst{first, assemblyPlane()};
    assembly.linkInPlacePart(first, firstToSecond);
    const auto state = assembly.stateKey();
    bool rejected = false;
    try
    {
        assembly.linkInPlacePart(second, secondToFirst);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "A circular assembly-reference dependency was accepted.");
    require(assembly.stateKey() == state,
            "Rejecting a circular dependency changed the assembly state.");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        QTemporaryDir folder;
        require(folder.isValid(), "Temporary test directory could not be created.");
        externalPartWorkflow(folder.path());
        dependencyCycleRejected(folder.path());
        std::cout << "Independent assembly part workflow tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
