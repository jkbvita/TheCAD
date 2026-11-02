#include "document/PartDocument.h"
#include "modeling/Box.h"

#include <BRepCheck_Analyzer.hxx>
#include <Standard_Failure.hxx>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace thecad::document {
namespace {
constexpr qint64 maximumDocumentBytes = 1024 * 1024;
constexpr double maximumLengthMillimeters = 1e9;

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

void requireFields(const QJsonObject& object, const QSet<QString>& fields, const QString& location)
{
    const auto keys = object.keys();
    if (QSet<QString>(keys.cbegin(), keys.cend()) != fields) {
        fail(location + ": missing or unsupported fields. This file may require a newer TheCAD version.");
    }
}

void requirePartExtension(const QString& path)
{
    if (QFileInfo(path).suffix().compare("tcpart", Qt::CaseInsensitive) != 0) {
        fail("Part documents must use the .tcpart extension.");
    }
}

double readLength(const QJsonObject& parameters, const QString& key)
{
    const QJsonValue value = parameters.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        fail("Box parameter '" + key + "' must be a finite number.");
    }
    return value.toDouble();
}
}

PartDocument::PartDocument(BoxParameters parameters, TopoDS_Shape shape, QString featureId)
    : m_parameters(parameters), m_shape(std::move(shape)), m_featureId(std::move(featureId))
{
}

PartDocument PartDocument::createBox(BoxParameters parameters)
{
    try {
        for (const double length : {parameters.width, parameters.depth, parameters.height}) {
            if (!std::isfinite(length) || length > maximumLengthMillimeters) {
                fail("Box dimensions must be finite and no larger than 1,000,000,000 mm.");
            }
        }
        if (!std::isfinite(parameters.width * parameters.depth * parameters.height)) {
            fail("Box dimensions produce an unsupported non-finite volume.");
        }
        auto shape = modeling::makeBox(parameters.width, parameters.depth, parameters.height);
        if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid()) {
            fail("The box parameters do not produce valid solid geometry.");
        }
        return PartDocument(parameters, std::move(shape), "box-1");
    } catch (const Standard_Failure& error) {
        fail("Cannot create part geometry: " + QString::fromUtf8(error.GetMessageString()));
    } catch (const std::invalid_argument& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

PartDocument PartDocument::load(const QString& path)
{
    requirePartExtension(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail("Cannot open part document: " + file.errorString());
    }
    if (file.size() > maximumDocumentBytes) {
        fail("Part document exceeds the current 1 MiB prototype size limit.");
    }
    const QByteArray bytes = file.read(maximumDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximumDocumentBytes) {
        fail("Cannot read part document, or the document exceeds the prototype size limit.");
    }
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail("Invalid part document JSON: " + parseError.errorString());
    }
    if (!json.isObject()) {
        fail("Part document must contain a JSON object.");
    }
    const auto root = json.object();
    requireFields(root, {"format", "version", "units", "features", "resultFeature"}, "Part document");
    if (root.value("format") != QJsonValue("TheCAD.Part")) {
        fail("Unsupported document format; expected a native TheCAD part document.");
    }
    if (!root.value("version").isDouble() || root.value("version").toDouble() != 1.0) {
        fail("Unsupported part document version. This application supports version 1 only.");
    }
    if (root.value("units") != QJsonValue("mm")) {
        fail("Unsupported part document units. Version 1 supports millimeters only.");
    }
    if (!root.value("features").isArray() || root.value("features").toArray().size() != 1
        || !root.value("features").toArray().at(0).isObject()) {
        fail("Unsupported feature history. This prototype supports exactly one box feature.");
    }
    const auto feature = root.value("features").toArray().at(0).toObject();
    requireFields(feature, {"id", "type", "parameters", "dependencies"}, "Box feature");
    const auto idValue = feature.value("id");
    static const QRegularExpression identifier(R"(\A[A-Za-z0-9_-]{1,128}\z)");
    if (!idValue.isString() || !identifier.match(idValue.toString()).hasMatch()) {
        fail("Invalid feature identifier.");
    }
    if (feature.value("type") != QJsonValue("box")) {
        fail("Unsupported feature type. This prototype can load box features only.");
    }
    if (!feature.value("dependencies").isArray() || !feature.value("dependencies").toArray().isEmpty()) {
        fail("Unsupported feature dependencies. Version 1 box features must have no dependencies.");
    }
    if (root.value("resultFeature") != idValue) {
        fail("Part resultFeature must reference its box feature identifier.");
    }
    if (!feature.value("parameters").isObject()) {
        fail("Box feature parameters must be an object.");
    }
    const auto parameters = feature.value("parameters").toObject();
    requireFields(parameters, {"width", "depth", "height"}, "Box parameters");
    auto result = createBox({readLength(parameters, "width"), readLength(parameters, "depth"),
                             readLength(parameters, "height")});
    result.m_featureId = idValue.toString();
    return result;
}

void PartDocument::save(const QString& path) const
{
    requirePartExtension(path);
    const QJsonObject parameters{{"width", m_parameters.width}, {"depth", m_parameters.depth},
                                 {"height", m_parameters.height}};
    const QJsonObject feature{{"id", m_featureId}, {"type", "box"}, {"parameters", parameters},
                              {"dependencies", QJsonArray{}}};
    const QJsonObject root{{"format", "TheCAD.Part"}, {"version", 1}, {"units", "mm"},
                           {"features", QJsonArray{feature}}, {"resultFeature", m_featureId}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    file.setDirectWriteFallback(false); // Never truncate a valid existing document on failure.
    if (!file.open(QIODevice::WriteOnly)) {
        fail("Cannot save part document: " + file.errorString());
    }
    if (file.write(bytes) != bytes.size()) {
        fail("Cannot write part document: " + file.errorString());
    }
    if (!file.commit()) {
        fail("Cannot replace part document atomically: " + file.errorString());
    }
}

} // namespace thecad::document
