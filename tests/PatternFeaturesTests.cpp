#include "document/PartDocument.h"
#include "modeling/PatternFeatures.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <TopExp_Explorer.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace thecad::document;
using namespace thecad::modeling;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void checkSolid(const PartDocument& part, double expectedVolume, int expectedBodies)
{
    require(!part.shape().IsNull() && BRepCheck_Analyzer(part.shape()).IsValid(),
            "Expected a valid solid result.");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(part.shape(), properties);
    require(std::abs(properties.Mass() - expectedVolume) < 1e-5, "Unexpected result volume.");
    int bodies = 0;
    for (TopExp_Explorer it(part.shape(), TopAbs_SOLID); it.More(); it.Next())
        ++bodies;
    require(bodies == expectedBodies, "Unexpected body count.");
}

GeometryReference reference(ReferenceSource source, int index)
{
    GeometryReference ref;
    ref.source = source;
    ref.index = index;
    return ref;
}

std::string circle(PartDocument& part, Point2 center)
{
    Sketch sketch;
    sketch.entities.push_back({EntityKind::Circle, center, {}, 3, "circle"});
    return part.addSketch(sketch);
}

void featureCopies()
{
    auto part = PartDocument::createBox({80, 40, 10});
    const auto holeSketch = circle(part, {10, 12});
    const auto hole = part.addCut(holeSketch, {"circle"}, 10, true, Direction::Positive);
    const auto bossSketch = circle(part, {10, 28});
    const auto boss = part.addExtrude(bossSketch, {"circle"}, 15, Direction::Positive);
    const double area = std::acos(-1.0) * 9;
    const double baseVolume = 80 * 40 * 10;
    checkSolid(part, baseVolume - area * 5, 1);

    Feature pattern;
    pattern.kind = FeatureKind::LinearPattern;
    pattern.pattern.reference = reference(ReferenceSource::BaseAxis, 0);
    pattern.pattern.spacing = 20;
    pattern.pattern.instances = 3;
    pattern.pattern.seeds = {boss, hole};
    const auto patternId = part.addSolidFeature(pattern);
    checkSolid(part, baseVolume - 3 * area * 5, 1);
    require(part.features().back().pattern.seeds == std::vector<std::string>{hole, boss},
            "Seeds should replay in history order.");
    require(part.features().back().dependencies == std::vector<std::string>{boss, hole},
            "Pattern must retain dependencies on the preceding solid and earlier seeds.");

    pattern = part.features().back();
    pattern.pattern.instances = 4;
    part.replaceFeature(pattern);
    checkSolid(part, baseVolume - 4 * area * 5, 1);
    const auto originalShape = part.shape();
    pattern.pattern.instances = 1;
    bool rejected = false;
    try
    {
        part.replaceFeature(pattern);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    require(rejected && part.shape().IsSame(originalShape) &&
                part.features().back().pattern.instances == 4,
            "Invalid edits must leave the original result intact.");

    auto mirrored = part.beforeFeature(patternId);
    DatumDefinition plane;
    plane.method = DatumMethod::PlaneXYZ;
    plane.position = {40, 0, 0};
    plane.direction = {1, 0, 0};
    const auto planeId = mirrored.addReferenceGeometry(plane);
    Feature mirror;
    mirror.kind = FeatureKind::Mirror;
    mirror.pattern.reference = reference(ReferenceSource::Datum, 0);
    mirror.pattern.reference->featureId = planeId;
    mirror.pattern.seeds = {hole};
    mirrored.addSolidFeature(mirror);
    checkSolid(mirrored, baseVolume - area * 15, 1);
    require(mirrored.referenceDeletionDependents({planeId}).size() == 1,
            "Mirror should protect its supporting datum.");

    QTemporaryDir temp;
    require(temp.isValid(), "Could not create a fixture directory.");
    const auto path = temp.filePath("mirror.thepart");
    mirrored.save(path);
    const auto loaded = PartDocument::load(path);
    checkSolid(loaded, baseVolume - area * 15, 1);
    require(loaded.features().back().pattern.reference->featureId == planeId &&
                loaded.features().back().pattern.seeds == std::vector<std::string>{hole},
            "Mirror must preserve its references when reopened.");
}

void circularPattern()
{
    const gp_Ax1 axis(gp_Pnt(30, 30, 0), gp_Dir(0, 0, 1));
    const gp_Pnt seedPoint(40, 30, 0);
    const auto fullTurn = circularInstances(axis, 360, 4, true, false);
    require(fullTurn.size() == 3 &&
                seedPoint.Transformed(fullTurn.front()).IsEqual({30, 40, 0}, 1e-8) &&
                seedPoint.Transformed(fullTurn.back()).IsEqual({30, 20, 0}, 1e-8),
            "A full turn must space instances evenly without duplicating the seed.");
    const auto partial = circularInstances(axis, 90, 3, true, true);
    require(seedPoint.Transformed(partial.back()).IsEqual({30, 20, 0}, 1e-8),
            "A reversed partial span must include its endpoint.");
    const auto increments = circularInstances(axis, 90, 3, false, false);
    require(seedPoint.Transformed(increments.back()).IsEqual({20, 30, 0}, 1e-8),
            "Incremental spacing must apply the specified angle for every step.");

    auto part = PartDocument::createEmpty();
    Sketch disk;
    disk.entities.push_back({EntityKind::Circle, {30, 30}, {}, 25, "disk"});
    const auto diskSketch = part.addSketch(disk);
    part.addExtrude(diskSketch, {"disk"}, 10, Direction::Positive);
    const auto cutSketch = circle(part, {40, 30});
    const auto cut = part.addCut(cutSketch, {"circle"}, 10, true, Direction::Positive);

    const auto options = part.referenceOptions(true);
    const auto rim = std::find_if(options.begin(), options.end(), [](const auto& option) {
        const auto center = option.geometry.frame.Location();
        return option.reference.source == ReferenceSource::BodyCircleAxis &&
               std::abs(center.X() - 30) < 1e-8 && std::abs(center.Y() - 30) < 1e-8 &&
               !option.reference.topologyName.empty();
    });
    require(rim != options.end(), "The circular rim should provide a named axis.");
    Feature pattern;
    pattern.kind = FeatureKind::CircularPattern;
    pattern.pattern.reference = rim->reference;
    pattern.pattern.seeds = {cut};
    pattern.pattern.instances = 4;
    const auto patternId = part.addSolidFeature(pattern);
    const double pi = std::acos(-1.0);
    checkSolid(part, 10 * pi * (625 - 36), 1);
    require(part.featureDeletionDependents({cut}).back().id == patternId,
            "Circular Pattern must depend on the seed.");

    auto edit = part.features().back();
    edit.pattern.angle = 120;
    edit.pattern.equalSpacing = false;
    bool rejected = false;
    const auto original = part.shape();
    try
    {
        part.replaceFeature(edit);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    require(rejected && part.shape().IsSame(original), "Reject a duplicate seed transactionally.");
    edit.pattern.angle = 60;
    edit.pattern.instances = 3;
    edit.pattern.reverse = true;
    part.replaceFeature(edit);
    auto larger = part.features().front();
    larger.sketch.entities.front().radius = 27;
    part.replaceFeature(larger);
    checkSolid(part, 10 * pi * (729 - 27), 1);

    QTemporaryDir directory;
    require(directory.isValid(), "Could not create a fixture directory.");
    const auto path = directory.filePath("circular-pattern.thepart");
    part.save(path);
    const auto loaded = PartDocument::load(path);
    checkSolid(loaded, 10 * pi * (729 - 27), 1);
    const auto& saved = loaded.features().back().pattern;
    require(saved.angle == 60 && saved.instances == 3 && !saved.equalSpacing && saved.reverse &&
                saved.reference->source == ReferenceSource::BodyCircleAxis &&
                saved.reference->topologyName == rim->reference.topologyName,
            "Circular Pattern must preserve its parameters and named axis.");
}

void bodyCopiesAndTopology()
{
    auto part = PartDocument::createBox({20, 10, 5});
    const double boxVolume = 20 * 10 * 5;
    Feature pattern;
    pattern.kind = FeatureKind::LinearPattern;
    pattern.pattern.wholePart = true;
    pattern.pattern.reference = reference(ReferenceSource::BaseAxis, 0);
    pattern.pattern.spacing = 50;
    pattern.pattern.instances = 3;
    pattern.pattern.reverse = true;
    part.addSolidFeature(pattern);
    checkSolid(part, 3 * boxVolume, 3);

    const auto options = part.referenceOptions();
    const auto copiedFace = std::find_if(options.begin(), options.end(), [](const auto& option) {
        return option.reference.source == ReferenceSource::BodyFace &&
               option.geometry.kind == DatumKind::Plane &&
               std::abs(option.geometry.frame.Location().X() + 100) < 1e-6 &&
               std::abs(option.geometry.frame.Direction().X()) > 0.99;
    });
    require(copiedFace != options.end() && !copiedFace->reference.topologyName.empty(),
            "A copied face must have a persistent name.");
    DatumDefinition datum;
    datum.method = DatumMethod::PlaneOffset;
    datum.references = {copiedFace->reference};
    part.addReferenceGeometry(datum);

    auto edit = part.features()[1];
    edit.pattern.spacing = 60;
    part.replaceFeature(edit);
    require(std::abs(part.features().back().datum.frame.Location().X() + 120) < 1e-6,
            "A dependent plane must follow its original pattern instance.");

    QTemporaryDir temp;
    require(temp.isValid(), "Could not create a fixture directory.");
    const auto path = temp.filePath("pattern.thepart");
    part.save(path);
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Could not inspect the saved fixture.");
    require(QJsonDocument::fromJson(file.readAll()).object()["version"].toInt() == 18,
            "Pattern documents must use schema 18.");
    const auto loaded = PartDocument::load(path);
    checkSolid(loaded, 3 * boxVolume, 3);
    const auto& saved = loaded.features()[1].pattern;
    require(saved.spacing == 60 && saved.instances == 3 && saved.reverse && saved.wholePart &&
                saved.reference->source == ReferenceSource::BaseAxis,
            "Pattern parameters must survive reopening.");
    require(std::abs(loaded.features().back().datum.frame.Location().X() + 120) < 1e-6,
            "The named-face association must survive reopening.");

    auto mirrored = PartDocument::createBox({20, 10, 5});
    const auto faces = mirrored.referenceOptions();
    const auto side = std::find_if(faces.begin(), faces.end(), [](const auto& option) {
        return option.reference.source == ReferenceSource::BodyFace &&
               option.geometry.kind == DatumKind::Plane &&
               std::abs(option.geometry.frame.Direction().X()) > 0.99;
    });
    require(side != faces.end(), "Expected a planar box face.");
    Feature mirror;
    mirror.kind = FeatureKind::Mirror;
    mirror.pattern.wholePart = true;
    mirror.pattern.reference = side->reference;
    mirrored.addSolidFeature(mirror);
    checkSolid(mirrored, 2 * boxVolume, 1);
}
}

int main()
{
    try
    {
        featureCopies();
        circularPattern();
        bodyCopiesAndTopology();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
