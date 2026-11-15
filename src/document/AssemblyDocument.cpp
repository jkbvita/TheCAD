#include "document/AssemblyDocument.h"
#include "document/ReferenceGeometryData.h"
#include "document/storage/AssemblyFileStore.h"
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
    Bnd_Box box;
    BRepBndLib::Add(component.part->shape(), box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    component.size = std::max(
        1.0, std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0)));
    auto references = std::make_shared<std::vector<ReferenceOption>>();
    for (auto option : component.part->referenceOptions(true))
    {
        if (option.geometry.kind == modeling::DatumKind::Point)
            continue;
        if (int(option.reference.source) < 6)
            option.selectionShape = component.part->currentReferenceShape(option.reference);
        references->push_back(std::move(option));
    }
    component.references = std::move(references);
    return component;
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
    candidate.solve();
    *this = std::move(candidate);
    return added.id;
}
void AssemblyDocument::setPlacement(const std::string& id, const gp_Trsf& placement)
{
    require(!component(id).fixed, "Float the component before moving it.");
    auto candidate = *this;
    for (auto& c : candidate.m_components)
        if (c.id == id)
            c.placement = placement;
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
    candidate.m_mates.erase(std::remove_if(candidate.m_mates.begin(), candidate.m_mates.end(),
                                           [&](const auto& m) { return member(ids, m.id); }),
                            candidate.m_mates.end());
    candidate.solve();
    *this = std::move(candidate);
}
std::vector<AssemblyMate>
AssemblyDocument::componentDependents(const std::vector<std::string>& ids) const
{
    std::vector<AssemblyMate> result;
    for (const auto& m : m_mates)
        if (member(ids, m.first.componentId) || member(ids, m.second.componentId))
            result.push_back(m);
    return result;
}
void AssemblyDocument::deleteComponents(const std::vector<std::string>& ids,
                                        bool deleteDependentMates)
{
    require(!ids.empty(), "Select a component to delete.");
    for (const auto& id : ids)
        (void)component(id);
    const auto dependents = componentDependents(ids);
    require(deleteDependentMates || dependents.empty(),
            "The selected components have dependent mates. Confirm their removal explicitly.");
    auto candidate = *this;
    candidate.m_components.erase(std::remove_if(candidate.m_components.begin(),
                                                candidate.m_components.end(),
                                                [&](const auto& c) { return member(ids, c.id); }),
                                 candidate.m_components.end());
    candidate.m_mates.erase(
        std::remove_if(
            candidate.m_mates.begin(), candidate.m_mates.end(), [&](const auto& m)
            { return member(ids, m.first.componentId) || member(ids, m.second.componentId); }),
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
        auto options = part.referenceOptions();
        for (auto& option : options)
            option.selectionShape = part.currentReferenceShape(option.reference);
        return options;
    }();
    const auto* instance = id.empty() ? nullptr : &component(id);
    const auto& options = instance ? *instance->references : origin;
    const auto prefix = QString::fromStdString(instance ? instance->name : "Assembly") + " / ";
    std::vector<AssemblyReferenceOption> result;
    for (const auto& option : options)
    {
        if (id.empty() && option.reference.source != modeling::ReferenceSource::BasePlane &&
            option.reference.source != modeling::ReferenceSource::BaseAxis)
            continue;
        if (option.geometry.kind == modeling::DatumKind::Point)
            continue;
        const auto label = prefix + QString::fromStdString(option.label);
        result.push_back({label, {id, option.reference}, option.geometry, option.selectionShape});
    }
    return result;
}
void AssemblyDocument::solve()
{
    require(m_components.size() <= 64 && m_mates.size() <= 256,
            "This assembly milestone supports 64 components and 256 mates.");
    std::set<std::string> ids;
    std::vector<modeling::assembly::RigidBody> bodies;
    for (const auto& c : m_components)
    {
        require(!c.id.empty() && c.id.size() <= 128 && ids.insert(c.id).second,
                "Invalid or duplicate component ID.");
        require(!c.name.empty() && c.name.size() <= 256 && c.part && !c.part->shape().IsNull(),
                "Invalid component source or name.");
        bodies.push_back({c.id, c.placement, c.fixed, c.size});
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
    for (const auto& m : m_mates)
    {
        require(!m.id.empty() && m.id.size() <= 128 && ids.insert(m.id).second && !m.name.empty() &&
                    m.name.size() <= 256,
                "Invalid or duplicate mate ID/name.");
        try
        {
            mates.push_back({m.name, m.kind, index(m.first.componentId),
                             index(m.second.componentId), referenceGeometry(m.first, false),
                             referenceGeometry(m.second, false), m.value, m.alignment});
        }
        catch (const std::exception& failure)
        {
            throw std::runtime_error("Mate " + m.name + ": " + failure.what());
        }
    }
    m_solution = modeling::assembly::solveMates(bodies, mates);
    for (std::size_t i = 0; i < m_components.size(); ++i)
        m_components[i].placement = m_solution.placements[i];
}
QByteArray AssemblyDocument::stateKey() const
{
    QByteArray key;
    QDataStream out(&key, QIODevice::WriteOnly);
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
