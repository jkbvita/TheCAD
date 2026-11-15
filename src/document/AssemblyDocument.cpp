#include "document/AssemblyDocument.h"
#include "document/ReferenceGeometryData.h"
#include "document/storage/AssemblyFileStore.h"
#include "document/storage/AssemblyMateData.h"
#include "document/storage/PartFileStore.h"
#include "document/storage/AssemblyWorkflowStore.h"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <map>
#include <set>
#include <cmath>
#include <stdexcept>
#include <BRepBuilderAPI_MakeVertex.hxx>

namespace thecad::document
{
namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::string uuid(const char* prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
bool member(const std::vector<std::string>& ids, const std::string& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
modeling::DatumGeometry legacyGeometry(const modeling::assembly::MateGeometry& g)
{
    using K = modeling::assembly::MateGeometryKind;
    return {g.kind == K::Plane ? modeling::DatumKind::Plane
            : modeling::assembly::mateGeometryDirection(g) && g.kind != K::Frame
                ? modeling::DatumKind::Axis
                : modeling::DatumKind::Point,
            g.frame, g.kind == K::Cylinder ? g.radius : 0};
}
std::vector<AssemblyReference> mateReferences(const AssemblyMate& mate)
{
    if (!mate.definition)
        return {mate.first, mate.second};
    std::vector<AssemblyReference> refs;
    for (const auto& group : mate.definition->groups)
        refs.insert(refs.end(), group.references.begin(), group.references.end());
    return refs;
}
bool sameReference(const modeling::GeometryReference& a, const modeling::GeometryReference& b)
{
    return a.source == b.source &&
           (!a.topologyName.empty() ? a.topologyName == b.topologyName
                                    : a.featureId == b.featureId && a.entityId == b.entityId &&
                                          a.role == b.role && a.index == b.index);
}
} // namespace
AssemblyDocument AssemblyDocument::load(const QString& path)
{
    return AssemblyFileStore::load(path);
}
void AssemblyDocument::save(const QString& path) const
{
    AssemblyFileStore::save(*this, path);
}
AssemblyComponent AssemblyDocument::loadSource(const QString& source)
{
    AssemblyComponent component;
    component.path = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    QFile file(component.path);
    require(file.open(QIODevice::ReadOnly),
            "Cannot read component part: " + component.path.toStdString());
    require(file.size() > 0 && file.size() <= 1024 * 1024,
            "The component part exceeds the supported document size limit.");
    const auto bytes = file.readAll();
    file.close();
    component.part = std::make_shared<PartDocument>(PartDocument::load(component.path));
    require(!component.part->shape().IsNull(),
            "The component part has no solid body: " + component.path.toStdString());
    require(file.open(QIODevice::ReadOnly), "Component source disappeared while loading.");
    require(file.readAll() == bytes,
            "The component source changed while loading. Retry the insertion or rebuild.");
    component.digest =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    prepareSource(component);
    return component;
}
AssemblyComponent AssemblyDocument::ownedSource(const QByteArray& data)
{
    AssemblyComponent component;
    component.embeddedPart = data;
    component.part = std::make_shared<PartDocument>(PartFileStore::decode(data));
    component.digest =
        QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    prepareSource(component);
    return component;
}
void AssemblyDocument::prepareSource(AssemblyComponent& component)
{
    if (component.part->shape().IsNull())
    {
        require(!component.embeddedPart.isEmpty(), "Linked assembly parts require a solid body.");
        const auto last =
            std::find_if(component.part->features().rbegin(), component.part->features().rend(),
                         [](const auto& f) { return f.kind == FeatureKind::Sketch; });
        require(last != component.part->features().rend(),
                "An owned component requires a solid or a generated sketch.");
        component.displayShape = modeling::sketchShape(last->sketch);
    }
    require(!component.shape().IsNull(), "The component has no displayable geometry.");
    Bnd_Box box;
    BRepBndLib::Add(component.shape(), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    component.size = std::max(
        1.0, std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0)));
    auto references = std::make_shared<std::vector<AssemblySourceReference>>();
    for (auto option : component.part->referenceOptions(false, false, false))
    {
        auto geometry = modeling::assembly::mateGeometry(
            option.geometry, option.reference.source == modeling::ReferenceSource::Origin);
        if (option.reference.source == modeling::ReferenceSource::SketchLine)
            geometry = modeling::assembly::mateGeometry(option.selectionShape);
        references->push_back(
            {option.label, option.reference, option.geometry, option.selectionShape, geometry});
    }
    for (const auto& option : component.part->namedTopologyOptions())
        try
        {
            const auto geometry = modeling::assembly::mateGeometry(option.selectionShape);
            references->push_back({option.label, option.reference, legacyGeometry(geometry),
                                   option.selectionShape, geometry});
            if (geometry.kind == modeling::assembly::MateGeometryKind::Circle)
            {
                auto ref = option.reference;
                ref.source = modeling::ReferenceSource::BodyCircleAxis;
                auto axis = geometry;
                axis.kind = modeling::assembly::MateGeometryKind::Line;
                axis.curve.Nullify();
                axis.profile = modeling::assembly::MateProfileKind::None;
                axis.radius = 0;
                references->push_back({"Axis of " + option.label,
                                       ref,
                                       {modeling::DatumKind::Axis, geometry.frame},
                                       option.selectionShape,
                                       axis});
                ref.source = modeling::ReferenceSource::BodyCircleCenter;
                auto point = geometry;
                point.kind = modeling::assembly::MateGeometryKind::Point;
                point.radius = 0;
                point.curve.Nullify();
                point.profile = modeling::assembly::MateProfileKind::None;
                point.shape = BRepBuilderAPI_MakeVertex(geometry.frame.Location());
                references->push_back({"Center of " + option.label,
                                       ref,
                                       {modeling::DatumKind::Point, geometry.frame},
                                       point.shape,
                                       point});
            }
        }
        catch (const Standard_Failure&)
        {
        }
        catch (const std::exception&)
        {
        }
    component.references = std::move(references);
}
const AssemblyComponent& AssemblyDocument::component(const std::string& id) const
{
    const auto found = std::find_if(m_components.begin(), m_components.end(),
                                    [&](const auto& c) { return c.id == id; });
    require(found != m_components.end(), "Assembly component is unavailable: " + id);
    return *found;
}
std::string AssemblyDocument::insertComponent(const QString& path, const gp_Trsf& placement,
                                              std::optional<bool> fixed)
{
    auto candidate = *this;
    AssemblyComponent added;
    const auto absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto cached =
        std::find_if(m_components.begin(), m_components.end(), [&](const auto& c)
                     { return c.path.compare(absolute, Qt::CaseInsensitive) == 0; });
    if (cached != m_components.end())
        added = *cached;
    else
        added = loadSource(absolute);
    added.id = uuid("component-");
    added.placement = placement;
    added.fixed = fixed.value_or(m_components.empty());
    added.visible = true;
    const auto base = QFileInfo(path).completeBaseName().toStdString();
    int number = 1;
    do
    {
        added.name = base + "<" + std::to_string(number++) + ">";
    } while (std::any_of(m_components.begin(), m_components.end(),
                         [&](const auto& c) { return c.name == added.name; }));
    candidate.m_components.push_back(added);
    for (const auto& asset : m_assets)
        if (component(asset.componentId).path.compare(added.path, Qt::CaseInsensitive) == 0)
        {
            auto published = asset;
            published.componentId = added.id;
            candidate.m_assets.push_back(std::move(published));
            break;
        }
    candidate.solve();
    *this = std::move(candidate);
    return added.id;
}
void AssemblyDocument::setPlacement(const std::string& id, const gp_Trsf& placement)
{
    require(!component(id).fixed, "Float the component before moving it.");
    modeling::assembly::validateRigidPlacement(placement);
    bool moved = false;
    for (int r = 1; r <= 3; ++r)
        for (int c = 1; c <= 4; ++c)
            moved |= std::abs(component(id).placement.Value(r, c) - placement.Value(r, c)) > 1e-10;
    if (!moved)
        return;
    auto candidate = *this;
    for (auto& mate : candidate.m_mates)
        if (mate.kind == AssemblyMateKind::Magnetic &&
            mate.status == modeling::assembly::MateStatus::Active && mate.definition &&
            !mate.definition->parameters.magnetic.locked)
        {
            bool involves = false;
            for (const auto& ref : mateReferences(mate))
                involves |= ref.componentId == id;
            if (involves)
            {
                mate.status = modeling::assembly::MateStatus::Suppressed;
                mate.diagnostic =
                    "Disconnected unlocked magnetic connection while moving the component.";
            }
        }
    for (auto& c : candidate.m_components)
        if (c.id == id)
            c.placement = placement;
    const auto mechanical = [](AssemblyMateKind kind)
    {
        using K = AssemblyMateKind;
        return kind == K::Gear || kind == K::RackPinion || kind == K::Screw ||
               kind == K::LinearCoupler || kind == K::UniversalJoint || kind == K::Belt;
    };
    const bool drive = std::any_of(m_inPlaces.begin(), m_inPlaces.end(), [&](const auto& part)
                                   { return part.support.componentId == id; }) ||
                       std::any_of(m_mates.begin(), m_mates.end(),
                                   [&](const auto& mate)
                                   {
                                       if (!mechanical(mate.kind) ||
                                           mate.status != modeling::assembly::MateStatus::Active)
                                           return false;
                                       for (const auto& ref : mateReferences(mate))
                                           if (ref.componentId == id)
                                               return true;
                                       return false;
                                   });
    if (drive)
    {
        auto driven = candidate;
        try
        {
            driven.solve(id);
            driven.solve();
            candidate = std::move(driven);
        }
        catch (const std::exception&)
        {
            candidate.solve();
        }
    }
    else
        candidate.solve();
    *this = std::move(candidate);
}
void AssemblyDocument::setFixed(const std::string& id, bool fixed)
{
    (void)component(id);
    auto candidate = *this;
    for (auto& c : candidate.m_components)
        if (c.id == id)
            c.fixed = fixed;
    candidate.solve();
    *this = std::move(candidate);
}
void AssemblyDocument::setVisible(const std::string& id, bool visible)
{
    (void)component(id);
    for (auto& c : m_components)
        if (c.id == id)
            c.visible = visible;
}
std::string AssemblyDocument::setMate(AssemblyMate mate)
{
    if (mate.id.empty())
        mate.id = uuid("mate-");
    if (mate.name.empty())
    {
        int n = 1;
        do
        {
            mate.name = std::string(modeling::assembly::mateName(mate.kind)) + std::to_string(n++);
        } while (std::any_of(m_mates.begin(), m_mates.end(),
                             [&](const auto& m) { return m.name == mate.name; }));
    }
    auto candidate = *this;
    const auto found = std::find_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                    [&](const auto& m) { return m.id == mate.id; });
    if (mate.definition && found != candidate.m_mates.end())
    {
        const auto before = mateReferences(*found), after = mateReferences(mate);
        bool changed =
            found->kind != mate.kind || before.size() != after.size() || !found->definition;
        for (std::size_t i = 0; !changed && i < before.size(); ++i)
            changed = before[i].componentId != after[i].componentId ||
                      before[i].connectorId != after[i].connectorId ||
                      !sameReference(before[i].geometry, after[i].geometry);
        if (!changed && mate.definition->parameters.standard.lockRotation &&
            !found->definition->parameters.standard.lockRotation)
            changed = true;
        if (changed)
            mate.definition->motion = {};
    }
    if (found == candidate.m_mates.end())
        candidate.m_mates.push_back(mate);
    else
        *found = mate;
    candidate.solve();
    *this = std::move(candidate);
    return mate.id;
}
void AssemblyDocument::deleteMates(const std::vector<std::string>& ids)
{
    require(!ids.empty(), "Select a mate to delete.");
    auto candidate = *this;
    for (const auto& id : ids)
        require(
            std::any_of(m_mates.begin(), m_mates.end(), [&](const auto& m) { return m.id == id; }),
            "Mate is unavailable: " + id);
    for (const auto& id : ids)
    {
        for (auto& belt : candidate.m_belts)
        {
            if (id == "mate-rotation-" + belt.id)
                belt.engaged = false;
            if (id == "mate-length-" + belt.id)
                belt.driving = false;
            if (id == "mate-lock-" + belt.id)
                belt.lockPart = false;
        }
        for (auto& part : candidate.m_inPlaces)
            if (id == "mate-inplace-" + part.id)
            {
                part.linked = false;
                part.support = {};
                part.offset = candidate.component(part.componentId).placement;
            }
        for (auto& asset : candidate.m_assets)
            if (id == "mate-ground-" + asset.componentId)
                asset.grounded = false;
    }
    candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                           [&](const auto& m) { return member(ids, m.id); }),
                            candidate.m_mates.end());
    candidate.solve();
    *this = std::move(candidate);
}
std::string AssemblyDocument::keepUnsolvedMate(AssemblyMate mate)
{
    try
    {
        return setMate(mate);
    }
    catch (const std::exception& error)
    {
        auto candidate = *this;
        mate.status = modeling::assembly::MateStatus::Unsolved;
        mate.diagnostic = error.what();
        const auto id = candidate.setMate(std::move(mate));
        *this = std::move(candidate);
        return id;
    }
}
std::string AssemblyDocument::forceMate(AssemblyMate mate, const std::vector<std::string>& affected)
{
    require(!affected.empty(), "Select the mates that will be marked unsolved.");
    auto candidate = *this;
    for (const auto& id : affected)
    {
        const auto found =
            std::find_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                         [&](const auto& m) { return m.id == id && m.id != mate.id; });
        require(found != candidate.m_mates.end(), "The affected mate is unavailable.");
        found->status = modeling::assembly::MateStatus::Unsolved;
        found->diagnostic =
            "Explicitly left unsolved while forcing " +
            (mate.name.empty() ? std::string(modeling::assembly::mateName(mate.kind)) : mate.name);
    }
    mate.status = modeling::assembly::MateStatus::Active;
    mate.diagnostic.clear();
    const auto id = candidate.setMate(std::move(mate));
    *this = std::move(candidate);
    return id;
}
void AssemblyDocument::setMateStatus(const std::vector<std::string>& ids,
                                     modeling::assembly::MateStatus status)
{
    require(!ids.empty() && int(status) >= 0 && int(status) <= 2,
            "Select valid mates and a valid status.");
    auto candidate = *this;
    for (const auto& id : ids)
    {
        const auto found = std::find_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                        [&](const auto& m) { return m.id == id; });
        require(found != candidate.m_mates.end(), "Mate is unavailable.");
        if (found->kind == AssemblyMateKind::Belt &&
            status == modeling::assembly::MateStatus::Active && found->status != status &&
            found->definition)
            found->definition->motion = {};
        found->status = status;
        if (status != modeling::assembly::MateStatus::Unsolved)
            found->diagnostic.clear();
    }
    candidate.solve();
    *this = std::move(candidate);
}
std::vector<AssemblyMate>
AssemblyDocument::componentDependents(const std::vector<std::string>& ids) const
{
    std::vector<AssemblyMate> result;
    for (const auto& m : m_mates)
        if (
            [&]
            {
                for (const auto& ref : mateReferences(m))
                    if (member(ids, ref.componentId))
                        return true;
                return false;
            }())
            result.push_back(m);
    return result;
}
void AssemblyDocument::deleteComponents(const std::vector<std::string>& ids, bool includeDependents)
{
    require(!ids.empty(), "Select a component to delete.");
    for (const auto& id : ids)
        (void)component(id);
    require(includeDependents || componentDependencyNames(ids).empty(),
            "The selected components have dependent mates or generated features. Confirm their "
            "removal explicitly.");
    auto candidate = *this;
    std::set<std::string> selected(ids.begin(), ids.end());
    bool expanded = true;
    while (expanded)
    {
        expanded = false;
        for (const auto& belt : candidate.m_belts)
        {
            bool used = belt.location && selected.count(belt.location->componentId);
            for (const auto& ref : belt.pulleys)
                used |= selected.count(ref.componentId) > 0;
            if (used && !belt.componentId.empty())
                expanded |= selected.insert(belt.componentId).second;
        }
        for (const auto& part : candidate.m_inPlaces)
            if (part.linked && selected.count(part.support.componentId))
                expanded |= selected.insert(part.componentId).second;
    }
    candidate.m_belts.erase(
        std::remove_if(candidate.m_belts.begin(), candidate.m_belts.end(),
                       [&](auto& belt)
                       {
                           bool used = belt.location && selected.count(belt.location->componentId);
                           for (const auto& ref : belt.pulleys)
                               used |= selected.count(ref.componentId) > 0;
                           if (used)
                           {
                               candidate.m_beltLayoutKeys.erase(belt.id);
                               return true;
                           }
                           if (selected.count(belt.componentId))
                           {
                               belt.createPart = false;
                               belt.componentId.clear();
                               candidate.m_beltLayoutKeys.erase(belt.id);
                           }
                           return false;
                       }),
        candidate.m_belts.end());
    candidate.m_inPlaces.erase(
        std::remove_if(candidate.m_inPlaces.begin(), candidate.m_inPlaces.end(),
                       [&](const auto& part)
                       {
                           return selected.count(part.componentId) ||
                                  (part.linked && selected.count(part.support.componentId));
                       }),
        candidate.m_inPlaces.end());
    candidate.m_assets.erase(std::remove_if(candidate.m_assets.begin(), candidate.m_assets.end(),
                                            [&](const auto& asset)
                                            { return selected.count(asset.componentId); }),
                             candidate.m_assets.end());
    if (candidate.m_groundPlane && selected.count(candidate.m_groundPlane->componentId))
        candidate.m_groundPlane.reset();
    candidate.m_components.erase(std::remove_if(candidate.m_components.begin(),
                                                candidate.m_components.end(), [&](const auto& c)
                                                { return selected.count(c.id); }),
                                 candidate.m_components.end());
    candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                           [&](const auto& mate)
                                           {
                                               for (const auto& ref : mateReferences(mate))
                                                   if (selected.count(ref.componentId))
                                                       return true;
                                               return false;
                                           }),
                            candidate.m_mates.end());
    candidate.solve();
    *this = std::move(candidate);
}
void AssemblyDocument::reloadSources()
{
    auto candidate = *this;
    std::map<QString, AssemblyComponent> cache;
    for (auto& c : candidate.m_components)
    {
        if (!c.embeddedPart.isEmpty())
            continue;
        auto found = cache.find(c.path.toCaseFolded());
        if (found == cache.end())
            found = cache.emplace(c.path.toCaseFolded(), loadSource(c.path)).first;
        c.part = found->second.part;
        c.references = found->second.references;
        c.digest = found->second.digest;
        c.size = found->second.size;
    }
    candidate.solve();
    *this = std::move(candidate);
}
modeling::DatumGeometry AssemblyDocument::referenceGeometry(const AssemblyReference& reference,
                                                            bool transformed) const
{
    if (reference.componentId.empty())
    {
        require(reference.geometry.source == modeling::ReferenceSource::BasePlane ||
                    reference.geometry.source == modeling::ReferenceSource::BaseAxis,
                "Select an assembly plane or axis.");
        return PartDocument::createEmpty().resolveReference(reference.geometry);
    }
    const auto& c = component(reference.componentId);
    auto geometry = c.part->resolveCurrentReference(reference.geometry);
    if (transformed)
        geometry.frame.Transform(c.placement);
    return geometry;
}
std::vector<AssemblyReferenceOption> AssemblyDocument::referenceOptions(const std::string& id) const
{
    static const auto origin = []
    {
        const auto part = PartDocument::createEmpty();
        std::vector<AssemblySourceReference> options;
        for (const auto& option : part.referenceOptions(false, false, false))
            options.push_back({option.label, option.reference, option.geometry,
                               part.currentReferenceShape(option.reference),
                               modeling::assembly::mateGeometry(
                                   option.geometry,
                                   option.reference.source == modeling::ReferenceSource::Origin)});
        return options;
    }();
    const auto* instance = id.empty() ? nullptr : &component(id);
    const auto& options = instance ? *instance->references : origin;
    const auto prefix = QString::fromStdString(instance ? instance->name : "Assembly") + " / ";
    std::vector<AssemblyReferenceOption> result;
    for (const auto& option : options)
    {
        const auto label = prefix + QString::fromStdString(option.label);
        result.push_back({label,
                          {id, option.reference},
                          option.geometry,
                          option.selectionShape,
                          option.mateGeometry});
    }
    for (const auto& asset : m_assets)
        if (asset.componentId == id)
            for (const auto& connector : asset.connectors)
            {
                AssemblyReference ref{id, {}, connector.id};
                const auto geometry = connectorGeometry(ref);
                result.push_back(
                    {prefix + "Published references / " + QString::fromStdString(connector.name),
                     ref,
                     {modeling::DatumKind::Point, geometry.frame},
                     modeling::datumDisplayShape({modeling::DatumKind::Axis, geometry.frame}, 5),
                     geometry});
            }
    return result;
}
modeling::assembly::MateGeometry AssemblyDocument::mateGeometry(const AssemblyReference& ref) const
{
    if (!ref.connectorId.empty())
        return connectorGeometry(ref);
    if (int(ref.geometry.source) >= 6)
    {
        require(!ref.geometry.topologyName.empty() &&
                    ref.geometry.role == modeling::PointRole::First &&
                    ref.geometry.entityId.empty() && ref.geometry.signature.size() == 64,
                "Body mate references require valid persistent topology names.");
        (void)component(ref.componentId).part->currentReferenceShape(ref.geometry);
    }
    if (ref.componentId.empty())
    {
        for (const auto& option : referenceOptions({}))
            if (sameReference(option.reference.geometry, ref.geometry))
                return option.mateGeometry;
    }
    else
        for (const auto& option : *component(ref.componentId).references)
            if (sameReference(option.reference, ref.geometry))
                return option.mateGeometry;
    throw std::runtime_error("The mate reference is missing or its geometry is unsupported. "
                             "Replace the reference explicitly.");
}
void AssemblyDocument::solve(const std::string& driven, int generatedPass)
{
    require(generatedPass < 8,
            "Generated assembly geometry did not converge. The previous document is retained.");
    (void)updateGeneratedParts();
    require(m_components.size() <= 64 && m_mates.size() <= 256,
            "This assembly milestone supports 64 components and 256 mates.");
    std::set<std::string> ids;
    std::vector<modeling::assembly::RigidBody> bodies;
    for (const auto& c : m_components)
    {
        require(!c.id.empty() && c.id.size() <= 128 && ids.insert(c.id).second,
                "Invalid or duplicate component ID.");
        require(!c.name.empty() && c.name.size() <= 256 && c.part && !c.shape().IsNull(),
                "Invalid component source or name.");
        bodies.push_back({c.id, c.placement, c.fixed || c.id == driven, c.size});
    }
    const auto index = [&](const std::string& id)
    {
        if (id.empty())
            return -1;
        for (std::size_t i = 0; i < m_components.size(); ++i)
            if (m_components[i].id == id)
                return int(i);
        throw std::runtime_error("Mate component is missing: " + id);
    };
    std::vector<modeling::assembly::ResolvedMate> mates;
    std::vector<std::size_t> activeMates;
    for (std::size_t mi = 0; mi < m_mates.size(); ++mi)
    {
        const auto& m = m_mates[mi];
        require(!m.id.empty() && m.id.size() <= 128 && ids.insert(m.id).second && !m.name.empty() &&
                    m.name.size() <= 256,
                "Invalid or duplicate mate ID/name.");
        try
        {
            (void)readAssemblyMate(writeAssemblyMate(m));
            require(int(m.status) >= 0 && int(m.status) <= 2, "Invalid mate status.");
            for (const auto& ref : mateReferences(m))
                (void)index(ref.componentId);
            if (m.definition)
                modeling::assembly::validateMateParameters(m.kind, m.definition->parameters);
            if (m.status != modeling::assembly::MateStatus::Active)
                continue;
            modeling::assembly::ResolvedMate resolved;
            resolved.id = m.name;
            resolved.kind = m.kind;
            resolved.value = m.value;
            resolved.alignment = m.alignment;
            if (m.definition)
            {
                resolved.definition = modeling::assembly::ResolvedMateDefinition{};
                resolved.definition->parameters = m.definition->parameters;
                resolved.definition->motion = m.definition->motion;
                for (const auto& group : m.definition->groups)
                    for (const auto& ref : group.references)
                        resolved.definition->references.push_back(
                            {group.role, index(ref.componentId), mateGeometry(ref)});
                if (resolved.definition->references.size() >= 2)
                {
                    resolved.first = resolved.definition->references[0].body;
                    resolved.second = resolved.definition->references[1].body;
                    resolved.a = legacyGeometry(resolved.definition->references[0].geometry);
                    resolved.b = legacyGeometry(resolved.definition->references[1].geometry);
                }
            }
            else
            {
                resolved.first = index(m.first.componentId);
                resolved.second = index(m.second.componentId);
                resolved.a = referenceGeometry(m.first, false);
                resolved.b = referenceGeometry(m.second, false);
            }
            mates.push_back(std::move(resolved));
            activeMates.push_back(mi);
        }
        catch (const std::exception& failure)
        {
            throw std::runtime_error("Mate " + m.name + ": " + failure.what());
        }
    }
    m_solution = modeling::assembly::solveMates(bodies, mates);
    for (std::size_t i = 0; i < m_solution.motion.size(); ++i)
        if (m_mates[activeMates[i]].definition)
            m_mates[activeMates[i]].definition->motion = m_solution.motion[i];
    for (std::size_t i = 0; i < m_components.size(); ++i)
        m_components[i].placement = m_solution.placements[i];
    for (auto& part : m_inPlaces)
        if (!part.linked)
            part.offset = component(part.componentId).placement;
    if (updateGeneratedParts())
        solve(driven, generatedPass + 1);
}
QByteArray AssemblyDocument::stateKey() const
{
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
    out << QJsonDocument(AssemblyWorkflowStore::write(*this)).toJson(QJsonDocument::Compact);
    out << quint64(m_components.size()) << quint64(m_mates.size());
    for (const auto& c : m_components)
    {
        out << QString::fromStdString(c.id) << QString::fromStdString(c.name) << c.path << c.digest
            << c.fixed << c.visible;
        for (int i = 1; i <= 3; ++i)
            for (int j = 1; j <= 4; ++j)
                out << c.placement.Value(i, j);
    }
    for (const auto& m : m_mates)
    {
        out << QJsonDocument(writeAssemblyMate(m)).toJson(QJsonDocument::Compact);
        out << QString::fromStdString(m.id) << QString::fromStdString(m.name) << int(m.kind)
            << m.value << m.alignment;
        for (const auto* ref : {&m.first, &m.second})
            out << QString::fromStdString(ref->componentId)
                << QJsonDocument(writeGeometryReference(ref->geometry))
                       .toJson(QJsonDocument::Compact);
    }
    return key;
}
} // namespace thecad::document
