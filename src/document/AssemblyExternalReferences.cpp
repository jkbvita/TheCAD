#include "document/AssemblyDocument.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <QDir>
#include <QFileInfo>
#include <gp_Pln.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
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
using namespace modeling;

void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
        .compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), Qt::CaseInsensitive) == 0;
}

ExternalReferenceKind externalKind(ReferenceSource source)
{
    switch (source)
    {
    case ReferenceSource::Origin: return ExternalReferenceKind::Origin;
    case ReferenceSource::BasePlane: return ExternalReferenceKind::BasePlane;
    case ReferenceSource::BaseAxis: return ExternalReferenceKind::BaseAxis;
    case ReferenceSource::Datum: return ExternalReferenceKind::Datum;
    case ReferenceSource::SketchPoint: return ExternalReferenceKind::SketchPoint;
    case ReferenceSource::SketchLine: return ExternalReferenceKind::SketchLine;
    default: throw std::runtime_error("Only datum and stable sketch geometry can be referenced across parts.");
    }
}

GeometryReference sourceReference(const ExternalSketchReference& link)
{
    GeometryReference reference;
    switch (link.sourceKind)
    {
    case ExternalReferenceKind::Origin: reference.source = ReferenceSource::Origin; break;
    case ExternalReferenceKind::BasePlane: reference.source = ReferenceSource::BasePlane; break;
    case ExternalReferenceKind::BaseAxis: reference.source = ReferenceSource::BaseAxis; break;
    case ExternalReferenceKind::Datum: reference.source = ReferenceSource::Datum; break;
    case ExternalReferenceKind::SketchPoint: reference.source = ReferenceSource::SketchPoint; break;
    case ExternalReferenceKind::SketchLine: reference.source = ReferenceSource::SketchLine; break;
    }
    reference.featureId = link.sourceFeatureId;
    reference.entityId = link.sourceEntityId;
    reference.role = link.role;
    return reference;
}

const AssemblyComponent* findComponent(const AssemblyDocument& document, const std::string& id)
{
    const auto found = std::find_if(document.components().begin(), document.components().end(),
                                    [&](const auto& component) { return component.id == id; });
    return found == document.components().end() ? nullptr : &*found;
}

std::pair<gp_Pnt, gp_Pnt> sketchLinePoints(const PartDocument& part,
                                          const GeometryReference& reference)
{
    const auto feature = std::find_if(part.features().begin(), part.features().end(),
                                      [&](const auto& item) {
                                          return item.id == reference.featureId &&
                                                 item.kind == FeatureKind::Sketch;
                                      });
    require(feature != part.features().end(), "The source sketch is missing.");
    const auto entity = std::find_if(feature->sketch.entities.begin(), feature->sketch.entities.end(),
                                     [&](const auto& item) { return item.id == reference.entityId; });
    require(entity != feature->sketch.entities.end() && entity->kind == EntityKind::Line,
            "The source sketch line is missing or changed type.");
    return {sketchPoint(feature->sketch, entity->first), sketchPoint(feature->sketch, entity->second)};
}

Point2 sketchCoordinates(const gp_Ax3& frame, const gp_Pnt& position)
{
    const gp_Vec delta(frame.Location(), position);
    return {delta.Dot(gp_Vec(frame.XDirection())), delta.Dot(gp_Vec(frame.YDirection()))};
}

double sketchExtent(const Sketch& sketch)
{
    double extent = 50;
    const auto include = [&](Point2 point, double& value) {
        value = std::max(value, std::hypot(point.x, point.y));
    };
    for (const auto& entity : sketch.entities)
    {
        include(entity.first, extent);
        include(entity.second, extent);
    }
    return std::max(50.0, extent * 2.0 + 10.0);
}

SketchEntity projectedPoint(const Sketch& target, gp_Pnt point, const gp_Trsf& sourceToTarget)
{
    point.Transform(sourceToTarget);
    SketchEntity result;
    result.kind = EntityKind::Point;
    result.first = sketchCoordinates(sketchAxes(target), point);
    result.construction = true;
    return result;
}

