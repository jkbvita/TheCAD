#include "document/PartDocument.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>

#include <QCoreApplication>
#include <QDir>
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

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using thecad::document::PartDocument;
using thecad::document::BoxParameters;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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
    require(file.write(bytes) == bytes.size(), "Test file must be written");
}

void rejected(const std::function<void()>& action)
{
    bool failed = false;
    try {
        action();
    } catch (const std::runtime_error& error) {
        failed = !QString::fromUtf8(error.what()).isEmpty();
    }
    require(failed, "Invalid operation must throw a clear runtime_error");
}

void checkGeometry(const PartDocument& document, const BoxParameters& parameters)
{
    require(document.parameters().width == parameters.width
            && document.parameters().depth == parameters.depth
            && document.parameters().height == parameters.height,
            "Part parameters must survive saving/loading exactly");
    const auto& shape = document.shape();
    require(!shape.IsNull() && BRepCheck_Analyzer(shape).IsValid(), "Loaded BRep must be valid");
    int solids = 0;
    int faces = 0;
    for (TopExp_Explorer item(shape, TopAbs_SOLID); item.More(); item.Next()) { ++solids; }
    for (TopExp_Explorer item(shape, TopAbs_FACE); item.More(); item.Next()) { ++faces; }
    require(solids == 1 && faces == 6, "Loaded BRep must have one solid and six faces");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    const double volume = parameters.width * parameters.depth * parameters.height;
    require(std::abs(properties.Mass() - volume) < volume * 1e-10,
            "Loaded volume must match parameters");
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double tolerance = 10.0 * Precision::Confusion();
    require(std::abs(xmin) < tolerance && std::abs(ymin) < tolerance && std::abs(zmin) < tolerance,
            "Loaded geometry must start at origin");
    require(std::abs(xmax - parameters.width) < tolerance
            && std::abs(ymax - parameters.depth) < tolerance
            && std::abs(zmax - parameters.height) < tolerance,
            "Loaded geometry bounds must match parameters");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "Temporary directory must exist");
        const QString path = directory.filePath(QString::fromUtf8("příliš-žluťoučký.tcpart"));
        const BoxParameters first{125.25, 21.5, 8.75};
        const auto original = PartDocument::createBox(first);
        original.save(path);
        checkGeometry(PartDocument::load(path), first);
        const QByteArray initialBytes = read(path);
        const auto base = QJsonDocument::fromJson(initialBytes).object();
        require(base.value("format").toString() == "TheCAD.Part", "Format marker must identify native part");
        require(base.value("version").toInt() == 1, "Schema must be versioned");
        require(!base.contains("mesh") && !base.contains("image"), "File must contain actual modeling inputs");

        const BoxParameters second{10.0, 20.0, 30.0};
        const auto changed = PartDocument::createBox(second);
        changed.save(path); // Save reuses the current file rather than appending.
        checkGeometry(PartDocument::load(path), second);
        const QString saveAs = directory.filePath("second.TCPART");
        changed.save(saveAs);
        checkGeometry(PartDocument::load(saveAs), second);

        // Preserve a caller-provided stable feature identifier rather than renumbering.
        auto withId = base;
        auto feature = withId.value("features").toArray().first().toObject();
        feature["id"] = "feature_custom_42";
        withId["features"] = QJsonArray{feature};
        withId["resultFeature"] = "feature_custom_42";
        write(path, QJsonDocument(withId).toJson());
        PartDocument::load(path).save(saveAs);
        require(QJsonDocument::fromJson(read(saveAs)).object().value("resultFeature").toString()
                == "feature_custom_42", "Feature IDs must survive round-trip");

        const QString invalidPath = directory.filePath("invalid.tcpart");
        const auto rejectRoot = [&](QJsonObject root) {
            write(invalidPath, QJsonDocument(root).toJson());
            rejected([&] { (void)PartDocument::load(invalidPath); });
            checkGeometry(original, first); // Failed loads cannot mutate an existing document.
        };
        auto bad = base; bad["version"] = 2; rejectRoot(bad);
        bad = base; bad["version"] = 1.5; rejectRoot(bad);
        bad = base; bad["version"] = "1"; rejectRoot(bad);
        bad = base; bad["format"] = "TheCAD.Assembly"; rejectRoot(bad);
        bad = base; bad["units"] = "inch"; rejectRoot(bad);
        bad = base; bad["resultFeature"] = "missing"; rejectRoot(bad);
        bad = base; bad["features"] = QJsonArray{}; rejectRoot(bad);
        bad = base; bad["features"] = QJsonArray{feature, feature}; rejectRoot(bad);
        bad = base; bad["futureState"] = 1; rejectRoot(bad);
        bad = base; bad.remove("units"); rejectRoot(bad);
        const auto rejectFeature = [&](QJsonObject box) {
            auto root = base;
            root["features"] = QJsonArray{box};
            rejectRoot(root);
        };
        feature = base.value("features").toArray().first().toObject();
        auto badFeature = feature; badFeature["type"] = "extrusion"; rejectFeature(badFeature);
        badFeature = feature; badFeature["dependencies"] = QJsonArray{"box-1"}; rejectFeature(badFeature);
        badFeature = feature; badFeature["dependencies"] = ""; rejectFeature(badFeature);
        badFeature = feature; badFeature["id"] = ""; rejectFeature(badFeature);
        badFeature = feature; badFeature["id"] = "bad id"; rejectFeature(badFeature);
        badFeature = feature; badFeature["id"] = "box-1\n"; rejectFeature(badFeature);
        badFeature = feature; badFeature["id"] = 42; rejectFeature(badFeature);
        badFeature = feature; badFeature["id"] = QString(129, QChar('a')); rejectFeature(badFeature);
        badFeature = feature; badFeature["futureFlag"] = true; rejectFeature(badFeature);
        badFeature = feature; badFeature["parameters"] = 123; rejectFeature(badFeature);
        const auto parameters = feature.value("parameters").toObject();
        for (const QJsonValue value : {QJsonValue(0.0), QJsonValue(-1.0),
                QJsonValue(Precision::Confusion()), QJsonValue("ten"), QJsonValue(QJsonValue::Null),
                QJsonValue(1e300)}) {
            auto badParameters = parameters;
            badParameters["width"] = value;
            badFeature = feature;
            badFeature["parameters"] = badParameters;
            rejectFeature(badFeature);
        }
        for (const QByteArray bytes : {QByteArray("not JSON"), QByteArray("[]"), QByteArray("{}"),
                                      QByteArray()}) {
            write(invalidPath, bytes);
            rejected([&] { (void)PartDocument::load(invalidPath); });
        }
        write(invalidPath, QByteArray(1024 * 1024 + 1, ' '));
        rejected([&] { (void)PartDocument::load(invalidPath); });
        rejected([&] { (void)PartDocument::load(directory.filePath("missing.tcpart")); });
        rejected([&] { original.save(directory.filePath("assembly.tcasmb")); });
        rejected([&] { (void)PartDocument::createBox({std::numeric_limits<double>::quiet_NaN(), 2, 3}); });

        // Deny deletion/replacement while permitting reads. Atomic save must fail
        // without truncating the old file when Windows blocks the final replace.
        const QByteArray beforeFailure = read(saveAs);
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(saveAs.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Existing document must lock for failed-save check");
        bool failureWasReported = false;
        try {
            original.save(saveAs);
        } catch (const std::runtime_error&) {
            failureWasReported = true;
        }
        CloseHandle(locked);
        require(failureWasReported, "Locked destination save must fail clearly");
        require(read(saveAs) == beforeFailure, "Failed atomic replacement must leave original bytes intact");
        checkGeometry(PartDocument::load(saveAs), first);
        std::cout << "PASS: parameter/BRep round-trip, Save/Save As, Unicode paths, stable IDs, "
                     "invalid files, unsupported schema/features/dependencies, atomic failure safety\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
