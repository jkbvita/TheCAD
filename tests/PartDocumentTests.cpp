#include "document/PartDocument.h"
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace thecad::document;
using namespace thecad::modeling;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Test file must open");
    return file.readAll();
}
void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Test file must open for writing");
    require(file.write(bytes) == bytes.size(), "Test bytes must write");
}
void rejected(const std::function<void()>& action)
{
    bool failed = false;
    try { action(); } catch (const std::runtime_error& error) { failed = !QString::fromUtf8(error.what()).isEmpty(); }
    require(failed, "Invalid operation must fail with a clear error");
}
double volume(const PartDocument& document)
{
    const auto& shape = document.shape();
    require(!shape.IsNull() && BRepCheck_Analyzer(shape).IsValid(), "Regenerated solid must be valid BRep");
    int solids = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) ++solids;
    require(solids == 1, "Part must have one solid");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}
void closeVolume(const PartDocument& document, double expected)
{
    require(std::abs(volume(document) - expected) < std::max(1.0, expected) * 1e-8, "Regenerated volume must match parameters");
}
void checkBox(const PartDocument& document, BoxParameters dimensions)
{
    require(document.parameters().width == dimensions.width && document.parameters().depth == dimensions.depth
            && document.parameters().height == dimensions.height, "Box parameters must survive exactly");
    closeVolume(document, dimensions.width * dimensions.depth * dimensions.height);
    Bnd_Box bounds;
    BRepBndLib::Add(document.shape(), bounds);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    require(std::abs(xmin) < 1e-6 && std::abs(ymin) < 1e-6 && std::abs(zmin) < 1e-6
            && std::abs(xmax - dimensions.width) < 1e-6 && std::abs(ymax - dimensions.depth) < 1e-6
            && std::abs(zmax - dimensions.height) < 1e-6, "Box bounds must survive exactly");
}
Sketch rectangle()
{
    Sketch sketch;
    sketch.plane = SketchPlane::XY;
    SketchEntity entity;
    entity.kind = EntityKind::Rectangle;
    entity.first = {0, 0};
    entity.second = {40, 30};
    sketch.entities.push_back(entity);
    return sketch;
}

Sketch constrainedRectangle()
{
    Sketch sketch{SketchPlane::XY, {
        {EntityKind::Line, {0,0}, {40,0}, 1, "a"},
        {EntityKind::Line, {40,0}, {40,30}, 1, "b"},
        {EntityKind::Line, {40,30}, {0,30}, 1, "c"},
        {EntityKind::Line, {0,30}, {0,0}, 1, "d"}}};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& entity = sketch.entities[i];
        sketch.relations.push_back({"join-" + std::to_string(i), RelationKind::Coincident,
            {entity.id, PointRole::Second}, {sketch.entities[(i + 1) % 4].id, PointRole::First}});
        sketch.relations.push_back({"axis-" + std::to_string(i), i % 2 ? RelationKind::Vertical : RelationKind::Horizontal,
            {entity.id, PointRole::First}, {entity.id, PointRole::Second}});
    }
    sketch.relations.push_back({"origin", RelationKind::Coincident, {"a", PointRole::First}, {}});
    return sketch;
}