SketchEntity projectedSegment(const Sketch& target, gp_Pnt first, gp_Pnt second,
                              const gp_Trsf& sourceToTarget)
{
    first.Transform(sourceToTarget);
    second.Transform(sourceToTarget);
    const auto frame = sketchAxes(target);
    SketchEntity result;
    result.kind = EntityKind::Line;
    result.first = sketchCoordinates(frame, first);
    result.second = sketchCoordinates(frame, second);
    require(std::hypot(result.second.x - result.first.x, result.second.y - result.first.y) > 1e-8,
            "The selected line is normal to the sketch plane and has no distinct 2D projection.");
    result.construction = true;
    return result;
}

SketchEntity projectedInfiniteLine(const Sketch& target, const gp_Pnt& sourcePoint,
                                   const gp_Dir& sourceDirection, const gp_Trsf& sourceToTarget)
{
    auto point = sourcePoint;
    point.Transform(sourceToTarget);
    auto direction = sourceDirection;
    direction.Transform(sourceToTarget);
    const auto frame = sketchAxes(target);
    const gp_Vec localDirection(direction);
    Point2 projectedDirection{localDirection.Dot(gp_Vec(frame.XDirection())),
                              localDirection.Dot(gp_Vec(frame.YDirection()))};
    const double length = std::hypot(projectedDirection.x, projectedDirection.y);
    if (length <= 1e-9)
        return projectedPoint(target, sourcePoint, sourceToTarget);
    const auto center = sketchCoordinates(frame, point);
    projectedDirection.x /= length;
    projectedDirection.y /= length;
    const double extent = sketchExtent(target);
    SketchEntity result;
    result.kind = EntityKind::Line;
    result.first = {center.x - projectedDirection.x * extent,
                    center.y - projectedDirection.y * extent};
    result.second = {center.x + projectedDirection.x * extent,
                     center.y + projectedDirection.y * extent};
    result.construction = true;
    return result;
}

gp_Pnt planeIntersectionPoint(const gp_Pln& first, const gp_Pln& second,
                              gp_Dir& direction)
{
    const gp_Vec n1(first.Axis().Direction()), n2(second.Axis().Direction());
    const gp_Vec cross = n1.Crossed(n2);
    const double magnitude2 = cross.SquareMagnitude();
    require(magnitude2 > 1e-16,
            "The selected reference plane is parallel to the sketch plane and has no unique intersection.");
    const double d1 = n1.Dot(gp_Vec(gp_Pnt(0, 0, 0), first.Location()));
    const double d2 = n2.Dot(gp_Vec(gp_Pnt(0, 0, 0), second.Location()));
    const gp_Vec point = (n2.Crossed(cross) * d1 + cross.Crossed(n1) * d2) / magnitude2;
    direction = gp_Dir(cross);
    return gp_Pnt(0, 0, 0).Translated(point);
}

SketchEntity projectGeometry(const Sketch& target, const PartDocument& sourcePart,
                             const GeometryReference& reference,
                             const AssemblyComponent& sourceComponent,
                             const AssemblyComponent& targetComponent)
{
    const auto geometry = sourcePart.resolveCurrentReference(reference);
    gp_Trsf sourceToTarget = sourceComponent.placement;
    auto targetInverse = targetComponent.placement;
    targetInverse.Invert();
    sourceToTarget.PreMultiply(targetInverse);

    if (reference.source == ReferenceSource::SketchLine)
    {
        const auto [first, second] = sketchLinePoints(sourcePart, reference);
        return projectedSegment(target, first, second, sourceToTarget);
    }
    if (geometry.kind == DatumKind::Point)
        return projectedPoint(target, geometry.frame.Location(), sourceToTarget);
    if (geometry.kind == DatumKind::Axis)
        return projectedInfiniteLine(target, geometry.frame.Location(), geometry.frame.Direction(),
                                     sourceToTarget);

    gp_Ax3 sourceFrame = geometry.frame;
    sourceFrame.Transform(sourceToTarget);
    const auto targetFrame = sketchAxes(target);
    gp_Dir lineDirection(0, 0, 1);
    const auto intersection = planeIntersectionPoint(
        gp_Pln(targetFrame.Location(), targetFrame.Direction()),
        gp_Pln(sourceFrame.Location(), sourceFrame.Direction()), lineDirection);
    const auto extent = sketchExtent(target);
    const gp_Vec half(lineDirection.X() * extent, lineDirection.Y() * extent,
                      lineDirection.Z() * extent);
    const auto first = intersection.Translated(-half);
    const auto second = intersection.Translated(half);
    return projectedSegment(target, first, second, {});
}

