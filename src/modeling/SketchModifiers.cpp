#include "modeling/SketchModifiers.h"
#include "modeling/ConstraintSolver.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace thecad::modeling
{
namespace
{
constexpr double radians = 3.14159265358979323846 / 180;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool near(Point2 a, Point2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y) <= 1e-6;
}

bool sameGeometry(const SketchEntity& a, const SketchEntity& b)
{
    if (a.kind != b.kind || a.construction != b.construction)
        return false;
    if (a.kind == EntityKind::Line)
        return (near(a.first, b.first) && near(a.second, b.second)) ||
               (near(a.first, b.second) && near(a.second, b.first));
    return near(a.first, b.first) &&
           (a.kind != EntityKind::Circle || std::abs(a.radius - b.radius) <= 1e-6);
}

const SketchEntity& entity(const Sketch& sketch, const std::string& id)
{
    const auto found = std::find_if(sketch.entities.begin(), sketch.entities.end(),
                                    [&](const auto& item) { return item.id == id; });
    require(found != sketch.entities.end(), "A sketch modifier references missing geometry.");
    return *found;
}

std::string nextId(const Sketch& sketch, const char* prefix)
{
    for (std::size_t i = sketch.entities.size() + 1;; ++i)
    {
        auto id = prefix + std::to_string(i);
        if (std::none_of(sketch.entities.begin(), sketch.entities.end(),
                         [&](const auto& item) { return item.id == id; }))
            return id;
    }
}

bool supported(EntityKind kind)
{
    return kind == EntityKind::Line || kind == EntityKind::Circle || kind == EntityKind::Point;
}
}

const Sketch::Projection* sketchProjection(const Sketch& sketch, const std::string& entityId)
{
    const auto found = std::find_if(sketch.projections.begin(), sketch.projections.end(),
                                    [&](const auto& link) { return link.entityId == entityId; });
    return found == sketch.projections.end() ? nullptr : &*found;
}

const ExternalSketchReference* externalSketchProjection(const Sketch& sketch,
                                                         const std::string& entityId)
{
    const auto found = std::find_if(sketch.externalReferences.begin(), sketch.externalReferences.end(),
                                    [&](const auto& link) { return link.entityId == entityId; });
    return found == sketch.externalReferences.end() ? nullptr : &*found;
}

bool isSketchProjection(const Sketch& sketch, const std::string& entityId)
{
    return sketchProjection(sketch, entityId) || externalSketchProjection(sketch, entityId);
}

std::string useSketchProjection(Sketch& sketch, const SketchProjection& option, bool construction)
{
    for (const auto& link : sketch.projections)
        if (link.sourceFeature == option.link.sourceFeature && link.edgeName == option.link.edgeName)
        {
            if (!construction)
                for (auto& item : sketch.entities)
                    if (item.id == link.entityId)
                        item.construction = false;
            return link.entityId;
        }
    require(sketch.entities.size() < 1000, "The sketch already has the maximum 1000 entities.");
    auto link = option.link;
    link.entityId = nextId(sketch, "projected-");
    auto item = option.geometry;
    item.id = link.entityId;
    item.construction = construction;
    sketch.entities.push_back(item);
    sketch.projections.push_back(link);
    return link.entityId;
}

std::string useExternalSketchProjection(Sketch& sketch, SketchEntity geometry,
                                        ExternalSketchReference reference)
{
    for (const auto& link : sketch.externalReferences)
        if (link.associative == reference.associative && link.componentId == reference.componentId &&
            link.sourceKind == reference.sourceKind && link.sourceFeatureId == reference.sourceFeatureId &&
            link.sourceEntityId == reference.sourceEntityId && link.role == reference.role)
            return link.entityId;
    require(sketch.entities.size() < 1000, "The sketch already has the maximum 1000 entities.");
    geometry.id = nextId(sketch, "external-");
    geometry.construction = true;
    reference.entityId = geometry.id;
    sketch.entities.push_back(std::move(geometry));
    sketch.externalReferences.push_back(std::move(reference));
    return sketch.entities.back().id;
}

bool breakExternalSketchProjection(Sketch& sketch, const std::string& entityId)
{
    const auto oldSize = sketch.externalReferences.size();
    sketch.externalReferences.erase(
        std::remove_if(sketch.externalReferences.begin(), sketch.externalReferences.end(),
                       [&](const auto& link) { return link.entityId == entityId; }),
        sketch.externalReferences.end());
    return sketch.externalReferences.size() != oldSize;
}

