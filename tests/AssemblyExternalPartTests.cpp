#include "application/AssemblySession.h"
#include "document/storage/PartFileStore.h"
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

gp_Trsf translated(double x, double y = 0, double z = 0)
{
    gp_Trsf transform;
    transform.SetTranslation(gp_Vec(x, y, z));
    return transform;
}

gp_Trsf rotatedTranslated(double x, double y, double z, double radians)
{
    gp_Trsf transform;
    transform.SetRotation(gp_Quaternion(gp_Vec(0, 0, 1), radians));
    transform.SetTranslationPart({x, y, z});
    return transform;
}

const document::ExternalSketchReferenceOption& externalOption(
    const std::vector<document::ExternalSketchReferenceOption>& options,
    const std::string& componentId, modeling::ReferenceSource source,
    const std::string& featureId, const std::string& entityId = {})
{
    const auto found = std::find_if(options.begin(), options.end(), [&](const auto& option) {
        return option.reference.componentId == componentId &&
               option.reference.sourceKind == modeling::ExternalReferenceKind(int(source)) &&
               option.reference.sourceFeatureId == featureId &&
               option.reference.sourceEntityId == entityId;
    });
    if (found == options.end())
        throw std::runtime_error("A requested stable external reference was not offered.");
    return *found;
}