bool closeEnough(double a, double b)
{
    return std::abs(a - b) <= 1e-9 * std::max({1.0, std::abs(a), std::abs(b)});
}

bool sameProjectedGeometry(const SketchEntity& a, const SketchEntity& b)
{
    if (a.kind != b.kind) return false;
    if (a.kind == EntityKind::Point)
        return closeEnough(a.first.x, b.first.x) && closeEnough(a.first.y, b.first.y);
    return (closeEnough(a.first.x, b.first.x) && closeEnough(a.first.y, b.first.y) &&
            closeEnough(a.second.x, b.second.x) && closeEnough(a.second.y, b.second.y)) ||
           (closeEnough(a.first.x, b.second.x) && closeEnough(a.first.y, b.second.y) &&
            closeEnough(a.second.x, b.first.x) && closeEnough(a.second.y, b.first.y));
}

ExternalReferenceDiagnostic diagnosticFor(const std::string& target, const Feature& feature,
                                          const ExternalSketchReference& link,
                                          ExternalReferenceState state, std::string message)
{
    return {target, feature.id, link.entityId, link.componentId, link.sourceFeatureId,
            link.sourceEntityId, state, std::move(message)};
}

std::vector<const ExternalSketchReference*> referencesIn(const PartDocument& part)
{
    std::vector<const ExternalSketchReference*> result;
    for (const auto& feature : part.features())
        if (feature.kind == FeatureKind::Sketch)
            for (const auto& link : feature.sketch.externalReferences)
                if (link.associative) result.push_back(&link);
    return result;
}
}

std::vector<ExternalSketchReferenceOption> AssemblyDocument::externalSketchReferenceOptions(
    const std::string& targetComponentId, const std::string& sketchFeatureId) const
{
    return externalSketchReferenceOptions(targetComponentId, *component(targetComponentId).part,
                                          sketchFeatureId);
}