void updateExternalSketchProjections(
    Sketch& sketch, const std::vector<ExternalSketchProjectionUpdate>& updates)
{
    auto candidate = sketch;
    std::set<std::string> seen;
    for (const auto& update : updates)
    {
        require(seen.insert(update.entityId).second,
                "An external sketch entity was updated more than once.");
        auto link = std::find_if(candidate.externalReferences.begin(), candidate.externalReferences.end(),
                                 [&](const auto& item) { return item.entityId == update.entityId; });
        require(link != candidate.externalReferences.end() && link->associative,
                "The associative sketch reference is unavailable.");
        auto entity = std::find_if(candidate.entities.begin(), candidate.entities.end(),
                                   [&](const auto& item) { return item.id == update.entityId; });
        require(entity != candidate.entities.end() && entity->kind == update.geometry.kind,
                "The source geometry changed type. Reselect the intended reference.");
        auto replacement = update.geometry;
        replacement.id = entity->id;
        replacement.construction = true;
        *entity = std::move(replacement);
        link->sourceDigest = update.sourceDigest;
    }
    validateSketchModifiers(candidate);
    sketch = std::move(candidate);
}

void validateSketchModifiers(const Sketch& sketch)
{
    std::set<std::string> derived;
    std::set<std::pair<std::string, std::string>> links;
    for (const auto& link : sketch.projections)
    {
        const auto& item = entity(sketch, link.entityId);
        require((item.kind == EntityKind::Line || item.kind == EntityKind::Circle) &&
                    !link.sourceFeature.empty() && !link.edgeName.empty() &&
                    link.edgeName.size() <= 8192 && link.startVertex.size() <= 8192 &&
                    derived.insert(link.entityId).second &&
                    links.emplace(link.sourceFeature, link.edgeName).second,
                "Invalid or duplicate projected sketch geometry.");
    }
    for (const auto& link : sketch.externalReferences)
    {
        const auto& item = entity(sketch, link.entityId);
        const auto validId = [](const std::string& value) {
            return !value.empty() && value.size() <= 128 &&
                std::all_of(value.begin(), value.end(), [](unsigned char c) {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_';
                });
        };
        const bool builtIn = link.sourceKind == ExternalReferenceKind::Origin ||
                             link.sourceKind == ExternalReferenceKind::BasePlane ||
                             link.sourceKind == ExternalReferenceKind::BaseAxis;
        const bool sketchPoint = link.sourceKind == ExternalReferenceKind::SketchPoint;
        const bool sketchLine = link.sourceKind == ExternalReferenceKind::SketchLine;
        const bool datum = link.sourceKind == ExternalReferenceKind::Datum;
        const int kind = int(link.sourceKind);
        require(kind >= int(ExternalReferenceKind::Origin) && kind <= int(ExternalReferenceKind::SketchLine) &&
                    (item.kind == EntityKind::Point || item.kind == EntityKind::Line) && item.construction &&
                    validId(link.entityId) && derived.insert(link.entityId).second &&
                    link.assemblyPath.size() <= 4096 && link.sourcePartPath.size() <= 4096 &&
                    (!link.associative || (!link.assemblyPath.empty() && validId(link.componentId))) &&
                    (builtIn ? link.sourceFeatureId.empty() && link.sourceEntityId.empty()
                             : validId(link.sourceFeatureId)) &&
                    ((sketchPoint || sketchLine) ? validId(link.sourceEntityId) : link.sourceEntityId.empty()) &&
                    (!sketchLine || link.role == PointRole::First) &&
                    (link.sourceDigest.empty() ||
                     (link.sourceDigest.size() == 64 && std::all_of(link.sourceDigest.begin(), link.sourceDigest.end(),
                         [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))) &&
                    (datum || sketchPoint || sketchLine || builtIn),
                "Invalid or duplicate external sketch reference.");
    }
    std::map<std::string, std::size_t> targets;
    for (std::size_t i = 0; i < sketch.replications.size(); ++i)
    {
        const auto& copy = sketch.replications[i];
        require(derived.insert(copy.target).second, "A sketch entity has more than one derivation.");
        targets.emplace(copy.target, i);
    }
    for (std::size_t i = 0; i < sketch.replications.size(); ++i)
    {
        const auto& copy = sketch.replications[i];
        const auto& source = entity(sketch, copy.source);
        const auto& target = entity(sketch, copy.target);
        const auto earlier = [&](const std::string& id) {
            const auto found = targets.find(id);
            return found == targets.end() || found->second < i;
        };
        require(supported(source.kind) && source.kind == target.kind && earlier(copy.source) &&
                    std::isfinite(copy.angle) && std::abs(copy.angle) <= 6.284 &&
                    std::isfinite(copy.offset.x) && std::isfinite(copy.offset.y) &&
                    std::abs(copy.offset.x) <= 1e9 && std::abs(copy.offset.y) <= 1e9,
                "Invalid or cyclic sketch copy reference.");
        if (!copy.mirrorLine.empty())
            require(entity(sketch, copy.mirrorLine).kind == EntityKind::Line &&
                        copy.mirrorLine != copy.source && earlier(copy.mirrorLine),
                    "Mirror requires a distinct earlier sketch line.");
    }
}

Sketch replicateSketchEntities(const Sketch& sketch, const std::vector<std::string>& seeds,
                               const SketchPattern& parameters)
{
    require(!seeds.empty(), "Select one or more sketch entities.");
    require(std::set<std::string>(seeds.begin(), seeds.end()).size() == seeds.size(),
            "Select distinct sketch entities.");
    const bool mirror = parameters.kind == SketchModifier::Mirror;
    const bool linear = parameters.kind == SketchModifier::LinearPattern;
    const bool circular = parameters.kind == SketchModifier::CircularPattern;
    require(mirror || linear || circular, "Invalid sketch copy operation.");
    const int count = mirror ? 2 : parameters.count;
    require(count >= 2 && count <= 100, "Enter 2 to 100 instances, including the seed.");
    require(std::isfinite(parameters.angle) && std::abs(parameters.angle) <= 360,
            "Enter a valid angle in degrees.");
    require(!linear || (std::isfinite(parameters.spacing) && parameters.spacing > 1e-6),
            "Enter a positive linear spacing.");
    require(!circular || (parameters.angle > 1e-6 && std::isfinite(parameters.center.x) &&
                          std::isfinite(parameters.center.y)), "Enter a valid circular span and center.");
    const auto* axis = mirror ? &entity(sketch, parameters.mirrorLine) : nullptr;
    require(!axis || axis->kind == EntityKind::Line, "Select a sketch line as the mirror axis.");
    auto result = sketch;
    for (int i = 1; i < count; ++i)
    {
        Sketch::Replication copy;
        const double direction = parameters.reverse ? -1 : 1;
        if (linear)
        {
            const double heading = parameters.angle * radians;
            copy.offset = {direction * i * parameters.spacing * std::cos(heading),
                           direction * i * parameters.spacing * std::sin(heading)};
        }
        if (circular)
        {
            const int intervals = parameters.angle == 360 ? count : count - 1;
            copy.angle = direction * i * parameters.angle * radians / intervals;
            const auto [x, y] = parameters.center;
            const double c = std::cos(copy.angle), s = std::sin(copy.angle);
            copy.offset = {x - c * x + s * y, y - s * x - c * y};
        }
        if (axis)
            copy.mirrorLine = axis->id;
        const auto transform = [&](Point2 point) {
            if (axis)
            {
                const double dx = axis->second.x - axis->first.x, dy = axis->second.y - axis->first.y;
                const double t = ((point.x - axis->first.x) * dx + (point.y - axis->first.y) * dy) /
                                 (dx * dx + dy * dy);
                return Point2{2 * (axis->first.x + t * dx) - point.x,
                              2 * (axis->first.y + t * dy) - point.y};
            }
            const double c = std::cos(copy.angle), s = std::sin(copy.angle);
            return Point2{c * point.x - s * point.y + copy.offset.x,
                          s * point.x + c * point.y + copy.offset.y};
        };
        for (const auto& seed : seeds)
        {
            if (axis && seed == axis->id)
                continue;
            auto item = entity(sketch, seed);
            require(supported(item.kind), "Select lines, circles, or sketch points to copy.");
            item.first = transform(item.first);
            if (item.kind == EntityKind::Line)
                item.second = transform(item.second);
            if (std::any_of(result.entities.begin(), result.entities.end(),
                            [&](const auto& existing) { return sameGeometry(existing, item); }))
                continue;
            require(result.entities.size() < 1000, "The pattern exceeds the 1000-entity sketch limit.");
            item.id = nextId(result, "copy-");
            copy.source = seed;
            copy.target = item.id;
            result.entities.push_back(item);
            result.replications.push_back(copy);
        }
    }
    require(result.entities.size() != sketch.entities.size(), "The copies overlap existing geometry.");
    return requireSolvedSketch(solvePlanarSketch(result));
}
}
