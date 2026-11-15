#include "document/AssemblyDocument.h"
#include "document/storage/PartFileStore.h"
#include "document/storage/AssemblyMateData.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
namespace thecad::document
{
namespace
{
using namespace modeling::assembly;
void require(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error(message);
}
std::string uuid(const char* prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
bool identifier(const std::string& s)
{
    return !s.empty() && s.size() <= 128 &&
           std::all_of(s.begin(), s.end(),
                       [](unsigned char c)
                       {
                           return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                  (c >= '0' && c <= '9') || c == '-' || c == '_';
                       });
}
gp_Trsf placement(const gp_Ax3& frame)
{
    gp_Trsf t;
    t.SetDisplacement(gp_Ax3(), frame);
    return t;
}
bool samePose(const gp_Trsf& a, const gp_Trsf& b)
{
    for (int r = 1; r <= 3; ++r)
        for (int c = 1; c <= 4; ++c)
            if (std::abs(a.Value(r, c) - b.Value(r, c)) > 1e-10)
                return false;
    return true;
}
MateGeometry world(const AssemblyDocument& doc, const AssemblyReference& ref)
{
    auto g = doc.mateGeometry(ref);
    if (!ref.componentId.empty())
        g.frame.Transform(doc.component(ref.componentId).placement);
    return g;
}
AssemblyMate generatedMate(const std::string& id, const std::string& name, MateKind kind,
                           std::vector<AssemblyMate::Group> groups)
{
    AssemblyMate m;
    m.id = id;
    m.name = name;
    m.kind = kind;
    m.definition = AssemblyMate::Definition{};
    m.definition->groups = std::move(groups);
    std::vector<AssemblyReference> refs;
    for (const auto& group : m.definition->groups)
        refs.insert(refs.end(), group.references.begin(), group.references.end());
    if (refs.size() >= 2)
    {
        m.first = refs[0];
        m.second = refs[1];
    }
    return m;
}
void put(std::vector<AssemblyMate>& mates, AssemblyMate mate, bool reset = false)
{
    const auto old =
        std::find_if(mates.begin(), mates.end(), [&](const auto& m) { return m.id == mate.id; });
    if (old == mates.end())
    {
        mates.push_back(std::move(mate));
        return;
    }
    mate.status = old->status;
    mate.diagnostic = old->diagnostic;
    if (!reset && old->definition && old->kind == mate.kind)
    {
        auto before = writeMateDefinition(*old->definition),
             after = writeMateDefinition(*mate.definition);
        if (before["groups"] == after["groups"] &&
            old->definition->parameters.belt.engaged == mate.definition->parameters.belt.engaged)
            mate.definition->motion = old->definition->motion;
    }
    *old = std::move(mate);
}
bool follows(const std::vector<AssemblyMate>& mates, const std::string& id)
{
    const auto old =
        std::find_if(mates.begin(), mates.end(), [&](const auto& m) { return m.id == id; });
    return old == mates.end() || old->status == MateStatus::Active;
}
} // namespace
modeling::assembly::MateGeometry
AssemblyDocument::connectorGeometry(const AssemblyReference& ref) const
{
    require(!ref.componentId.empty() && !ref.connectorId.empty(),
            "Choose a published component connector.");
    const auto asset = std::find_if(m_assets.begin(), m_assets.end(), [&](const auto& a)
                                    { return a.componentId == ref.componentId; });
    require(asset != m_assets.end(), "The component no longer has published references.");
    const auto connector = std::find_if(asset->connectors.begin(), asset->connectors.end(),
                                        [&](const auto& c) { return c.id == ref.connectorId; });
    require(connector != asset->connectors.end(),
            "The published connector is missing. Replace its mate reference explicitly.");
    const auto point = mateGeometry({ref.componentId, connector->point}),
               direction = mateGeometry({ref.componentId, connector->direction});
    gp_Pnt location = point.frame.Location();
    if (point.kind == MateGeometryKind::Line)
    {
        require(!point.curve.IsNull() && std::isfinite(point.first) && std::isfinite(point.last),
                "A connector midpoint requires a finite straight edge.");
        location = point.curve->Value((point.first + point.last) / 2);
    }
    else
        require(point.kind == MateGeometryKind::Point || point.kind == MateGeometryKind::Frame ||
                    point.kind == MateGeometryKind::Circle,
                "Select a vertex, sketch point, origin, circular edge center or straight-edge "
                "midpoint for the connection point.");
    require(mateGeometryDirection(direction),
            "Select a plane, planar face or axis for the connection direction.");
    gp_Dir up = direction.frame.XDirection();
    if (asset->groundPlane)
    {
        const auto ground = mateGeometry({ref.componentId, *asset->groundPlane});
        gp_Vec projected(ground.frame.Direction());
        projected -= gp_Vec(direction.frame.Direction()) *
                     projected.Dot(gp_Vec(direction.frame.Direction()));
        if (projected.SquareMagnitude() > 1e-12)
            up = gp_Dir(projected);
    }
    const auto frame = gp_Ax3(location, direction.frame.Direction(), up);
    MateGeometry result;
    result.kind = MateGeometryKind::Frame;
    result.frame = gp_Ax3();
    result.frame.Transform(placement(frame) * connector->offset);
    return result;
}
void AssemblyDocument::publishAsset(PublishedAsset asset)
{
    (void)component(asset.componentId);
    require(asset.connectors.size() <= 64, "An asset supports at most 64 connectors.");
    auto candidate = *this;
    std::set<std::string> ids;
    for (auto& connector : asset.connectors)
    {
        if (connector.id.empty())
            connector.id = uuid("connector-");
        if (connector.name.empty())
            connector.name = "Connector" + std::to_string(ids.size() + 1);
        require(identifier(connector.id) && ids.insert(connector.id).second &&
                    !connector.name.empty() && connector.name.size() <= 256,
                "Invalid or duplicate published connector.");
        validateRigidPlacement(connector.offset);
    }
    if (asset.groundPlane)
        require(candidate.mateGeometry({asset.componentId, *asset.groundPlane}).kind ==
                    MateGeometryKind::Plane,
                "An asset ground reference must be planar.");
    const auto old =
        std::find_if(candidate.m_assets.begin(), candidate.m_assets.end(),
                     [&](const auto& a) { return a.componentId == asset.componentId; });
    if (old == candidate.m_assets.end())
        candidate.m_assets.push_back(asset);
    else
        *old = asset;
    for (const auto& connector : asset.connectors)
        (void)candidate.connectorGeometry({asset.componentId, {}, connector.id});
    if (!asset.groundPlane || !asset.grounded)
        candidate.m_mates.erase(
            std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(), [&](const auto& m)
                           { return m.id == "mate-ground-" + asset.componentId; }),
            candidate.m_mates.end());
    candidate.updateGroundMates();
    candidate.solve();
    *this = std::move(candidate);
}
void AssemblyDocument::setGroundPlane(std::optional<AssemblyReference> plane)
{
    auto candidate = *this;
    if (plane)
        require(candidate.mateGeometry(*plane).kind == MateGeometryKind::Plane,
                "The assembly ground plane must be a plane or planar face.");
    candidate.m_groundPlane = std::move(plane);
    if (!candidate.m_groundPlane)
        candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                               [](const auto& mate)
                                               { return mate.id.rfind("mate-ground-", 0) == 0; }),
                                candidate.m_mates.end());
    candidate.updateGroundMates();
    candidate.solve();
    *this = std::move(candidate);
}
void AssemblyDocument::updateGroundMates()
{
    if (!m_groundPlane)
        return;
    require(mateGeometry(*m_groundPlane).kind == MateGeometryKind::Plane,
            "The assembly ground plane is unavailable or no longer planar.");
    for (const auto& asset : m_assets)
        if (asset.groundPlane && asset.grounded && asset.componentId != m_groundPlane->componentId)
        {
            AssemblyReference support{asset.componentId, *asset.groundPlane};
            auto m = generatedMate(
                "mate-ground-" + asset.componentId, "Ground " + component(asset.componentId).name,
                MateKind::Coincident, {{"first", {*m_groundPlane}}, {"second", {support}}});
            const auto a = world(*this, *m_groundPlane), b = world(*this, support);
            m.alignment = a.frame.Direction().Dot(b.frame.Direction()) < 0 ? -1 : 1;
            const auto old = std::find_if(m_mates.begin(), m_mates.end(),
                                          [&](const auto& value) { return value.id == m.id; });
            if (old != m_mates.end())
                m.alignment = old->alignment;
            put(m_mates, std::move(m));
        }
}
modeling::assembly::BeltGeometry AssemblyDocument::beltGeometry(const AssemblyBelt& belt) const
{
    require(belt.pulleys.size() >= 2 && belt.pulleys.size() <= 64,
            "Select 2 to 64 pulley components in route order.");
    require(belt.diameters.size() == belt.pulleys.size() &&
                belt.sides.size() == belt.pulleys.size(),
            "Each belt pulley requires its diameter and routing side.");
    require(std::isfinite(belt.length) && belt.length > 0 && belt.length <= 1e9 &&
                std::isfinite(belt.thickness) && belt.thickness > 0 && belt.thickness <= 1e9,
            "Belt length and thickness must be positive finite values.");
    std::vector<BeltPulley> pulleys;
    for (std::size_t i = 0; i < belt.pulleys.size(); ++i)
    {
        const auto g = world(*this, belt.pulleys[i]);
        require(g.kind == MateGeometryKind::Line || g.kind == MateGeometryKind::Cylinder ||
                    g.kind == MateGeometryKind::Circle,
                "Belt members must be axes, circular edges or cylindrical faces.");
        const double diameter = belt.diameters[i];
        require(std::isfinite(diameter) && diameter >= 0 && diameter <= 1e9,
                "Pulley diameter must be finite and nonnegative; zero uses the measured value.");
        const double radius = diameter == 0 ? g.radius : diameter / 2;
        require(radius > 1e-6, "A selected axis requires a positive pulley diameter override.");
        pulleys.push_back(
            {g.frame, radius + (belt.useThickness ? belt.thickness / 2 : 0), belt.sides[i]});
    }
    double offset = 0;
    if (belt.location)
    {
        const auto g = world(*this, *belt.location);
        require(g.kind == MateGeometryKind::Point || g.kind == MateGeometryKind::Frame ||
                    g.kind == MateGeometryKind::Plane,
                "Belt location requires a vertex, point, origin, plane or planar face.");
        if (g.kind == MateGeometryKind::Plane)
            require(g.frame.Direction().IsParallel(pulleys[0].frame.Direction(), 1e-6),
                    "The belt location plane must be normal to the pulley axes.");
        offset = gp_Vec(pulleys[0].frame.Location(), g.frame.Location())
                     .Dot(gp_Vec(pulleys[0].frame.Direction()));
    }
    return modeling::assembly::beltGeometry(pulleys, belt.useThickness ? belt.thickness : 0,
                                            offset);
}
std::string AssemblyDocument::setBelt(AssemblyBelt belt)
{
    auto candidate = *this;
    if (belt.id.empty())
        belt.id = uuid("belt-");
    require(identifier(belt.id), "Invalid belt identifier.");
    const auto old = std::find_if(candidate.m_belts.begin(), candidate.m_belts.end(),
                                  [&](const auto& b) { return b.id == belt.id; });
    if (belt.name.empty())
        belt.name = "Belt" + std::to_string(old == candidate.m_belts.end()
                                                ? candidate.m_belts.size() + 1
                                                : std::size_t(old - candidate.m_belts.begin()) + 1);
    require(belt.name.size() <= 256, "Belt name is too long.");
    if (belt.diameters.empty())
        belt.diameters.assign(belt.pulleys.size(), 0);
    if (belt.sides.empty())
        belt.sides.assign(belt.pulleys.size(), 1);
    (void)candidate.beltGeometry(belt);
    if (old == candidate.m_belts.end())
        require(belt.componentId.empty(),
                "A new belt cannot take ownership of an existing component.");
    else
        require(belt.componentId == old->componentId,
                "The generated belt component cannot be reassigned.");
    if (old != candidate.m_belts.end() && !belt.createPart && !belt.componentId.empty())
    {
        const auto dependent = candidate.componentDependents({belt.componentId});
        require(std::all_of(dependent.begin(), dependent.end(),
                            [&](const auto& m) { return m.id == "mate-lock-" + belt.id; }),
                "The generated belt part has dependent mates. Remove those explicitly before "
                "disabling part creation.");
        candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                               [&](const auto& m)
                                               { return m.id == "mate-lock-" + belt.id; }),
                                candidate.m_mates.end());
        candidate.m_components.erase(std::remove_if(candidate.m_components.begin(),
                                                    candidate.m_components.end(), [&](const auto& c)
                                                    { return c.id == belt.componentId; }),
                                     candidate.m_components.end());
        belt.componentId.clear();
    }
    if (belt.createPart && belt.componentId.empty())
    {
        const auto layout = candidate.beltGeometry(belt);
        auto part = PartDocument::createEmpty();
        part.addSketch(layout.centerline);
        auto component = ownedSource(PartFileStore::encode(part));
        component.id = uuid("component-");
        component.name = belt.name + " Part";
        component.placement = placement(layout.frame);
        belt.componentId = component.id;
        candidate.m_components.push_back(std::move(component));
    }
    const bool relock = old != candidate.m_belts.end() && !old->lockPart && belt.lockPart;
    if (old == candidate.m_belts.end())
        candidate.m_belts.push_back(belt);
    else
        *old = belt;
    for (auto& m : candidate.m_mates)
        if (m.id == "mate-rotation-" + belt.id || m.id == "mate-length-" + belt.id)
        {
            const auto status = (m.id == "mate-rotation-" + belt.id ? belt.engaged : belt.driving)
                                    ? MateStatus::Active
                                    : MateStatus::Suppressed;
            if (m.status != status && m.definition)
                m.definition->motion = {};
            m.status = status;
        }
    for (auto& mate : candidate.m_mates)
        if (mate.id == "mate-lock-" + belt.id)
        {
            if (!belt.lockPart)
                mate.status = MateStatus::Suppressed;
            else if (relock)
            {
                mate.status = MateStatus::Active;
                if (mate.definition)
                    mate.definition->motion = {};
            }
        }
    candidate.solve();
    *this = std::move(candidate);
    return belt.id;
}
std::string AssemblyDocument::createInPlacePart(const PartDocument& part,
                                                const AssemblyReference& support,
                                                const gp_Trsf& offset)
{
    return setInPlacePart({{}, {}, support, offset}, part);
}
std::string AssemblyDocument::setInPlacePart(InPlacePart definition, const PartDocument& part)
{
    validateRigidPlacement(definition.offset);
    if (!definition.linked)
        definition.support = {};
    const auto support = world(*this, definition.support);
    require(support.kind == MateGeometryKind::Plane || support.kind == MateGeometryKind::Frame,
            "Create an in-context part on a plane, planar face or origin.");
    auto candidate = *this;
    if (definition.id.empty())
        definition.id = uuid("inplace-");
    require(identifier(definition.id), "Invalid in-context feature identifier.");
    const auto old = std::find_if(candidate.m_inPlaces.begin(), candidate.m_inPlaces.end(),
                                  [&](const auto& p) { return p.id == definition.id; });
    const auto data = PartFileStore::encode(part);
    if (old == candidate.m_inPlaces.end())
    {
        require(definition.componentId.empty(),
                "A new in-context feature cannot own an existing component.");
        auto component = ownedSource(data);
        component.id = uuid("component-");
        component.name = "In-context Part" + std::to_string(candidate.m_inPlaces.size() + 1);
        component.placement = placement(support.frame) * definition.offset;
        definition.componentId = component.id;
        candidate.m_components.push_back(std::move(component));
        candidate.m_inPlaces.push_back(definition);
    }
    else
    {
        require(definition.componentId == old->componentId,
                "The in-context component cannot be reassigned.");
        if (old->linked && !definition.linked)
            definition.offset = candidate.component(old->componentId).placement;
        auto& component =
            *std::find_if(candidate.m_components.begin(), candidate.m_components.end(),
                          [&](const auto& c) { return c.id == old->componentId; });
        require(!component.embeddedPart.isEmpty(),
                "The in-context component must be owned by this assembly.");
        if (data != component.embeddedPart)
        {
            auto replacement = ownedSource(data);
            replacement.id = component.id;
            replacement.name = component.name;
            replacement.placement = component.placement;
            replacement.fixed = component.fixed;
            replacement.visible = component.visible;
            component = std::move(replacement);
        }
        if (!definition.linked)
            component.placement = definition.offset;
        *old = definition;
    }
    if (!definition.linked)
        candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                               [&](const auto& m)
                                               { return m.id == "mate-inplace-" + definition.id; }),
                                candidate.m_mates.end());
    candidate.solve();
    *this = std::move(candidate);
    return definition.componentId;
}
void AssemblyDocument::updateOwnedPart(const std::string& id, const PartDocument& part)
{
    const auto& old = component(id);
    require(!old.embeddedPart.isEmpty(),
            "This component links an external part; edit its source document.");
    require(std::none_of(m_belts.begin(), m_belts.end(),
                         [&](const auto& b) { return b.componentId == id; }),
            "Edit the Belt/Chain feature to update its generated sketch.");
    auto candidate = *this;
    auto replacement = ownedSource(PartFileStore::encode(part));
    replacement.id = old.id;
    replacement.name = old.name;
    replacement.placement = old.placement;
    replacement.fixed = old.fixed;
    replacement.visible = old.visible;
    for (auto& c : candidate.m_components)
        if (c.id == id)
            c = std::move(replacement);
    candidate.solve();
    *this = std::move(candidate);
}
bool AssemblyDocument::updateGeneratedParts()
{
    require(m_assets.size() <= 64 && m_belts.size() <= 64 && m_inPlaces.size() <= 64,
            "Too many generated assembly workflows.");
    bool changed = false;
    std::set<std::string> assetOwners;
    for (const auto& asset : m_assets)
    {
        (void)component(asset.componentId);
        require(assetOwners.insert(asset.componentId).second && asset.connectors.size() <= 64,
                "Duplicate asset owner or too many connectors.");
        std::set<std::string> ids;
        for (const auto& connector : asset.connectors)
        {
            require(identifier(connector.id) && ids.insert(connector.id).second &&
                        !connector.name.empty() && connector.name.size() <= 256,
                    "Invalid or duplicate published connector.");
            validateRigidPlacement(connector.offset);
            (void)connectorGeometry({asset.componentId, {}, connector.id});
        }
        if (asset.groundPlane)
            require(mateGeometry({asset.componentId, *asset.groundPlane}).kind ==
                        MateGeometryKind::Plane,
                    "The published ground plane is unavailable.");
    }
    updateGroundMates();
    std::set<std::string> workflowIds;
    std::map<std::string, std::vector<std::string>> dependencies;
    for (const auto& belt : m_belts)
    {
        require(identifier(belt.id) && workflowIds.insert(belt.id).second && !belt.name.empty() &&
                    belt.name.size() <= 256,
                "Invalid or duplicate Belt/Chain feature.");
        require(belt.createPart ? !belt.componentId.empty() : belt.componentId.empty(),
                "Generated belt part ownership is inconsistent.");
        if (belt.createPart)
        {
            require(!component(belt.componentId).embeddedPart.isEmpty() &&
                        !dependencies.count(belt.componentId),
                    "A generated belt must own its component exclusively.");
            auto& refs = dependencies[belt.componentId];
            for (const auto& ref : belt.pulleys)
                if (!ref.componentId.empty())
                    refs.push_back(ref.componentId);
            if (belt.location && !belt.location->componentId.empty())
                refs.push_back(belt.location->componentId);
        }
    }
    for (const auto& part : m_inPlaces)
    {
        require(identifier(part.id) && workflowIds.insert(part.id).second &&
                    !component(part.componentId).embeddedPart.isEmpty() &&
                    !dependencies.count(part.componentId),
                "Invalid or duplicate in-context component ownership.");
        validateRigidPlacement(part.offset);
        dependencies[part.componentId] = {};
        if (part.linked && !part.support.componentId.empty())
            dependencies[part.componentId].push_back(part.support.componentId);
    }
    for (const auto& c : m_components)
        require(c.embeddedPart.isEmpty() || dependencies.count(c.id),
                "An owned component is missing its generating workflow.");
    std::map<std::string, int> visits;
    std::vector<std::string> order;
    std::function<void(const std::string&)> visit = [&](const auto& id)
    {
        (void)component(id);
        if (!dependencies.count(id))
            return;
        require(visits[id] != 1, "Generated component references contain a cycle.");
        if (visits[id] == 2)
            return;
        visits[id] = 1;
        for (const auto& dependency : dependencies[id])
            visit(dependency);
        visits[id] = 2;
        order.push_back(id);
    };
    for (const auto& [id, refs] : dependencies)
        visit(id);
    const auto updateBelt = [&](AssemblyBelt& belt)
    {
        for (const auto& mate : m_mates)
            if (mate.id == "mate-rotation-" + belt.id)
                belt.engaged = mate.status == MateStatus::Active;
            else if (mate.id == "mate-length-" + belt.id)
                belt.driving = mate.status == MateStatus::Active;
        const auto layout = beltGeometry(belt);
        if (!belt.driving)
            belt.length = layout.length;
        if (belt.engaged || std::any_of(m_mates.begin(), m_mates.end(), [&](const auto& m)
                                        { return m.id == "mate-rotation-" + belt.id; }))
        {
            auto rotation = generatedMate("mate-rotation-" + belt.id, belt.name + " rotation",
                                          MateKind::Belt, {{"pulleys", belt.pulleys}});
            rotation.definition->parameters.belt = {
                true,           false,     belt.length, belt.useThickness ? belt.thickness : 0,
                belt.diameters, belt.sides};
            rotation.status = belt.engaged ? MateStatus::Active : MateStatus::Suppressed;
            put(m_mates, std::move(rotation));
        }
        if (belt.driving || std::any_of(m_mates.begin(), m_mates.end(), [&](const auto& m)
                                        { return m.id == "mate-length-" + belt.id; }))
        {
            auto length = generatedMate("mate-length-" + belt.id, belt.name + " length",
                                        MateKind::Belt, {{"pulleys", belt.pulleys}});
            length.definition->parameters.belt = {
                false,          true,      belt.length, belt.useThickness ? belt.thickness : 0,
                belt.diameters, belt.sides};
            length.status = belt.driving ? MateStatus::Active : MateStatus::Suppressed;
            put(m_mates, std::move(length));
        }
        if (!belt.createPart)
            return;
        auto sketch = layout.centerline;
        const auto snap = [](double value)
        { return std::abs(value) < 1e8 ? std::round(value * 1e10) / 1e10 : value; };
        for (auto& e : sketch.entities)
        {
            for (auto* p : {&e.first, &e.second, &e.center})
            {
                p->x = snap(p->x);
                p->y = snap(p->y);
            }
            e.radius = snap(e.radius);
        }
        auto& component = *std::find_if(m_components.begin(), m_components.end(),
                                        [&](const auto& c) { return c.id == belt.componentId; });
        const auto lockId = "mate-lock-" + belt.id;
        const auto target = placement(layout.frame);
        const bool move =
            belt.lockPart && follows(m_mates, lockId) && !samePose(component.placement, target);
        QByteArray layoutKey;
        QDataStream stream(&layoutKey, QIODevice::WriteOnly);
        for (const auto& e : sketch.entities)
            stream << int(e.kind) << e.first.x << e.first.y << e.second.x << e.second.y
                   << e.center.x << e.center.y << e.radius << e.clockwise;
        if (m_beltLayoutKeys[belt.id] != layoutKey)
        {
            auto part = PartDocument::createEmpty();
            part.addSketch(sketch);
            const auto data = PartFileStore::encode(part);
            if (data != component.embeddedPart)
            {
                auto updated = ownedSource(data);
                updated.id = component.id;
                updated.name = component.name;
                updated.placement = component.placement;
                updated.fixed = component.fixed;
                updated.visible = component.visible;
                component = std::move(updated);
                changed = true;
            }
            m_beltLayoutKeys[belt.id] = layoutKey;
        }
        if (move)
        {
            require(!component.fixed,
                    "Unfix the generated component before moving its generating references.");
            component.placement = target;
            changed = true;
        }
        if (belt.lockPart || std::any_of(m_mates.begin(), m_mates.end(),
                                         [&](const auto& m) { return m.id == lockId; }))
            put(m_mates,
                generatedMate(lockId, belt.name + " Lock To Sketch", MateKind::LockToSketch,
                              {{"first", {{}}}, {"second", {{component.id, {}}}}}),
                move);
    };
    for (const auto& id : order)
    {
        const auto context = std::find_if(m_inPlaces.begin(), m_inPlaces.end(),
                                          [&](const auto& p) { return p.componentId == id; });
        if (context != m_inPlaces.end())
        {
            if (!context->linked)
                continue;
            const auto support = world(*this, context->support);
            require(support.kind == MateGeometryKind::Plane ||
                        support.kind == MateGeometryKind::Frame,
                    "The in-context support is unavailable or no longer planar.");
            const auto target = placement(support.frame) * context->offset;
            auto& component = *std::find_if(m_components.begin(), m_components.end(),
                                            [&](const auto& c) { return c.id == id; });
            const auto mateId = "mate-inplace-" + context->id;
            const bool move = follows(m_mates, mateId) && !samePose(component.placement, target);
            if (move)
            {
                require(!component.fixed,
                        "Unfix the generated component before moving its generating references.");
                component.placement = target;
                changed = true;
            }
            put(m_mates,
                generatedMate(mateId, component.name + " In Place", MateKind::InPlace,
                              {{"first", {context->support}}, {"second", {{id, {}}}}}),
                move);
        }
        else
        {
            auto belt = std::find_if(m_belts.begin(), m_belts.end(),
                                     [&](const auto& b) { return b.componentId == id; });
            updateBelt(*belt);
        }
    }
    for (auto& belt : m_belts)
        if (!belt.createPart)
            updateBelt(belt);
    return changed;
}