void attachExternalReference(document::PartDocument& part, const std::string& sketchId,
                             const document::ExternalSketchReferenceOption& option,
                             bool associative, const std::string& coincidentEntity = {})
{
    auto feature = *std::find_if(part.features().begin(), part.features().end(), [&](const auto& item) {
        return item.id == sketchId && item.kind == document::FeatureKind::Sketch;
    });
    auto reference = option.reference;
    reference.associative = associative;
    const auto projectedId = modeling::useExternalSketchProjection(
        feature.sketch, option.projectedGeometry, std::move(reference));
    if (!coincidentEntity.empty())
    {
        modeling::SketchRelation relation;
        relation.id = "external-coincident";
        relation.kind = modeling::RelationKind::Coincident;
        relation.first = {coincidentEntity, modeling::PointRole::Center};
        relation.second = {projectedId, modeling::PointRole::First};
        feature.sketch.relations.push_back(relation);
    }
    modeling::validateSketchModifiers(feature.sketch);
    part.replaceFeature(feature);
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

void refreshRetainsInstancesMatesAndPlacements(const QString& folder)
{
    const auto sourcePath = folder + "/Shared.thepart";
    const auto assemblyPath = folder + "/SharedInstances.theasmb";
    document::PartDocument::createBox({20, 30, 40}).save(sourcePath);

    document::AssemblyDocument assembly;
    const auto first = assembly.insertComponent(sourcePath);
    gp_Trsf placement;
    placement.SetTranslation(gp_Vec(60, 5, -2));
    const auto second = assembly.insertComponent(sourcePath, placement, false);
    require(assembly.component(first).part == assembly.component(second).part,
            "Repeated components did not share their external part definition.");

    document::AssemblyMate mate;
    mate.kind = document::AssemblyMateKind::Coincident;
    mate.first = {first, assemblyPlane()};
    mate.second = {second, assemblyPlane()};
    const auto mateId = assembly.setMate(mate);
    assembly.save(assemblyPath);
    auto reopened = document::AssemblyDocument::load(assemblyPath);
    const auto savedPose = reopened.component(second).placement;
    const auto savedMate = reopened.mates().front();

    document::PartDocument::createBox({35, 25, 15}).save(sourcePath);
    reopened.reloadSources();
    require(reopened.component(first).part == reopened.component(second).part,
            "Source refresh split instances that share one part definition.");
    require(reopened.component(first).part->parameters().width == 35,
            "Refreshing the source did not load the new part definition.");
    require(reopened.mates().size() == 1 && reopened.mates().front().id == mateId &&
                reopened.mates().front().first.componentId == savedMate.first.componentId &&
                reopened.mates().front().second.componentId == savedMate.second.componentId,
            "Source refresh removed or remapped an existing mate.");
    equalPose(reopened.component(second).placement, savedPose);
}

void associativeCrossPartReferences(const QString& folder)
{
    using namespace modeling;
    const auto sourcePath = folder + "/ReferenceHousing.thepart";
    const auto associativePath = folder + "/AssociativeBracket.thepart";
    const auto copyPath = folder + "/CopyBracket.thepart";
    const auto datumPath = folder + "/DatumBracket.thepart";
    const auto assemblyPath = folder + "/CrossPartReferences.theasmb";

    auto housing = document::PartDocument::createBox({100, 60, 20});
    Sketch sourceSketch;
    SketchEntity sourcePoint;
    sourcePoint.kind = EntityKind::Point;
    sourcePoint.id = "mount-center";
    sourcePoint.first = {5, 7};
    sourceSketch.entities.push_back(sourcePoint);
    SketchEntity sourceLine;
    sourceLine.kind = EntityKind::Line;
    sourceLine.id = "mount-edge";
    sourceLine.first = {-3, 4};
    sourceLine.second = {9, 4};
    sourceSketch.entities.push_back(sourceLine);
    SketchEntity sourceCircle;
    sourceCircle.kind = EntityKind::Circle;
    sourceCircle.id = "mount-hole";
    sourceCircle.first = {11, 13};
    sourceCircle.radius = 2;
    sourceSketch.entities.push_back(sourceCircle);
    const auto sourceSketchId = housing.addSketch(sourceSketch);
    DatumDefinition datumDefinition;
    datumDefinition.method = DatumMethod::PointXYZ;
    datumDefinition.position = gp_Pnt(2, 3, 0);
    const auto datumId = housing.addReferenceGeometry(datumDefinition);
    housing.save(sourcePath);
    auto sourceRoundTrip = document::PartDocument::load(sourcePath);
    require(sourceRoundTrip.features().size() == 3,
            "Stable source sketch and datum geometry did not persist in the part file.");
    const auto sourceReferences = sourceRoundTrip.referenceOptions(false, false, false);
    require(std::any_of(sourceReferences.begin(), sourceReferences.end(), [&](const auto& option) {
                return option.reference.source == ReferenceSource::SketchPoint &&
                       option.reference.featureId == sourceSketchId &&
                       option.reference.entityId == sourceCircle.id &&
                       option.reference.role == PointRole::Center;
            }) &&
                std::any_of(sourceReferences.begin(), sourceReferences.end(), [&](const auto& option) {
                    return option.reference.source == ReferenceSource::SketchLine &&
                           option.reference.featureId == sourceSketchId &&
                           option.reference.entityId == sourceLine.id;
                }),
            "Stable sketch point, circle-center or line references did not survive save/reopen.");

    auto associative = document::PartDocument::createEmpty();
    Sketch associativeSketch;
    SketchEntity circle;
    circle.kind = EntityKind::Circle;
    circle.id = "bracket-hole";
    circle.first = {15, 5};
    circle.radius = 2;
    associativeSketch.entities.push_back(circle);
    const auto associativeSketchId = associative.addSketch(associativeSketch);
    associative.addExtrude(associativeSketchId, {circle.id}, 4, Direction::Positive);
    associative.save(associativePath);

    auto copied = document::PartDocument::createBox({10, 10, 5});
    Sketch copiedSketch;
    SketchEntity copiedAnchor;
    copiedAnchor.kind = EntityKind::Point;
    copiedAnchor.id = "copied-anchor";
    copiedAnchor.first = {-4, -4};
    copiedSketch.entities.push_back(copiedAnchor);
    const auto copiedSketchId = copied.addSketch(copiedSketch);
    copied.save(copyPath);

    auto datumDependent = document::PartDocument::createBox({10, 10, 5});
    Sketch datumSketch;
    SketchEntity datumAnchor;
    datumAnchor.kind = EntityKind::Point;
    datumAnchor.id = "datum-anchor";
    datumAnchor.first = {-2, -3};
    datumSketch.entities.push_back(datumAnchor);
    const auto datumSketchId = datumDependent.addSketch(datumSketch);
    datumDependent.save(datumPath);

    document::AssemblyDocument assembly;
    const auto associativeInstance = assembly.insertComponent(
        associativePath, rotatedTranslated(0, 2, 0, 1.5707963267948966), false);
    const auto housingNear = assembly.insertComponent(sourcePath, translated(10));
    const auto housingFar = assembly.insertComponent(sourcePath, translated(40), false);
    const auto copyInstance = assembly.insertComponent(copyPath, {}, false);
    const auto datumInstance = assembly.insertComponent(datumPath, translated(0, 4), false);
    assembly.save(assemblyPath); // Establishes an owning assembly identity for associative links.

    const auto associativeOptions = assembly.externalSketchReferenceOptions(
        associativeInstance, *assembly.component(associativeInstance).part, associativeSketchId);
    const auto& nearPoint = externalOption(associativeOptions, housingNear,
        ReferenceSource::SketchPoint, sourceSketchId, sourcePoint.id);
    const auto& farPoint = externalOption(associativeOptions, housingFar,
        ReferenceSource::SketchPoint, sourceSketchId, sourcePoint.id);
    require(nearPoint.reference.componentId != farPoint.reference.componentId &&
                std::abs(farPoint.projectedGeometry.first.x - nearPoint.projectedGeometry.first.x) < 1e-7 &&
                std::abs(farPoint.projectedGeometry.first.y - nearPoint.projectedGeometry.first.y + 30) < 1e-7,
            "Repeated instances of one part were not distinguished in transformed references.");
    const auto circleCenter = std::find_if(associativeOptions.begin(), associativeOptions.end(),
        [&](const auto& option) {
            return option.reference.componentId == housingNear &&
                   option.reference.sourceKind == ExternalReferenceKind::SketchPoint &&
                   option.reference.sourceFeatureId == sourceSketchId &&
                   option.reference.sourceEntityId == sourceCircle.id &&
                   option.reference.role == PointRole::Center;
        });
    require(circleCenter != associativeOptions.end(),
            "The persistent center of a sketch circle was not exposed as an external reference.");
    attachExternalReference(associative, associativeSketchId, nearPoint, true, circle.id);
    const auto linkedCircle = std::find_if(associative.features().begin(), associative.features().end(),
        [&](const auto& feature) { return feature.id == associativeSketchId; });
    const auto projectedCircle = std::find_if(linkedCircle->sketch.entities.begin(), linkedCircle->sketch.entities.end(),
        [&](const auto& entity) { return entity.id != circle.id; });
    require(std::abs(sketchPointValue(linkedCircle->sketch, {circle.id, PointRole::Center}).x - 5) < 1e-7 &&
                std::abs(sketchPointValue(linkedCircle->sketch, {circle.id, PointRole::Center}).y + 15) < 1e-7 &&
                std::abs(projectedCircle->first.x - 5) < 1e-7 &&
                std::abs(projectedCircle->first.y + 15) < 1e-7,
            "Associative sketch projection or its coincident constraint used the wrong local coordinates.");
    const auto beforeRejectedUpdate = document::PartFileStore::encode(associative);
    modeling::ExternalSketchProjectionUpdate invalidUpdate;
    invalidUpdate.entityId = linkedCircle->sketch.externalReferences.front().entityId;
    invalidUpdate.geometry.kind = EntityKind::Line;
    bool invalidUpdateRejected = false;
    try
    {
        associative.updateExternalSketchReferences(associativeSketchId, {invalidUpdate});
    }
    catch (const std::exception&)
    {
        invalidUpdateRejected = true;
    }
    require(invalidUpdateRejected && beforeRejectedUpdate == document::PartFileStore::encode(associative),
            "A failed external-reference regeneration modified the committed part.");
    associative.save(associativePath);

    const auto copyOptions = assembly.externalSketchReferenceOptions(
        copyInstance, copied, copiedSketchId);
    attachExternalReference(copied, copiedSketchId,
        externalOption(copyOptions, housingFar, ReferenceSource::SketchPoint,
                       sourceSketchId, sourcePoint.id), false);
    attachExternalReference(copied, copiedSketchId,
        externalOption(copyOptions, housingFar, ReferenceSource::SketchLine,
                       sourceSketchId, sourceLine.id), false);
    copied.save(copyPath);

    const auto datumOptions = assembly.externalSketchReferenceOptions(
        datumInstance, datumDependent, datumSketchId);
    attachExternalReference(datumDependent, datumSketchId,
        externalOption(datumOptions, housingNear, ReferenceSource::Datum, datumId), true);
    datumDependent.save(datumPath);

    assembly.reloadSources();
    assembly.save(assemblyPath);

    auto reopened = document::AssemblyDocument::load(assemblyPath);
    const auto reopenedAssociative = std::find_if(reopened.component(associativeInstance).part->features().begin(),
        reopened.component(associativeInstance).part->features().end(),
        [&](const auto& feature) { return feature.id == associativeSketchId; });
    require(reopenedAssociative != reopened.component(associativeInstance).part->features().end() &&
                reopenedAssociative->sketch.externalReferences.size() == 1 &&
                reopenedAssociative->sketch.externalReferences.front().componentId == housingNear &&
                reopenedAssociative->sketch.externalReferences.front().associative,
            "Associative source identity and mode did not persist after save/reopen.");

    auto changedHousing = document::PartDocument::load(sourcePath);
    auto changedSketch = *std::find_if(changedHousing.features().begin(), changedHousing.features().end(),
        [&](const auto& feature) { return feature.id == sourceSketchId; });
    changedSketch.sketch.entities.front().first = {8, 9};
    changedHousing.replaceFeature(changedSketch);
    auto changedDatum = *std::find_if(changedHousing.features().begin(), changedHousing.features().end(),
        [&](const auto& feature) { return feature.id == datumId; });
    changedDatum.reference.position = gp_Pnt(4, 5, 0);
    changedHousing.replaceFeature(changedDatum);
    changedHousing.save(sourcePath);
    reopened.reloadSources();


    const auto& refreshedAssociative = reopened.component(associativeInstance).part->features();
    const auto refreshedSketch = std::find_if(refreshedAssociative.begin(), refreshedAssociative.end(),
        [&](const auto& feature) { return feature.id == associativeSketchId; });
    const auto& newCircleCenter = sketchPointValue(refreshedSketch->sketch,
        {circle.id, PointRole::Center});
    require(std::abs(newCircleCenter.x - 7) < 1e-7 && std::abs(newCircleCenter.y + 18) < 1e-7,
            "A changed source sketch point did not drive dependent constrained geometry.");
    require(!reopened.component(associativeInstance).shape().IsNull(),
            "Associative sketch changes did not regenerate its downstream solid feature.");

    const auto& refreshedCopy = reopened.component(copyInstance).part->features();
    const auto copyFeature = std::find_if(refreshedCopy.begin(), refreshedCopy.end(),
        [&](const auto& feature) { return feature.id == copiedSketchId; });
    const auto copyReference = std::find_if(copyFeature->sketch.externalReferences.begin(),
        copyFeature->sketch.externalReferences.end(), [](const auto& reference) { return !reference.associative; });
    const auto copyGeometry = std::find_if(copyFeature->sketch.entities.begin(), copyFeature->sketch.entities.end(),
        [&](const auto& entity) { return copyReference != copyFeature->sketch.externalReferences.end() &&
                                        entity.id == copyReference->entityId; });
    require(copyReference != copyFeature->sketch.externalReferences.end() &&
                std::abs(copyGeometry->first.x - 45) < 1e-7 &&
                std::abs(copyGeometry->first.y - 7) < 1e-7,
            "A non-associative sketch copy changed when its source changed.");
    const auto copiedLineReference = std::find_if(copyFeature->sketch.externalReferences.begin(),
        copyFeature->sketch.externalReferences.end(), [&](const auto& reference) {
            return reference.sourceKind == ExternalReferenceKind::SketchLine &&
                   reference.sourceEntityId == sourceLine.id;
        });
    const auto copiedLine = std::find_if(copyFeature->sketch.entities.begin(), copyFeature->sketch.entities.end(),
        [&](const auto& entity) { return copiedLineReference != copyFeature->sketch.externalReferences.end() &&
                                        entity.id == copiedLineReference->entityId; });
    require(copiedLineReference != copyFeature->sketch.externalReferences.end() &&
                std::abs(copiedLine->first.x - 37) < 1e-7 &&
                std::abs(copiedLine->second.x - 49) < 1e-7 &&
                std::abs(copiedLine->first.y - 4) < 1e-7,
            "Stable source sketch line geometry was not transformed into target part space.");

    const auto& refreshedDatumPart = reopened.component(datumInstance).part->features();
    const auto refreshedDatumSketch = std::find_if(refreshedDatumPart.begin(), refreshedDatumPart.end(),
        [&](const auto& feature) { return feature.id == datumSketchId; });
    const auto datumReference = refreshedDatumSketch->sketch.externalReferences.front();
    const auto projectedDatum = std::find_if(refreshedDatumSketch->sketch.entities.begin(),
        refreshedDatumSketch->sketch.entities.end(), [&](const auto& entity) {
            return entity.id == datumReference.entityId;
        });
    require(std::abs(projectedDatum->first.x - 14) < 1e-7 &&
                std::abs(projectedDatum->first.y - 1) < 1e-7,
            "A dependent datum reference did not update through component and target transforms.");

    const auto priorDependentCenter = newCircleCenter;
    changedHousing = document::PartDocument::load(sourcePath);
    changedSketch = *std::find_if(changedHousing.features().begin(), changedHousing.features().end(),
        [&](const auto& feature) { return feature.id == sourceSketchId; });
    changedSketch.sketch.entities.clear(); // The old stable point ID is now genuinely missing.
    changedHousing.replaceFeature(changedSketch);
    changedHousing.save(sourcePath);
    reopened.reloadSources();
    const auto invalidDiagnostics = reopened.externalReferenceDiagnostics();
    require(std::any_of(invalidDiagnostics.begin(), invalidDiagnostics.end(), [&](const auto& diagnostic) {
                return diagnostic.dependentComponentId == associativeInstance &&
                       diagnostic.sourceComponentId == housingNear &&
                       diagnostic.state == document::ExternalReferenceState::InvalidReference;
            }), "A missing stable source entity did not produce an invalid-reference diagnostic.");
    const auto& retained = reopened.component(associativeInstance).part->features();
    const auto retainedSketch = std::find_if(retained.begin(), retained.end(),
        [&](const auto& feature) { return feature.id == associativeSketchId; });
    const auto& retainedCenter = sketchPointValue(retainedSketch->sketch,
        {circle.id, PointRole::Center});
    require(std::abs(retainedCenter.x - priorDependentCenter.x) < 1e-7 &&
                std::abs(retainedCenter.y - priorDependentCenter.y) < 1e-7,
            "A failed external-reference regeneration overwrote the last valid dependent model.");
}

void externalReferenceCycleRejected(const QString& folder)
{
    const auto firstPath = folder + "/CycleA.thepart";
    const auto secondPath = folder + "/CycleB.thepart";
    const auto assemblyPath = folder + "/Cycle.theasmb";
    auto firstPart = document::PartDocument::createBox({2, 2, 2});
    modeling::Sketch sketch;
    modeling::SketchEntity point;
    point.kind = modeling::EntityKind::Point;
    point.id = "stable-point";
    point.first = {1, 1};
    sketch.entities.push_back(point);
    const auto firstSketchId = firstPart.addSketch(sketch);
    auto secondPart = firstPart;
    firstPart.save(firstPath);
    secondPart.save(secondPath);

    document::AssemblyDocument assembly;
    const auto first = assembly.insertComponent(firstPath);
    const auto second = assembly.insertComponent(secondPath, gp_Trsf{}, false);
    assembly.save(assemblyPath);
    const auto makeReferencePart = [&](const document::PartDocument& source,
                                       const std::string& sourceId,
                                       const QString& sourceFile) {
        auto candidate = source;
        auto feature = *std::find_if(candidate.features().begin(), candidate.features().end(),
            [](const auto& item) { return item.kind == document::FeatureKind::Sketch; });
        modeling::ExternalSketchReference reference;
        reference.assemblyPath = assemblyPath.toStdString();
        reference.componentId = sourceId;
        reference.sourcePartPath = QFileInfo(sourceFile).absoluteFilePath().toStdString();
        reference.sourceKind = modeling::ExternalReferenceKind::SketchPoint;
        reference.sourceFeatureId = firstSketchId;
        reference.sourceEntityId = point.id;
        modeling::SketchEntity projection;
        projection.kind = modeling::EntityKind::Point;
        projection.id = "external-cycle";
        projection.construction = true;
        modeling::useExternalSketchProjection(feature.sketch, projection, reference);
        candidate.replaceFeature(feature);
        return candidate;
    };
    const auto firstCandidate = makeReferencePart(*assembly.component(first).part, second, secondPath);
    std::string diagnostic;
    require(assembly.validateExternalReferenceGraph(first, firstCandidate, &diagnostic),
            "A one-way cross-part reference was incorrectly rejected.");
    firstCandidate.save(firstPath);
    assembly.reloadSources();
    auto secondCandidate = makeReferencePart(*assembly.component(second).part, first, firstPath);
    require(!assembly.validateExternalReferenceGraph(second, secondCandidate, &diagnostic) &&
                diagnostic.find("circular") != std::string::npos,
            ("A two-part circular external-reference dependency was not rejected clearly: " + diagnostic).c_str());
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
        refreshRetainsInstancesMatesAndPlacements(folder.path());
        associativeCrossPartReferences(folder.path());
        externalReferenceCycleRejected(folder.path());
        std::cout << "Independent assembly part workflow tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
