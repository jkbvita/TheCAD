#include "document/PartDocument.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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

void checkVolume(const PartDocument& part, double expected)
{
    require(!part.shape().IsNull() && BRepCheck_Analyzer(part.shape()).IsValid(),
            "Expected a valid sweep solid.");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(part.shape(), properties);
    if (std::abs(properties.Mass() - expected) > 1e-5)
    {
        std::cerr << "Expected volume " << expected << ", got " << properties.Mass() << '\n';
        throw std::runtime_error("Unexpected sweep volume.");
    }
}

Sketch disk(double radius)
{
    Sketch profile;
    profile.entities.push_back({EntityKind::Circle, {}, {}, radius, "outer"});
    return profile;
}

Feature sweep(FeatureKind kind, const std::string& profile, const std::string& path)
{
    Feature result;
    result.kind = kind;
    result.sketchId = profile;
    result.pathSketchId = path;
    result.contour = {"outer"};
    return result;
}

void bentBossAndAssociation()
{
    auto part = PartDocument::createEmpty();
    const auto profileId = part.addSketch(disk(2));
    Sketch path;
    path.plane = SketchPlane::XZ;
    // The sketch's entity order and directions do not determine the sweep start.
    path.entities.push_back({EntityKind::Line, {15, 20}, {0, 20}, 1, "horizontal"});
    path.entities.push_back({EntityKind::Line, {0, 20}, {0, 0}, 1, "vertical"});
    path.entities.push_back({EntityKind::Line, {3, 0}, {3, 6}, 1, "helper", true});
    const auto pathId = part.addSketch(path);
    part.addSolidFeature(sweep(FeatureKind::Sweep, profileId, pathId));
    const double area = std::acos(-1.0) * 4;
    checkVolume(part, area * 35);
    require(part.features().back().dependencies == std::vector<std::string>{profileId, pathId},
            "Sweep must depend on both sketches.");

    const auto options = part.referenceOptions();
    const auto cap = std::find_if(options.begin(), options.end(), [](const auto& option) {
        return option.reference.source == ReferenceSource::BodyFace &&
               option.geometry.kind == DatumKind::Plane &&
               std::abs(option.geometry.frame.Location().X() - 15) < 1e-6 &&
               std::abs(option.geometry.frame.Direction().X()) > 0.99;
    });
    require(cap != options.end() && !cap->reference.topologyName.empty(),
            "The swept end cap must have a persistent name.");
    DatumDefinition attachedPlane;
    attachedPlane.method = DatumMethod::PlaneOffset;
    attachedPlane.references = {cap->reference};
    part.addReferenceGeometry(attachedPlane);
    auto edit = part.features()[1];
    edit.sketch.entities.front().first.x = 25;
    part.replaceFeature(edit);
    checkVolume(part, area * 45);
    require(std::abs(part.features().back().datum.frame.Location().X() - 25) < 1e-6,
            "A dependent plane must follow the edited sweep end.");

    const auto before = part.shape();
    edit.sketch.entities.push_back({EntityKind::Line, {0, 20}, {-10, 20}, 1, "branch"});
    bool rejected = false;
    try
    {
        part.replaceFeature(edit);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    require(rejected && part.shape().IsSame(before), "A branched path edit must roll back.");

    QTemporaryDir temp;
    const auto filename = temp.filePath("bend.thepart");
    part.save(filename);
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(file.readAll()).object()["version"].toInt() == 19,
            "A sweep document should use schema 19.");
    const auto reopened = PartDocument::load(filename);
    checkVolume(reopened, area * 45);
    require(reopened.features()[2].pathSketchId == pathId &&
                std::abs(reopened.features().back().datum.frame.Location().X() - 25) < 1e-6,
            "Sweep and named-face association must survive reopening.");
}

void hollowBossAndCut()
{
    auto hollow = PartDocument::createEmpty();
    auto ring = disk(5);
    ring.entities.push_back({EntityKind::Circle, {}, {}, 3, "inner"});
    const auto ringId = hollow.addSketch(ring);
    Sketch path;
    path.plane = SketchPlane::XZ;
    path.entities.push_back({EntityKind::Line, {0, 0}, {0, 15}, 1, "path"});
    const auto pathId = hollow.addSketch(path);
    auto tube = sweep(FeatureKind::Sweep, ringId, pathId);
    tube.contour.push_back("inner");
    hollow.addSolidFeature(tube);
    const double pi = std::acos(-1.0);
    checkVolume(hollow, pi * (25 - 9) * 15);

    auto part = PartDocument::createEmpty();
    Sketch rectangle;
    rectangle.entities.push_back({EntityKind::Rectangle, {-20, -20}, {20, 20}, 1, "rectangle"});
    const auto baseSketch = part.addSketch(rectangle);
    const auto bodyId = part.addExtrude(baseSketch, {"rectangle"}, 10, Direction::Positive);
    const auto cutProfile = part.addSketch(disk(3));
    const auto cutPath = part.addSketch(path);
    part.addSolidFeature(sweep(FeatureKind::SweptCut, cutProfile, cutPath));
    checkVolume(part, (1600 - pi * 9) * 10);
    require(part.features().back().dependencies ==
                std::vector<std::string>{cutProfile, cutPath, bodyId},
            "Swept Cut must retain both sketches and the preceding solid.");
    auto movedProfile = part.features()[2];
    movedProfile.sketch.entities.front().first.x = 100;
    const auto cutResult = part.shape();
    bool missed = false;
    try
    {
        part.replaceFeature(movedProfile);
    }
    catch (const std::runtime_error&)
    {
        missed = true;
    }
    require(missed && part.shape().IsSame(cutResult), "A cut that misses the part must roll back.");

    auto empty = PartDocument::createEmpty();
    const auto emptyProfile = empty.addSketch(disk(3));
    const auto emptyPath = empty.addSketch(path);
    bool rejected = false;
    try
    {
        empty.addSolidFeature(sweep(FeatureKind::SweptCut, emptyProfile, emptyPath));
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    require(rejected && empty.features().size() == 2 && empty.shape().IsNull(),
            "A cut without a solid must fail transactionally.");
}

void obliqueStart()
{
    auto part = PartDocument::createEmpty();
    auto profile = disk(2);
    profile.entities.front().first = {2, 3};
    const auto profileId = part.addSketch(profile);
    Sketch path;
    path.plane = SketchPlane::XZ;
    path.entities.push_back({EntityKind::Line, {0, 0}, {10, -20}, 1, "path"});
    const auto pathId = part.addSketch(path);
    part.addSolidFeature(sweep(FeatureKind::Sweep, profileId, pathId));
    checkVolume(part, std::acos(-1.0) * 4 * 20);
}
} // namespace

int main()
{
    try
    {
        bentBossAndAssociation();
        hollowBossAndCut();
        obliqueStart();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