bool AssemblyDocument::snapMagnetic(const std::string& id, double distance, int movingConnector,
                                    int stationaryConnector, bool locked)
{
    require(std::isfinite(distance) && distance > 0 && distance <= 1e9,
            "Magnetic snap distance must be positive and finite.");
    const auto& moving = component(id);
    if (moving.fixed)
        return false;
    const auto asset = std::find_if(m_assets.begin(), m_assets.end(),
                                    [&](const auto& a) { return a.componentId == id; });
    if (asset == m_assets.end() || asset->connectors.empty())
        return false;
    struct Target
    {
        AssemblyReference moving, stationary;
        double distance;
    };
    std::vector<Target> targets;
    int stationaryIndex = 0;
    for (const auto& other : m_assets)
        if (other.componentId != id)
            for (const auto& fixed : other.connectors)
            {
                const int index = stationaryIndex++;
                if (stationaryConnector >= 0 && index != stationaryConnector)
                    continue;
                const AssemblyReference stationary{other.componentId, {}, fixed.id};
                const auto at = world(*this, stationary).frame.Location();
                for (std::size_t i = 0; i < asset->connectors.size(); ++i)
                {
                    if (movingConnector >= 0 && int(i) != movingConnector)
                        continue;
                    const AssemblyReference selected{id, {}, asset->connectors[i].id};
                    const double gap = world(*this, selected).frame.Location().Distance(at);
                    if (gap <= distance)
                        targets.push_back({selected, stationary, gap});
                }
            }
    std::stable_sort(targets.begin(), targets.end(),
                     [](const auto& a, const auto& b) { return a.distance < b.distance; });
    for (const auto& target : targets)
    {
        const auto key = QByteArray::fromStdString(id + "|" + target.moving.connectorId);
        const auto mateId =
            "magnetic-" +
            QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().toStdString();
        const auto previous = std::find_if(m_mates.begin(), m_mates.end(),
                                           [&](const auto& m) { return m.id == mateId; });
        bool lockedState = locked;
        if (previous != m_mates.end() && previous->definition &&
            previous->definition->parameters.magnetic.locked)
        {
            if (previous->first.componentId != target.stationary.componentId ||
                previous->first.connectorId != target.stationary.connectorId)
                continue;
            lockedState = true;
        }
        auto candidate = *this;
        for (auto& mate : candidate.m_mates)
            if (mate.kind == MateKind::Magnetic && mate.status == MateStatus::Active &&
                mate.definition && !mate.definition->parameters.magnetic.locked)
            {
                bool involves = false;
                for (const auto& group : mate.definition->groups)
                    for (const auto& ref : group.references)
                        involves |= ref.componentId == id;
                if (involves)
                {
                    mate.status = MateStatus::Suppressed;
                    mate.diagnostic =
                        "Disconnected unlocked magnetic connection during repositioning.";
                }
            }
        const auto local = candidate.connectorGeometry(target.moving).frame,
                   stationary = world(candidate, target.stationary).frame;
        gp_Ax3 goal(stationary.Location(), stationary.Direction().Reversed(),
                    stationary.XDirection());
        gp_Trsf pose;
        pose.SetDisplacement(local, goal);
        for (auto& c : candidate.m_components)
            if (c.id == id)
                c.placement = pose;
        auto mate =
            generatedMate(mateId, "Magnetic " + moving.name.substr(0, 220), MateKind::Magnetic,
                          {{"first", {target.stationary}}, {"second", {target.moving}}});
        mate.definition->parameters.magnetic.locked = lockedState;
        try
        {
            candidate.setMate(mate);
            *this = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        { /* A nearby connector may conflict with other active mates. Try the next. */
        }
    }
    return false;
}

std::vector<std::string>
AssemblyDocument::componentDependencyNames(const std::vector<std::string>& ids) const
{
    std::set<std::string> selected(ids.begin(), ids.end()), features;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& belt : m_belts)
        {
            bool used = belt.location && selected.count(belt.location->componentId);
            for (const auto& ref : belt.pulleys)
                used |= selected.count(ref.componentId) > 0;
            if (used)
            {
                features.insert(belt.name);
                if (!belt.componentId.empty() && selected.insert(belt.componentId).second)
                {
                    features.insert(component(belt.componentId).name + " (generated belt part)");
                    changed = true;
                }
            }
        }
        for (const auto& part : m_inPlaces)
            if (part.linked && selected.count(part.support.componentId))
            {
                features.insert(component(part.componentId).name + " (in-context dependency)");
                if (selected.insert(part.componentId).second)
                    changed = true;
            }
    }
    if (m_groundPlane && selected.count(m_groundPlane->componentId))
        features.insert("Assembly Ground Plane");
    for (const auto& mate :
         componentDependents(std::vector<std::string>(selected.begin(), selected.end())))
        features.insert(mate.name);
    return {features.begin(), features.end()};
}
void AssemblyDocument::deleteBelt(const std::string& id, bool includeDependents)
{
    const auto old =
        std::find_if(m_belts.begin(), m_belts.end(), [&](const auto& b) { return b.id == id; });
    require(old != m_belts.end(), "The Belt/Chain feature is unavailable.");
    auto candidate = *this;
    if (!old->componentId.empty())
    {
        auto dependencies = candidate.componentDependents({old->componentId});
        dependencies.erase(std::remove_if(dependencies.begin(), dependencies.end(),
                                          [&](const auto& m) { return m.id == "mate-lock-" + id; }),
                           dependencies.end());
        auto names = candidate.componentDependencyNames({old->componentId});
        names.erase(std::remove(names.begin(), names.end(), old->name + " Lock To Sketch"),
                    names.end());
        require(includeDependents || (dependencies.empty() && names.empty()),
                "The generated belt part has dependent mates or features. Confirm their deletion "
                "explicitly.");
        candidate.deleteComponents({old->componentId}, true);
    }
    candidate.m_belts.erase(std::remove_if(candidate.m_belts.begin(), candidate.m_belts.end(),
                                           [&](const auto& b) { return b.id == id; }),
                            candidate.m_belts.end());
    candidate.m_beltLayoutKeys.erase(id);
    candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                           [&](const auto& m)
                                           {
                                               return m.id == "mate-rotation-" + id ||
                                                      m.id == "mate-length-" + id ||
                                                      m.id == "mate-lock-" + id;
                                           }),
                            candidate.m_mates.end());
    candidate.solve();
    *this = std::move(candidate);
}

bool AssemblyDocument::componentMovable(const std::string& id) const
{
    const auto& selected = component(id);
    if (selected.fixed)
        return false;
    for (const auto& part : m_inPlaces)
        if (part.componentId == id && part.linked && follows(m_mates, "mate-inplace-" + part.id))
            return false;
    const auto index = std::size_t(&selected - m_components.data());
    if (index >= m_solution.fullyConstrained.size() || !m_solution.fullyConstrained[index])
        return true;
    for (const auto& mate : m_mates)
        if (mate.kind == MateKind::Magnetic && mate.status == MateStatus::Active &&
            mate.definition && !mate.definition->parameters.magnetic.locked)
            for (const auto& group : mate.definition->groups)
                for (const auto& ref : group.references)
                    if (ref.componentId == id)
                        return true;
    return false;
}

} // namespace thecad::document