Sketch circle()
{
    Sketch sketch;
    sketch.plane = SketchPlane::XY;
    SketchEntity entity;
    entity.kind = EntityKind::Circle;
    entity.first = {20, 15};
    entity.radius = 4;
    sketch.entities.push_back(entity);
    return sketch;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "Temporary directory must exist");
        const QString path = directory.filePath(QString::fromUtf8("příliš-žluťoučký.thepart"));
        auto empty = PartDocument::createEmpty();
        require(empty.features().empty() && empty.shape().IsNull(), "New part must be empty");
        empty.save(path);
        require(PartDocument::load(path).shape().IsNull(), "Empty part must round-trip");
        auto sketchOnly = PartDocument::createEmpty();
        Sketch openSketch;
        SketchEntity line;
        line.kind = EntityKind::Line;
        line.first = {0, 0}; line.second = {10, 0};
        openSketch.entities.push_back(line);
        const auto lineId = sketchOnly.addSketch(openSketch);
        sketchOnly.save(path);
        auto loadedSketch = PartDocument::load(path);
        require(loadedSketch.shape().IsNull() && loadedSketch.features()[0].id == lineId,
                "Open sketches must be saveable without pretending to be solids");
        rejected([&] { sketchOnly.addExtrude(lineId, {sketchOnly.features()[0].sketch.entities[0].id}, 10, Direction::Positive); });
        require(sketchOnly.features().size() == 1, "Failed append must preserve history");

        auto relatedPart = PartDocument::createEmpty();
        const auto relatedSketchId = relatedPart.addSketch(constrainedRectangle());
        relatedPart.addExtrude(relatedSketchId, {"a", "b", "c", "d"}, 12, Direction::Positive);
        relatedPart.save(path);
        auto relatedLoaded = PartDocument::load(path);
        require(relatedLoaded.features()[0].sketch.relations.size() == 9, "All supported relations must survive serialization");
        require(relatedLoaded.features()[0].sketch.relations.back().second.role == PointRole::Origin,
            "Origin references must survive with their actual geometric identity");
        auto relatedFeature = relatedLoaded.features()[0];
        auto relatedEdit = relatedFeature.sketch.entities[0]; relatedEdit.second.x = 50;
        relatedFeature.sketch = editSketchEntity(relatedFeature.sketch, relatedEdit);
        relatedLoaded.replaceFeature(relatedFeature);
        closeVolume(relatedLoaded, 50 * 30 * 12);
        relatedLoaded.save(path);
        closeVolume(PartDocument::load(path), 50 * 30 * 12);
        const auto relatedRoot = QJsonDocument::fromJson(read(path)).object();
        const auto rejectRelated = [&](QJsonObject root) { write(path, QJsonDocument(root).toJson()); rejected([&] { PartDocument::load(path); }); };
        const auto corruptRelation = [&](const QString& field, const QJsonValue& value) {
            auto root = relatedRoot; auto history = root["features"].toArray(); auto feature = history[0].toObject();
            auto relations = feature["relations"].toArray(); auto relation = relations[0].toObject(); relation[field] = value;
            relations[0] = relation; feature["relations"] = relations; history[0] = feature; root["features"] = history; rejectRelated(root);
        };
        corruptRelation("type", "unsupported");
        corruptRelation("id", "origin");
        corruptRelation("second", QJsonObject{{"entity", "missing"}, {"point", "first"}});
        corruptRelation("second", QJsonObject{{"entity", "a"}, {"point", "center"}});
        corruptRelation("second", QJsonObject{{"entity", "a"}, {"point", "origin"}});
        auto missingRelations = relatedRoot; auto relatedHistory = missingRelations["features"].toArray();
        auto noRelationsFeature = relatedHistory[0].toObject(); noRelationsFeature.remove("relations");
        relatedHistory[0] = noRelationsFeature; missingRelations["features"] = relatedHistory; rejectRelated(missingRelations);
        auto unsatisfied = relatedLoaded.features()[0]; unsatisfied.sketch.entities[1].first.x += 1;
        rejected([&] { relatedLoaded.replaceFeature(unsatisfied); });
        closeVolume(relatedLoaded, 50 * 30 * 12);
        // Schema 2 lacked relations. It remains a supported input, upgraded on save.
        auto legacyV2 = relatedRoot; legacyV2["version"] = 2; relatedHistory = legacyV2["features"].toArray();
        noRelationsFeature = relatedHistory[0].toObject(); noRelationsFeature.remove("relations");noRelationsFeature.remove("dimensions");noRelationsFeature.remove("space");
        relatedHistory[0] = noRelationsFeature; legacyV2["features"] = relatedHistory;
        write(path, QJsonDocument(legacyV2).toJson());
        auto upgraded = PartDocument::load(path); closeVolume(upgraded, 50 * 30 * 12);
        require(upgraded.features()[0].sketch.relations.empty(), "Legacy coordinates must load without inventing constraints");
        upgraded.save(path);
        require(QJsonDocument::fromJson(read(path)).object()["version"].toInt() == 7, "Legacy documents must save using the current schema");
        auto part = PartDocument::createEmpty();
        auto multiContourSketch = rectangle();
        auto otherRectangle = multiContourSketch.entities.front();
        otherRectangle.first = {100, 100};
        otherRectangle.second = {110, 110};
        multiContourSketch.entities.push_back(otherRectangle);
        const auto sketchId = part.addSketch(multiContourSketch);
        const std::vector<std::string> rectangleContour{part.features()[0].sketch.entities[0].id};
        const auto extrudeId = part.addExtrude(sketchId, rectangleContour, 20, Direction::Positive);
        closeVolume(part, 40 * 30 * 20);
        const auto circleId = part.addSketch(circle());
        const std::vector<std::string> circleContour{part.features()[2].sketch.entities[0].id};
        const auto cutId = part.addCut(circleId, circleContour, 5, true, Direction::Positive);
        const double pi = std::acos(-1.0);
        closeVolume(part, (40 * 30 - pi * 16) * 20);
        require(part.features()[1].dependencies == std::vector<std::string>{sketchId}, "Extrude must depend on its sketch");
        require(part.features()[3].dependencies == std::vector<std::string>({circleId, extrudeId}), "Cut must depend on sketch and prior body");
        auto extrude = part.features()[1];
        extrude.length = 30;
        part.replaceFeature(extrude);
        closeVolume(part, (40 * 30 - pi * 16) * 30);
        auto cut = part.features()[3];
        cut.throughAll = false;
        cut.length = 5;
        part.replaceFeature(cut);
        closeVolume(part, 40 * 30 * 30 - pi * 16 * 5);
        cut.throughAll = true;
        part.replaceFeature(cut);

        part.save(path);
        auto loaded = PartDocument::load(path);
        require(loaded.features().size() == 4 && loaded.features()[0].id == sketchId && loaded.features()[1].id == extrudeId
                && loaded.features()[2].id == circleId && loaded.features()[3].id == cutId,
                "Feature IDs and order must survive round-trip");
        require(loaded.features()[1].contour == rectangleContour && loaded.features()[3].contour == circleContour,
                "Explicit contour references must survive round-trip");
        require(loaded.features()[1].length == 30 && loaded.features()[3].throughAll
                && loaded.features()[0].sketch.entities[0].second.x == 40
                && loaded.features()[2].sketch.entities[0].radius == 4, "Editable feature and sketch parameters must survive");
        closeVolume(loaded, (40 * 30 - pi * 16) * 30);
        extrude = loaded.features()[1]; extrude.length = 45; loaded.replaceFeature(extrude);
        closeVolume(loaded, (40 * 30 - pi * 16) * 45);

        const auto priorShape = loaded.shape();
        const auto oldLength = loaded.features()[1].length;
        extrude.length = 0;
        rejected([&] { loaded.replaceFeature(extrude); });
        require(loaded.shape().IsSame(priorShape) && loaded.features()[1].length == oldLength,
                "Invalid feature edit must preserve shape and parameters");
        auto badSketch = loaded.features()[0];
        badSketch.sketch.entities[0].second = badSketch.sketch.entities[0].first;
        rejected([&] { loaded.replaceFeature(badSketch); });
        require(loaded.shape().IsSame(priorShape), "Invalid source-sketch edit must preserve current part");
        auto removedContour = loaded.features()[0];
        removedContour.sketch.entities.erase(removedContour.sketch.entities.begin());
        rejected([&] { loaded.replaceFeature(removedContour); });
        require(loaded.shape().IsSame(priorShape), "Missing selected contour must fail rather than switching to another profile");
        auto replacedContour = loaded.features()[0];
        replacedContour.sketch.entities.erase(replacedContour.sketch.entities.begin());
        auto replacementRectangle = rectangle().entities.front();
        replacementRectangle.first = {-20, -20};
        replacementRectangle.second = {60, 60};
        replacementRectangle.id.clear();
        replacedContour.sketch.entities.push_back(replacementRectangle);
        rejected([&] { loaded.replaceFeature(replacedContour); });
        require(loaded.shape().IsSame(priorShape), "New entities must not inherit a removed contour's persistent identity");
        auto badContour = loaded.features()[1]; badContour.contour = {"missing-entity"};
        rejected([&] { loaded.replaceFeature(badContour); });
        auto badReference = loaded.features()[1]; badReference.sketchId = "missing";
        rejected([&] { loaded.replaceFeature(badReference); });

        const auto goodRoot = QJsonDocument::fromJson(read(path)).object();
        require(goodRoot.value("version").toInt() == 7, "New documents must use schema 7");
        const QString invalid = directory.filePath("invalid.thepart");
        const auto rejectRoot = [&](QJsonObject root) {
            write(invalid, QJsonDocument(root).toJson());
            rejected([&] { (void)PartDocument::load(invalid); });
        };
        auto bad = goodRoot; bad["version"] = 99; rejectRoot(bad);
        bad = goodRoot; bad["version"] = 2.5; rejectRoot(bad);
        bad = goodRoot; bad["format"] = "TheCAD.Assembly"; rejectRoot(bad);
        bad = goodRoot; bad["units"] = "inch"; rejectRoot(bad);
        bad = goodRoot; bad["resultFeature"] = sketchId.c_str(); rejectRoot(bad);
        bad = goodRoot; bad["futureState"] = 42; rejectRoot(bad);
        auto history = goodRoot.value("features").toArray();
        auto feature = history.at(1).toObject(); feature["dependencies"] = QJsonArray{"missing"}; history[1] = feature;
        bad = goodRoot; bad["features"] = history; rejectRoot(bad);
        history = goodRoot.value("features").toArray();
        feature = history.at(1).toObject(); feature["sketch"] = circleId.c_str(); history[1] = feature;
        bad = goodRoot; bad["features"] = history; rejectRoot(bad); // forward dependency
        history = goodRoot.value("features").toArray();
        feature = history.at(0).toObject(); feature["id"] = "bad\n"; history[0] = feature;
        bad = goodRoot; bad["features"] = history; rejectRoot(bad);
        history = goodRoot.value("features").toArray();
        feature = history.at(2).toObject(); feature["id"] = sketchId.c_str(); history[2] = feature;
        bad = goodRoot; bad["features"] = history; rejectRoot(bad);
        history = goodRoot.value("features").toArray();
        feature = history.at(1).toObject(); feature["type"] = "fillet"; history[1] = feature;
        bad = goodRoot; bad["features"] = history; rejectRoot(bad);
        for (const auto bytes : {QByteArray(), QByteArray("not JSON"), QByteArray("[]"), QByteArray("{}")}) {
            write(invalid, bytes); rejected([&] { (void)PartDocument::load(invalid); });
        }
        write(invalid, QByteArray(1024 * 1024 + 1, ' ')); rejected([&] { (void)PartDocument::load(invalid); });
        rejected([&] { (void)PartDocument::load(directory.filePath("missing.thepart")); });
        rejected([&] { (void)PartDocument::createBox({std::numeric_limits<double>::quiet_NaN(), 2, 3}); });

        // Genuine schema-1 legacy fixture is read-only. Conversion changes the
        // destination extension and schema, preserving the original bytes/ID.
        const QJsonObject legacyFeature{{"id", "feature_custom_42"}, {"type", "box"},
            {"parameters", QJsonObject{{"width", 125.25}, {"depth", 21.5}, {"height", 8.75}}}, {"dependencies", QJsonArray{}}};
        const QJsonObject legacy{{"format", "TheCAD.Part"}, {"version", 1}, {"units", "mm"},
            {"features", QJsonArray{legacyFeature}}, {"resultFeature", "feature_custom_42"}};
        const auto legacyPath = directory.filePath("original.tcpart");
        const auto legacyBytes = QJsonDocument(legacy).toJson();
        write(legacyPath, legacyBytes);
        auto legacyPart = PartDocument::load(legacyPath);
        checkBox(legacyPart, {125.25, 21.5, 8.75});
        rejected([&] { legacyPart.save(legacyPath); });
        require(read(legacyPath) == legacyBytes, "Save must never overwrite a legacy .tcpart file");
        const auto converted = directory.filePath("converted.THEPART");
        legacyPart.save(converted);
        checkBox(PartDocument::load(converted), {125.25, 21.5, 8.75});
        require(PartDocument::load(converted).features()[0].id == "feature_custom_42", "Legacy stable ID must survive conversion");
        require(read(legacyPath) == legacyBytes, "Conversion must retain original legacy file bytes");
        auto invalidLegacy = legacy; invalidLegacy["features"] = QJsonArray{}; rejectRoot(invalidLegacy);
        auto invalidBox = legacyFeature; invalidBox["dependencies"] = QJsonArray{"feature_custom_42"};
        invalidLegacy = legacy; invalidLegacy["features"] = QJsonArray{invalidBox}; rejectRoot(invalidLegacy);
        rejected([&] { legacyPart.save(directory.filePath("assembly.theasmb")); });

        const auto priorBytes = read(converted);
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(converted.utf16()), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Existing native document must lock");
        bool failed = false;
        try { part.save(converted); } catch (const std::runtime_error&) { failed = true; }
        CloseHandle(locked);
        require(failed && read(converted) == priorBytes, "Failed atomic save must preserve the existing file");
        std::cout << "PASS: empty/sketch/box history, extrude/cut regeneration, transactional edits, schema/IDs/dependencies, legacy conversion, atomic save safety\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
