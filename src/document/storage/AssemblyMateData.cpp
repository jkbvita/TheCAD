#include "document/storage/AssemblyMateData.h"
#include "document/ReferenceGeometryData.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::document
{
using namespace modeling::assembly;
namespace
{
void require(bool v, const char* error)
{
    if (!v)
        throw std::runtime_error(error);
}
void fields(const QJsonObject& o, QSet<QString> expected)
{
    const auto keys = o.keys();
    require(QSet<QString>(keys.begin(), keys.end()) == expected,
            "Unknown or missing mate definition fields.");
}
QJsonObject object(const QJsonValue& v)
{
    require(v.isObject(), "Mate data must be an object.");
    return v.toObject();
}
QJsonArray array(const QJsonValue& v, int maximum)
{
    require(v.isArray() && v.toArray().size() <= maximum, "Invalid mate data array.");
    return v.toArray();
}
double number(const QJsonValue& v)
{
    require(v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) <= 1e9,
            "Invalid finite mate parameter.");
    return v.toDouble();
}
int integer(const QJsonValue& v, int lo, int hi)
{
    const double n = number(v);
    require(n >= lo && n <= hi && std::floor(n) == n, "Invalid mate enum or index.");
    return int(n);
}
bool boolean(const QJsonValue& v)
{
    require(v.isBool(), "Invalid Boolean mate option.");
    return v.toBool();
}
QString text(const QJsonValue& v, int max = 10000)
{
    require(v.isString() && v.toString().size() <= max && !v.toString().contains(QChar(0)),
            "Invalid mate text.");
    return v.toString();
}
std::string id(const QJsonValue& v, bool empty = false)
{
    const auto s = text(v, 128).toStdString();
    require((empty || !s.empty()) && std::all_of(s.begin(), s.end(),
                                                 [](unsigned char c)
                                                 {
                                                     return (c >= 'a' && c <= 'z') ||
                                                            (c >= 'A' && c <= 'Z') ||
                                                            (c >= '0' && c <= '9') || c == '-' ||
                                                            c == '_';
                                                 }),
            "Invalid mate identifier.");
    return s;
}
QJsonObject reference(const AssemblyReference& ref)
{
    QJsonObject object{{"component", QString::fromStdString(ref.componentId)},
                       {"geometry", writeGeometryReference(ref.geometry)}};
    if (!ref.connectorId.empty())
        object["connector"] = QString::fromStdString(ref.connectorId);
    return object;
}
AssemblyReference reference(const QJsonValue& v)
{
    const auto o = object(v);
    QSet<QString> expected{"component", "geometry"};
    if (o.contains("connector"))
        expected.insert("connector");
    fields(o, expected);
    AssemblyReference ref{id(o["component"], true),
                          readGeometryReference(object(o["geometry"]), 27)};
    if (o.contains("connector"))
    {
        ref.connectorId = id(o["connector"]);
        require(!ref.componentId.empty() &&
                    ref.geometry.source == modeling::ReferenceSource::Origin,
                "A published connector requires its component frame.");
    }
    return ref;
}
QJsonObject parameters(const MateParameters& p)
{
    QJsonArray diameters, sides;
    for (double value : p.belt.diameters)
        diameters.append(value);
    for (int side : p.belt.sides)
        sides.append(side);
    return {{"standard", QJsonObject{{"lockRotation", p.standard.lockRotation},
                                     {"alignAxes", p.standard.alignAxes},
                                     {"firstArc", int(p.standard.firstArc)},
                                     {"secondArc", int(p.standard.secondArc)},
                                     {"internalTangency", p.standard.internalTangency}}},
            {"limit", QJsonObject{{"minimum", p.limit.minimum}, {"maximum", p.limit.maximum}}},
            {"position", QJsonObject{{"mode", int(p.position.mode)},
                                     {"distance", p.position.distance},
                                     {"percent", p.position.percent},
                                     {"flip", p.position.flip}}},
            {"profile", QJsonObject{{"offset", p.profile.offset},
                                    {"rotation", p.profile.rotation},
                                    {"clockwise", p.profile.clockwise}}},
            {"path", QJsonObject{{"followAxis", p.path.followAxis},
                                 {"upAxis", p.path.upAxis},
                                 {"flipTangent", p.path.flipTangent},
                                 {"flipUp", p.path.flipUp}}},
            {"coupler", QJsonObject{{"firstRatio", p.coupler.firstRatio},
                                    {"secondRatio", p.coupler.secondRatio},
                                    {"pitch", p.coupler.pitch},
                                    {"reverse", p.coupler.reverse},
                                    {"useGeometryRatio", p.coupler.useGeometryRatio}}},
            {"hinge", QJsonObject{{"limits", p.hinge.limits}}},
            {"magnetic", QJsonObject{{"locked", p.magnetic.locked}}},
            {"belt", QJsonObject{{"engaged", p.belt.engaged},
                                 {"driving", p.belt.driving},
                                 {"length", p.belt.length},
                                 {"thickness", p.belt.thickness},
                                 {"diameters", diameters},
                                 {"sides", sides}}}};
}
MateParameters parameters(const QJsonValue& v)
{
    const auto o = object(v);
    QSet<QString> expected{"standard", "limit", "position", "profile", "path", "coupler", "hinge"};
    for (auto field : {"belt", "magnetic"})
        if (o.contains(field))
            expected.insert(field);
    fields(o, expected);
    MateParameters p;
    auto s = object(o["standard"]);
    fields(s, {"lockRotation", "alignAxes", "firstArc", "secondArc", "internalTangency"});
    p.standard = {boolean(s["lockRotation"]), boolean(s["alignAxes"]),
                  ArcCondition(integer(s["firstArc"], 0, 2)),
                  ArcCondition(integer(s["secondArc"], 0, 2)), boolean(s["internalTangency"])};
    s = object(o["limit"]);
    fields(s, {"minimum", "maximum"});
    p.limit = {number(s["minimum"]), number(s["maximum"])};
    s = object(o["position"]);
    fields(s, {"mode", "distance", "percent", "flip"});
    p.position = {PositionMode(integer(s["mode"], 0, 3)), number(s["distance"]),
                  number(s["percent"]), boolean(s["flip"])};
    s = object(o["profile"]);
    fields(s, {"offset", "rotation", "clockwise"});
    p.profile = {number(s["offset"]), number(s["rotation"]), boolean(s["clockwise"])};
    s = object(o["path"]);
    fields(s, {"followAxis", "upAxis", "flipTangent", "flipUp"});
    p.path = {integer(s["followAxis"], -1, 2), integer(s["upAxis"], -1, 2),
              boolean(s["flipTangent"]), boolean(s["flipUp"])};
    s = object(o["coupler"]);
    QSet<QString> couplerFields{"firstRatio", "secondRatio", "pitch", "reverse"};
    if (s.contains("useGeometryRatio"))
        couplerFields.insert("useGeometryRatio");
    fields(s, couplerFields);
    p.coupler = {number(s["firstRatio"]), number(s["secondRatio"]), number(s["pitch"]),
                 boolean(s["reverse"])};
    if (s.contains("useGeometryRatio"))
        p.coupler.useGeometryRatio = boolean(s["useGeometryRatio"]);
    s = object(o["hinge"]);
    fields(s, {"limits"});
    p.hinge.limits = boolean(s["limits"]);
    if (o.contains("magnetic"))
    {
        s = object(o["magnetic"]);
        fields(s, {"locked"});
        p.magnetic.locked = boolean(s["locked"]);
    }
    if (o.contains("belt"))
    {
        s = object(o["belt"]);
        fields(s, {"engaged", "driving", "length", "thickness", "diameters", "sides"});
        p.belt.engaged = boolean(s["engaged"]);
        p.belt.driving = boolean(s["driving"]);
        p.belt.length = number(s["length"]);
        p.belt.thickness = number(s["thickness"]);
        for (const auto& value : array(s["diameters"], 64))
            p.belt.diameters.push_back(number(value));
        for (const auto& value : array(s["sides"], 64))
        {
            const int side = integer(value, -1, 1);
            require(side != 0, "Belt routing side must be -1 or +1.");
            p.belt.sides.push_back(side);
        }
    }
    return p;
}
QJsonArray matrix(const gp_Trsf& t)
{
    QJsonArray values;
    for (int r = 1; r <= 3; ++r)
        for (int c = 1; c <= 4; ++c)
            values.append(t.Value(r, c));
    return values;
}
gp_Trsf matrix(const QJsonValue& v)
{
    const auto a = array(v, 12);
    require(a.size() == 12, "A rigid mate anchor requires 12 matrix values.");
    double m[12];
    for (int i = 0; i < 12; ++i)
        m[i] = number(a[i]);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
        {
            double dot = 0;
            for (int k = 0; k < 3; ++k)
                dot += m[r * 4 + k] * m[c * 4 + k];
            require(std::abs(dot - (r == c ? 1 : 0)) < 1e-8,
                    "Mate anchor is not a rigid transform.");
        }
    gp_Trsf t;
    t.SetValues(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    validateRigidPlacement(t);
    return t;
}
} // namespace
QJsonObject writeAssemblyReference(const AssemblyReference& ref)
{
    return reference(ref);
}
AssemblyReference readAssemblyReference(const QJsonValue& value)
{
    return reference(value);
}
QJsonArray writeAssemblyTransform(const gp_Trsf& value)
{
    return matrix(value);
}
gp_Trsf readAssemblyTransform(const QJsonValue& value)
{
    return matrix(value);
}
QJsonObject writeMateDefinition(const AssemblyMate::Definition& definition)
{
    QJsonArray groups, anchors, coordinates;
    for (const auto& group : definition.groups)
    {
        QJsonArray refs;
        for (const auto& ref : group.references)
            refs.append(reference(ref));
        groups.append(
            QJsonObject{{"role", QString::fromStdString(group.role)}, {"references", refs}});
    }
    for (const auto& t : definition.motion.anchors)
        anchors.append(matrix(t));
    for (double value : definition.motion.coordinates)
        coordinates.append(value);
    return {{"groups", groups},
            {"parameters", parameters(definition.parameters)},
            {"motion", QJsonObject{{"initialized", definition.motion.initialized},
                                   {"anchors", anchors},
                                   {"coordinates", coordinates}}}};
}
QJsonObject writeAssemblyMate(const AssemblyMate& m)
{
    return {{"id", QString::fromStdString(m.id)},
            {"name", QString::fromStdString(m.name)},
            {"kind", mateKey(m.kind)},
            {"first", reference(m.first)},
            {"second", reference(m.second)},
            {"value", m.value},
            {"alignment", m.alignment},
            {"definition", m.definition ? QJsonValue(writeMateDefinition(*m.definition))
                                        : QJsonValue(QJsonValue::Null)},
            {"status", m.status == MateStatus::Active       ? "active"
                       : m.status == MateStatus::Suppressed ? "suppressed"
                                                            : "unsolved"},
            {"diagnostic", QString::fromStdString(m.diagnostic)}};
}
AssemblyMate readAssemblyMate(const QJsonObject& o)
{
    fields(o, {"id", "name", "kind", "first", "second", "value", "alignment", "definition",
               "status", "diagnostic"});
    AssemblyMate m;
    m.id = id(o["id"]);
    m.name = text(o["name"], 256).toStdString();
    m.kind = mateFromKey(text(o["kind"], 64).toStdString());
    m.first = reference(o["first"]);
    m.second = reference(o["second"]);
    m.value = number(o["value"]);
    m.alignment = integer(o["alignment"], -1, 1);
    require(m.alignment != 0, "Invalid mate alignment.");
    const auto status = text(o["status"], 16);
    require(status == "active" || status == "suppressed" || status == "unsolved",
            "Unknown mate status.");
    m.status = status == "active"       ? MateStatus::Active
               : status == "suppressed" ? MateStatus::Suppressed
                                        : MateStatus::Unsolved;
    m.diagnostic = text(o["diagnostic"]).toStdString();
    if (!o["definition"].isNull())
    {
        const auto d = object(o["definition"]);
        fields(d, {"groups", "parameters", "motion"});
        m.definition = AssemblyMate::Definition{};
        auto& def = *m.definition;
        def.parameters = parameters(d["parameters"]);
        validateMateParameters(m.kind, def.parameters);
        int referenceCount = 0;
        for (const auto& value : array(d["groups"], 8))
        {
            const auto g = object(value);
            fields(g, {"role", "references"});
            AssemblyMate::Group group;
            group.role = text(g["role"], 64).toStdString();
            for (const auto& referenceValue : array(g["references"], 128))
                group.references.push_back(reference(referenceValue));
            referenceCount += int(group.references.size());
            def.groups.push_back(std::move(group));
        }
        require(referenceCount <= 512, "Mate has too many references.");
        const auto expected = mateRoles(m.kind, def.parameters.hinge.limits);
        QSet<QString> roles;
        for (const auto& group : def.groups)
        {
            require(!roles.contains(QString::fromStdString(group.role)),
                    "Duplicate mate reference role.");
            roles.insert(QString::fromStdString(group.role));
            const auto found = std::find_if(expected.begin(), expected.end(), [&](const auto& role)
                                            { return role.key == group.role; });
            require(found != expected.end() && int(group.references.size()) >= found->minimum &&
                        int(group.references.size()) <= found->maximum,
                    "Unknown role or invalid mate selection count.");
        }
        for (const auto& role : expected)
            if (role.minimum > 0)
                require(roles.contains(QString::fromStdString(role.key)),
                        "Missing required mate selection role.");
        const auto motion = object(d["motion"]);
        fields(motion, {"initialized", "anchors", "coordinates"});
        def.motion.initialized = boolean(motion["initialized"]);
        for (const auto& t : array(motion["anchors"], 512))
            def.motion.anchors.push_back(matrix(t));
        for (const auto& v : array(motion["coordinates"], 512))
            def.motion.coordinates.push_back(number(v));
        require(def.motion.initialized
                    ? def.motion.anchors.size() == std::size_t(referenceCount)
                    : def.motion.anchors.empty() && def.motion.coordinates.empty(),
                "Invalid mate motion anchor state.");
    }
    else
        require(int(m.kind) <= 3, "This mate kind requires a typed definition.");
    return m;
}
} // namespace thecad::document