std::vector<ExternalSketchReferenceOption> AssemblyDocument::externalSketchReferenceOptions(
    const std::string& targetComponentId, const PartDocument& targetPart,
    const std::string& sketchFeatureId) const
{
    const auto& target = component(targetComponentId);
    require(target.embeddedPart.isEmpty() && !target.path.isEmpty(),
            "Edit In Place external references require a linked .thepart component.");
    const auto targetFeature = std::find_if(targetPart.features().begin(), targetPart.features().end(),
                                            [&](const auto& item) {
                                                return item.id == sketchFeatureId && item.kind == FeatureKind::Sketch;
                                            });
    require(targetFeature != targetPart.features().end(), "Select an available sketch first.");

    std::vector<ExternalSketchReferenceOption> result;
    for (const auto& source : m_components)
    {
        if (source.id == targetComponentId || !source.visible || !source.embeddedPart.isEmpty() || source.path.isEmpty())
            continue;
        for (const auto& option : referenceOptions(source.id))
        {
            if (option.reference.geometry.source != ReferenceSource::Origin &&
                option.reference.geometry.source != ReferenceSource::BasePlane &&
                option.reference.geometry.source != ReferenceSource::BaseAxis &&
                option.reference.geometry.source != ReferenceSource::Datum &&
                option.reference.geometry.source != ReferenceSource::SketchPoint &&
                option.reference.geometry.source != ReferenceSource::SketchLine)
                continue;
            try
            {
                ExternalSketchReference link;
                link.assemblyPath = m_ownerPath.toStdString();
                link.componentId = source.id;
                link.sourcePartPath = QDir::cleanPath(QFileInfo(source.path).absoluteFilePath()).toStdString();
                link.sourceDigest = source.digest.toStdString();
                link.sourceKind = externalKind(option.reference.geometry.source);
                link.sourceFeatureId = option.reference.geometry.featureId;
                link.sourceEntityId = option.reference.geometry.entityId;
                link.role = option.reference.geometry.role;
                const auto transformSourceToTarget = [&] {
                    gp_Trsf transform = source.placement;
                    auto inverse = target.placement;
                    inverse.Invert();
                    transform.PreMultiply(inverse);
                    return transform;
                }();
                TopoDS_Shape shape = option.shape;
                if (!shape.IsNull())
                {
                    BRepBuilderAPI_Transform transformed(shape, transformSourceToTarget, Standard_True);
                    require(transformed.IsDone(), "Could not transform the selected source reference.");
                    shape = transformed.Shape();
                }
                auto projected = projectGeometry(targetFeature->sketch, *source.part,
                                                 option.reference.geometry, source, target);
                const auto id = source.id + ":" + std::to_string(int(link.sourceKind)) + ":" +
                    link.sourceFeatureId + ":" + link.sourceEntityId + ":" +
                    std::to_string(int(link.role));
                result.push_back({id, option.label.toStdString(), std::move(link),
                                  std::move(projected), shape});
            }
            catch (const Standard_Failure&) {}
            catch (const std::exception&) {}
        }
    }
    return result;
}

bool AssemblyDocument::validateExternalReferenceDependencyGraph(
    const std::string& targetComponentId, const PartDocument* candidatePart,
    std::string* output) const
{
    std::map<std::string, std::vector<std::string>> dependencies;
    for (const auto& target : m_components)
    {
        const auto& targetPart = candidatePart && target.id == targetComponentId
            ? *candidatePart : *target.part;
        for (const auto* link : referencesIn(targetPart))
        {
            if (m_ownerPath.isEmpty() || !samePath(QString::fromStdString(link->assemblyPath), m_ownerPath))
            {
                if (output) *output = "External references are bound to another assembly file. Recreate the reference in this assembly.";
                return false;
            }
            const auto* source = findComponent(*this, link->componentId);
            if (!source)
            {
                if (output) *output = "The source component " + link->componentId + " is missing from this assembly.";
                return false;
            }
            if (source->id == target.id)
            {
                if (output) *output = "A part cannot reference itself through an assembly instance.";
                return false;
            }
            if (!source->embeddedPart.isEmpty() || !samePath(source->path, QString::fromStdString(link->sourcePartPath)))
            {
                if (output) *output = "The source component now points to a different part file. Re-select the intended source.";
                return false;
            }
            dependencies[target.id].push_back(source->id);
        }
    }
    std::map<std::string, int> visits;
    std::function<bool(const std::string&)> visit = [&](const auto& id)
    {
        if (visits[id] == 1) return false;
        if (visits[id] == 2) return true;
        visits[id] = 1;
        for (const auto& dependency : dependencies[id])
            if (!visit(dependency)) return false;
        visits[id] = 2;
        return true;
    };
    for (const auto& [id, sourceList] : dependencies)
    {
        (void)sourceList;
        if (!visit(id))
        {
            if (output) *output = "This reference would create a circular cross-part dependency.";
            return false;
        }
    }
    if (output) output->clear();
    return true;
}

bool AssemblyDocument::validateExternalReferenceGraph(const std::string& targetComponentId,
                                                        const PartDocument& candidatePart,
                                                        std::string* diagnostic) const
{
    return validateExternalReferenceDependencyGraph(targetComponentId, &candidatePart, diagnostic);
}

