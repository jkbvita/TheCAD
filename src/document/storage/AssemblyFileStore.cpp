#include "document/storage/AssemblyFileStore.h"
#include "document/AssemblyDocument.h"
#include "document/ReferenceGeometryData.h"
#include "document/storage/AssemblyMateData.h"
#include "document/storage/AssemblyWorkflowStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>

namespace thecad::document
{
namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
QString q(const std::string& value)
{
    return QString::fromStdString(value);
}
QString text(const QJsonValue& value, int limit = 32767)
{
    require(value.isString() && value.toString().size() <= limit &&
                !value.toString().contains(QChar(0)),
            "Invalid assembly text field.");
    return value.toString();
}
double number(const QJsonValue& value)
{
    require(value.isDouble() && std::isfinite(value.toDouble()) &&
                std::abs(value.toDouble()) <= 1e9,
            "Invalid assembly numeric field.");
    return value.toDouble();
}
void fields(const QJsonObject& object, const QSet<QString>& expected)
{
    const auto keys = object.keys();
    require(QSet<QString>(keys.begin(), keys.end()) == expected,
            "Unknown or missing assembly fields.");
}
std::string identifier(const QJsonValue& value)
{
    const auto id = text(value, 128).toStdString();
    require(!id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c)
                                       { return std::isalnum(c) || c == '-' || c == '_'; }),
            "Invalid assembly identifier.");
    return id;
}
QJsonObject reference(const AssemblyReference& ref)
{
    return {{"component", q(ref.componentId)}, {"geometry", writeGeometryReference(ref.geometry)}};
}
AssemblyReference reference(const QJsonValue& value)
{
    require(value.isObject(), "Invalid mate reference.");
    const auto object = value.toObject();
    fields(object, {"component", "geometry"});
    require(object["geometry"].isObject(), "Invalid mate geometry reference.");
    AssemblyReference ref;
    ref.componentId = text(object["component"], 128).toStdString();
    if (!ref.componentId.empty())
        ref.componentId = identifier(object["component"]);
    ref.geometry = readGeometryReference(object["geometry"].toObject(), 27);
    return ref;
}
} // namespace
AssemblyDocument AssemblyFileStore::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open assembly: " + file.errorString().toStdString());
    require(file.size() > 0 && file.size() <= 4 * 1024 * 1024, "Invalid assembly document size.");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "Assembly is not valid JSON.");
    const auto root = parsed.object();
    QSet<QString> rootFields{"format", "version", "units", "components", "mates"};
    if (root.contains("workflows"))
        rootFields.insert("workflows");
    fields(root, rootFields);
    const double version = number(root["version"]);
    require(root["format"] == "TheCAD.Assembly" && (version == 1 || version == 2) &&
                root["units"] == "mm",
            "Unsupported assembly format, version or units.");
    require(version == 2 || !root.contains("workflows"), "Assembly workflows require schema 2.");
    require(root["components"].isArray() && root["components"].toArray().size() <= 64 &&
                root["mates"].isArray() && root["mates"].toArray().size() <= 256,
            "Invalid assembly component/mate list.");
    AssemblyDocument result;
    const QDir directory(QFileInfo(path).absolutePath());
    std::map<QString, AssemblyComponent> cache;
    for (const auto& value : root["components"].toArray())
    {
        require(value.isObject(), "Invalid assembly component.");
        const auto object = value.toObject();
        QSet<QString> componentFields{"id",        "name",  "source", "sourceDigest",
                                      "placement", "fixed", "visible"};
        if (object.contains("embeddedPart"))
            componentFields.insert("embeddedPart");
        fields(object, componentFields);
        const auto source = text(object["source"]);
        AssemblyComponent component;
        if (object.contains("embeddedPart"))
        {
            require(version == 2 && source.isEmpty() && object["embeddedPart"].isObject(),
                    "Owned part data requires schema 2 and cannot also name an external source.");
            component = AssemblyDocument::ownedSource(
                QJsonDocument(object["embeddedPart"].toObject()).toJson(QJsonDocument::Indented));
        }
        else
        {
            require(!source.isEmpty(), "Missing component source path.");
            const auto absolute = QDir::cleanPath(directory.absoluteFilePath(source));
            const auto key = absolute.toCaseFolded();
            auto found = cache.find(key);
            if (found == cache.end())
                found = cache.emplace(key, AssemblyDocument::loadSource(absolute)).first;
            component = found->second;
        }
        component.id = identifier(object["id"]);
        component.name = text(object["name"], 256).toStdString();
        require(object["fixed"].isBool() && object["visible"].isBool(),
                "Invalid component Fix/Float or visibility flag.");
        component.fixed = object["fixed"].toBool();
        component.visible = object["visible"].toBool();
        const auto digest = text(object["sourceDigest"], 64);
        require(digest.size() == 64, "Invalid source digest.");
        result.m_sourcesChanged |= digest != component.digest;
        require(object["placement"].isArray() && object["placement"].toArray().size() == 12,
                "A rigid placement requires twelve matrix values.");
        const auto values = object["placement"].toArray();
        double matrix[3][4];
        for (int i = 0; i < 12; ++i)
            matrix[i / 4][i % 4] = number(values[i]);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                double dot = 0;
                for (int k = 0; k < 3; ++k)
                    dot += matrix[i][k] * matrix[j][k];
                require(std::abs(dot - (i == j ? 1 : 0)) < 1e-8,
                        "Component matrix is not a rigid rotation.");
            }
        component.placement.SetValues(matrix[0][0], matrix[0][1], matrix[0][2], matrix[0][3],
                                      matrix[1][0], matrix[1][1], matrix[1][2], matrix[1][3],
                                      matrix[2][0], matrix[2][1], matrix[2][2], matrix[2][3]);
        modeling::assembly::validateRigidPlacement(component.placement);
        result.m_components.push_back(std::move(component));
    }
    for (const auto& value : root["mates"].toArray())
    {
        require(value.isObject(), "Invalid assembly mate.");
        const auto object = value.toObject();
        if (version == 2)
        {
            result.m_mates.push_back(readAssemblyMate(object));
            continue;
        }
        fields(object, {"id", "name", "kind", "first", "second", "value", "alignment"});
        AssemblyMate mate;
        mate.id = identifier(object["id"]);
        mate.name = text(object["name"], 256).toStdString();
        const auto kind = number(object["kind"]);
        require(kind >= 0 && kind <= 3 && std::floor(kind) == kind, "Unsupported mate type.");
        mate.kind = AssemblyMateKind(int(kind));
        mate.first = reference(object["first"]);
        mate.second = reference(object["second"]);
        mate.value = number(object["value"]);
        const auto alignment = number(object["alignment"]);
        require(alignment == 1 || alignment == -1, "Invalid mate alignment.");
        mate.alignment = int(alignment);
        result.m_mates.push_back(std::move(mate));
    }
    if (root.contains("workflows"))
    {
        require(root["workflows"].isObject(), "Invalid assembly workflow metadata.");
        AssemblyWorkflowStore::read(result, root["workflows"].toObject());
    }
    result.solve();
    return result;
}
void AssemblyFileStore::save(const AssemblyDocument& document, const QString& path)
{
    const bool version2 =
        !document.m_assets.empty() || !document.m_belts.empty() || !document.m_inPlaces.empty() ||
        document.m_groundPlane ||
        std::any_of(document.m_mates.begin(), document.m_mates.end(),
                    [](const auto& mate)
                    {
                        return mate.definition.has_value() ||
                               mate.status != modeling::assembly::MateStatus::Active ||
                               !mate.diagnostic.empty();
                    });
    QJsonArray components, mates;
    const QDir directory(QFileInfo(path).absolutePath());
    for (const auto& c : document.m_components)
    {
        QJsonArray matrix;
        for (int i = 1; i <= 3; ++i)
            for (int j = 1; j <= 4; ++j)
                matrix.append(c.placement.Value(i, j));
        QJsonObject component{
            {"id", q(c.id)},
            {"name", q(c.name)},
            {"source", c.embeddedPart.isEmpty() ? directory.relativeFilePath(c.path) : QString{}},
            {"sourceDigest", c.digest},
            {"placement", matrix},
            {"fixed", c.fixed},
            {"visible", c.visible}};
        if (!c.embeddedPart.isEmpty())
            component["embeddedPart"] = QJsonDocument::fromJson(c.embeddedPart).object();
        components.append(component);
    }
    for (const auto& m : document.m_mates)
        if (version2)
            mates.append(writeAssemblyMate(m));
        else
            mates.append(QJsonObject{{"id", q(m.id)},
                                     {"name", q(m.name)},
                                     {"kind", int(m.kind)},
                                     {"first", reference(m.first)},
                                     {"second", reference(m.second)},
                                     {"value", m.value},
                                     {"alignment", m.alignment}});
    QJsonObject root{{"format", "TheCAD.Assembly"},
                     {"version", version2 ? 2 : 1},
                     {"units", "mm"},
                     {"components", components},
                     {"mates", mates}};
    if (version2)
        root["workflows"] = AssemblyWorkflowStore::write(document);
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    require(bytes.size() <= 4 * 1024 * 1024, "Assembly exceeds its file-size limit.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save assembly atomically: " +
                                 file.errorString().toStdString());
}
} // namespace thecad::document
