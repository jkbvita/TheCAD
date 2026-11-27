#include "document/storage/AssemblyWorkflowStore.h"
#include "document/AssemblyDocument.h"
#include "document/storage/AssemblyMateData.h"
#include "document/ReferenceGeometryData.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::document
{
namespace
{
void require(bool value, const char* error)
{
    if (!value)
        throw std::runtime_error(error);
}
void fields(const QJsonObject& o, QSet<QString> expected)
{
    const auto keys = o.keys();
    require(QSet<QString>(keys.begin(), keys.end()) == expected,
            "Unknown or missing assembly workflow fields.");
}
QJsonObject object(const QJsonValue& v)
{
    require(v.isObject(), "Assembly workflow data must be an object.");
    return v.toObject();
}
QJsonArray array(const QJsonValue& v, int max)
{
    require(v.isArray() && v.toArray().size() <= max, "Assembly workflow list exceeds its limit.");
    return v.toArray();
}
std::string text(const QJsonValue& v, bool identifier = false, bool empty = false)
{
    require(v.isString() && v.toString().size() <= (identifier ? 128 : 256) &&
                !v.toString().contains(QChar(0)),
            "Invalid assembly workflow text.");
    const auto s = v.toString().toStdString();
    require(empty || !s.empty(), "Missing assembly workflow text.");
    if (identifier)
        require(std::all_of(s.begin(), s.end(),
                            [](unsigned char c)
                            {
                                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                       (c >= '0' && c <= '9') || c == '-' || c == '_';
                            }),
                "Invalid workflow identifier.");
    return s;
}
double number(const QJsonValue& v)
{
    require(v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) <= 1e9,
            "Invalid finite workflow parameter.");
    return v.toDouble();
}
bool boolean(const QJsonValue& v)
{
    require(v.isBool(), "Invalid workflow flag.");
    return v.toBool();
}
QJsonValue optional(const std::optional<AssemblyReference>& ref)
{
    return ref ? QJsonValue(writeAssemblyReference(*ref)) : QJsonValue(QJsonValue::Null);
}
} // namespace
QJsonObject AssemblyWorkflowStore::write(const AssemblyDocument& doc)
{
    QJsonArray assets, belts, inPlace;
    for (const auto& asset : doc.m_assets)
    {
        QJsonArray connectors;
        for (const auto& connector : asset.connectors)
            connectors.append(
                QJsonObject{{"id", QString::fromStdString(connector.id)},
                            {"name", QString::fromStdString(connector.name)},
                            {"point", writeGeometryReference(connector.point)},
                            {"direction", writeGeometryReference(connector.direction)},
                            {"offset", writeAssemblyTransform(connector.offset)}});
        assets.append(
            QJsonObject{{"component", QString::fromStdString(asset.componentId)},
                        {"groundPlane", asset.groundPlane
                                            ? QJsonValue(writeGeometryReference(*asset.groundPlane))
                                            : QJsonValue(QJsonValue::Null)},
                        {"connectors", connectors},
                        {"grounded", asset.grounded}});
    }
    for (const auto& belt : doc.m_belts)
    {
        QJsonArray pulleys, diameters, sides;
        for (const auto& ref : belt.pulleys)
            pulleys.append(writeAssemblyReference(ref));
        for (double diameter : belt.diameters)
            diameters.append(diameter);
        for (int side : belt.sides)
            sides.append(side);
        belts.append(QJsonObject{{"id", QString::fromStdString(belt.id)},
                                 {"name", QString::fromStdString(belt.name)},
                                 {"component", QString::fromStdString(belt.componentId)},
                                 {"pulleys", pulleys},
                                 {"diameters", diameters},
                                 {"sides", sides},
                                 {"location", optional(belt.location)},
                                 {"engaged", belt.engaged},
                                 {"driving", belt.driving},
                                 {"useThickness", belt.useThickness},
                                 {"createPart", belt.createPart},
                                 {"length", belt.length},
                                 {"thickness", belt.thickness},
                                 {"lockPart", belt.lockPart}});
    }
    for (const auto& part : doc.m_inPlaces)
        inPlace.append(QJsonObject{{"id", QString::fromStdString(part.id)},
                                   {"component", QString::fromStdString(part.componentId)},
                                   {"support", writeAssemblyReference(part.support)},
                                   {"offset", writeAssemblyTransform(part.offset)},
                                   {"linked", part.linked}});
    return {{"assets", assets},
            {"groundPlane", optional(doc.m_groundPlane)},
            {"belts", belts},
            {"inPlace", inPlace}};
}
void AssemblyWorkflowStore::read(AssemblyDocument& doc, const QJsonObject& root)
{
    fields(root, {"assets", "groundPlane", "belts", "inPlace"});
    doc.m_assets.clear();
    doc.m_belts.clear();
    doc.m_inPlaces.clear();
    doc.m_groundPlane.reset();
    if (!root["groundPlane"].isNull())
        doc.m_groundPlane = readAssemblyReference(root["groundPlane"]);
    for (const auto& value : array(root["assets"], 64))
    {
        const auto o = object(value);
        QSet<QString> assetFields{"component", "groundPlane", "connectors"};
        if (o.contains("grounded"))
            assetFields.insert("grounded");
        fields(o, assetFields);
        PublishedAsset asset;
        if (o.contains("grounded"))
            asset.grounded = boolean(o["grounded"]);
        asset.componentId = text(o["component"], true);
        if (!o["groundPlane"].isNull())
            asset.groundPlane = readGeometryReference(object(o["groundPlane"]), 27);
        for (const auto& c : array(o["connectors"], 64))
        {
            const auto data = object(c);
            fields(data, {"id", "name", "point", "direction", "offset"});
            asset.connectors.push_back({text(data["id"], true), text(data["name"]),
                                        readGeometryReference(object(data["point"]), 27),
                                        readGeometryReference(object(data["direction"]), 27),
                                        readAssemblyTransform(data["offset"])});
        }
        doc.m_assets.push_back(std::move(asset));
    }
    for (const auto& value : array(root["belts"], 64))
    {
        const auto o = object(value);
        QSet<QString> beltFields{"id",         "name",     "component", "pulleys", "diameters",
                                 "sides",      "location", "engaged",   "driving", "useThickness",
                                 "createPart", "length",   "thickness"};
        if (o.contains("lockPart"))
            beltFields.insert("lockPart");
        fields(o, beltFields);
        AssemblyBelt belt;
        if (o.contains("lockPart"))
            belt.lockPart = boolean(o["lockPart"]);
        belt.id = text(o["id"], true);
        belt.name = text(o["name"]);
        belt.componentId = text(o["component"], true, true);
        for (const auto& ref : array(o["pulleys"], 64))
            belt.pulleys.push_back(readAssemblyReference(ref));
        for (const auto& diameter : array(o["diameters"], 64))
            belt.diameters.push_back(number(diameter));
        for (const auto& side : array(o["sides"], 64))
        {
            const auto n = number(side);
            require(n == -1 || n == 1, "Invalid belt routing side.");
            belt.sides.push_back(int(n));
        }
        if (!o["location"].isNull())
            belt.location = readAssemblyReference(o["location"]);
        belt.engaged = boolean(o["engaged"]);
        belt.driving = boolean(o["driving"]);
        belt.useThickness = boolean(o["useThickness"]);
        belt.createPart = boolean(o["createPart"]);
        belt.length = number(o["length"]);
        belt.thickness = number(o["thickness"]);
        doc.m_belts.push_back(std::move(belt));
    }
    for (const auto& value : array(root["inPlace"], 64))
    {
        const auto o = object(value);
        QSet<QString> partFields{"id", "component", "support", "offset"};
        if (o.contains("linked"))
            partFields.insert("linked");
        fields(o, partFields);
        doc.m_inPlaces.push_back({text(o["id"], true), text(o["component"], true),
                                  readAssemblyReference(o["support"]),
                                  readAssemblyTransform(o["offset"]),
                                  o.contains("linked") ? boolean(o["linked"]) : true});
    }
}
} // namespace thecad::document