bool AssemblyDocument::updateExternalReferences(const std::string& targetComponentId)
{
    auto& target = *std::find_if(m_components.begin(), m_components.end(), [&](const auto& item) {
        return item.id == targetComponentId;
    });
    if (!target.embeddedPart.isEmpty() || target.path.isEmpty()) return false;
    std::map<std::string, std::vector<modeling::ExternalSketchProjectionUpdate>> updates;
    std::vector<ExternalReferenceDiagnostic> diagnostics;
    bool any = false;
    for (const auto& feature : target.part->features())
    {
        if (feature.kind != FeatureKind::Sketch) continue;
        auto& featureUpdates = updates[feature.id];
        for (const auto& link : feature.sketch.externalReferences)
        {
            if (!link.associative) continue;
            try
            {
                require(samePath(QString::fromStdString(link.assemblyPath), m_ownerPath),
                        "This reference belongs to a different assembly file.");
                const auto* source = findComponent(*this, link.componentId);
                require(source != nullptr, "The referenced source component is missing.");
                require(source->id != target.id, "A part cannot reference itself through an assembly instance.");
                require(source->embeddedPart.isEmpty() && samePath(source->path,
                            QString::fromStdString(link.sourcePartPath)),
                        "The source instance now points to a different part file.");
                const auto reference = sourceReference(link);
                auto projected = projectGeometry(feature.sketch, *source->part, reference, *source, target);
                const auto existing = std::find_if(feature.sketch.entities.begin(), feature.sketch.entities.end(),
                                                   [&](const auto& item) { return item.id == link.entityId; });
                require(existing != feature.sketch.entities.end(), "The dependent sketch entity is missing.");
                any |= !sameProjectedGeometry(*existing, projected) ||
                       link.sourceDigest != source->digest.toStdString();
                projected.id = link.entityId;
                projected.construction = true;
                featureUpdates.push_back({link.entityId, std::move(projected), source->digest.toStdString()});
                diagnostics.push_back(diagnosticFor(target.id, feature, link,
                    ExternalReferenceState::Valid, "Associative source is current."));
            }
            catch (const Standard_Failure& failure)
            {
                diagnostics.push_back(diagnosticFor(target.id, feature, link,
                    ExternalReferenceState::InvalidReference, failure.GetMessageString()));
            }
            catch (const std::exception& failure)
            {
                const std::string message = failure.what() ? failure.what() : "External reference resolution failed.";
                const bool missingSource =
                    message == "This reference belongs to a different assembly file." ||
                    message == "The referenced source component is missing." ||
                    message == "The source instance now points to a different part file.";
                diagnostics.push_back(diagnosticFor(target.id, feature, link,
                    missingSource ? ExternalReferenceState::MissingSource : ExternalReferenceState::InvalidReference,
                    message));
            }
        }
    }
    if (std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
            return item.state != ExternalReferenceState::Valid;
        }))
    {
        m_externalReferenceDiagnostics.insert(m_externalReferenceDiagnostics.end(),
                                               diagnostics.begin(), diagnostics.end());
        return false;
    }
    if (!any) return false;

    auto updatedPart = std::make_shared<PartDocument>(*target.part);
    try
    {
        updatedPart->updateExternalSketchReferences(updates);
    }
    catch (const Standard_Failure& failure)
    {
        for (auto& diagnostic : diagnostics)
        {
            diagnostic.state = ExternalReferenceState::FailedRegeneration;
            diagnostic.message = "Could not regenerate dependent feature " + diagnostic.dependentFeatureId +
                                 ": " + failure.GetMessageString();
        }
        m_externalReferenceDiagnostics.insert(m_externalReferenceDiagnostics.end(),
                                               diagnostics.begin(), diagnostics.end());
        return false;
    }
    catch (const std::exception& failure)
    {
        for (auto& diagnostic : diagnostics)
        {
            diagnostic.state = ExternalReferenceState::FailedRegeneration;
            diagnostic.message = "Could not regenerate dependent feature " + diagnostic.dependentFeatureId +
                                 ": " + failure.what();
        }
        m_externalReferenceDiagnostics.insert(m_externalReferenceDiagnostics.end(),
                                               diagnostics.begin(), diagnostics.end());
        return false;
    }
    target.part = std::move(updatedPart);
    prepareSource(target);
    m_externalReferenceDiagnostics.insert(m_externalReferenceDiagnostics.end(),
                                           diagnostics.begin(), diagnostics.end());
    return true;
}

std::vector<ExternalReferenceDiagnostic> AssemblyDocument::externalReferenceDiagnostics() const
{
    std::vector<ExternalReferenceDiagnostic> result = m_externalReferenceDiagnostics;
    for (const auto& target : m_components)
    {
        for (const auto& feature : target.part->features())
        {
            if (feature.kind != FeatureKind::Sketch) continue;
            for (const auto& link : feature.sketch.externalReferences)
            {
                const auto existing = std::find_if(result.begin(), result.end(), [&](const auto& item) {
                    return item.dependentComponentId == target.id && item.dependentFeatureId == feature.id &&
                           item.projectedEntityId == link.entityId;
                });
                if (existing != result.end()) continue;
                if (!link.associative)
                {
                    result.push_back(diagnosticFor(target.id, feature, link, ExternalReferenceState::Valid,
                        "Non-associative copy. Changes to the source do not update this geometry."));
                    continue;
                }
                if (!samePath(QString::fromStdString(link.assemblyPath), m_ownerPath))
                {
                    result.push_back(diagnosticFor(target.id, feature, link, ExternalReferenceState::MissingSource,
                        "The reference belongs to another or unsaved assembly context."));
                    continue;
                }
                const auto* source = findComponent(*this, link.componentId);
                if (!source)
                {
                    const auto samePathCount = std::count_if(m_components.begin(), m_components.end(),
                        [&](const auto& candidate) { return samePath(candidate.path, QString::fromStdString(link.sourcePartPath)); });
                    result.push_back(diagnosticFor(target.id, feature, link,
                        samePathCount > 1 ? ExternalReferenceState::AmbiguousReference
                                          : ExternalReferenceState::MissingSource,
                        samePathCount > 1 ? "More than one instance uses the source part; the original instance ID is missing."
                                          : "The source component is missing from the assembly."));
                    continue;
                }
                if (source->embeddedPart.isEmpty() &&
                    !samePath(source->path, QString::fromStdString(link.sourcePartPath)))
                {
                    result.push_back(diagnosticFor(target.id, feature, link, ExternalReferenceState::InvalidReference,
                        "The source component now points to a different part file."));
                    continue;
                }
                try
                {
                    const auto reference = sourceReference(link);
                    (void)source->part->resolveCurrentReference(reference);
                    const auto projected = projectGeometry(feature.sketch, *source->part, reference, *source, target);
                    const auto entity = std::find_if(feature.sketch.entities.begin(), feature.sketch.entities.end(),
                        [&](const auto& item) { return item.id == link.entityId; });
                    if (entity == feature.sketch.entities.end())
                        throw std::runtime_error("The dependent sketch entity is missing.");
                    const bool stale = link.sourceDigest != source->digest.toStdString() ||
                                       !sameProjectedGeometry(*entity, projected);
                    result.push_back(diagnosticFor(target.id, feature, link,
                        stale ? ExternalReferenceState::OutOfDate : ExternalReferenceState::Valid,
                        stale ? "The source geometry or instance placement changed. Refresh references to regenerate dependent features."
                              : "Associative source is current."));
                }
                catch (const Standard_Failure& failure)
                {
                    result.push_back(diagnosticFor(target.id, feature, link, ExternalReferenceState::InvalidReference,
                                                   failure.GetMessageString()));
                }
                catch (const std::exception& failure)
                {
                    result.push_back(diagnosticFor(target.id, feature, link, ExternalReferenceState::InvalidReference,
                                                   failure.what()));
                }
            }
        }
    }
    return result;
}
} // namespace thecad::document
