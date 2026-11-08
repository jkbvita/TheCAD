#include "modeling/DimensionAnnotation.h"
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include "document/PartDocument.h"
#include "document/ReferenceGeometryData.h"
#include "document/SketchProjectionData.h"
#include "modeling/Box.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/PlanarRegions.h"
#include "modeling/PatternFeatures.h"
#include "modeling/SolidFeatures.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "document/detail/PartValidation.h"
#include "document/storage/PartFileStore.h"
namespace thecad::document {
using namespace detail;
PartDocument PartDocument::createEmpty()
{
    PartDocument result;
    result.m_parameters = {0, 0, 0};
    return result;
}

PartDocument PartDocument::createBox(BoxParameters parameters)
{
    auto result = createEmpty();
    Feature box;
    box.kind = FeatureKind::Box;
    box.id = "box-1";
    box.name = "Box1";
    box.box = parameters;
    result.apply({box});
    return result;
}

void PartDocument::apply(std::vector<Feature> features, bool validateDependencies,
                         std::size_t unchangedPrefix)
{
    if (features.size() > maximumFeatures) fail("Part exceeds the 500-feature prototype limit.");
    TopoDS_Shape body;
    BoxParameters boxParameters{0, 0, 0};
    std::string bodyId;
    std::map<std::string, modeling::Sketch> sketches;
    std::map<std::string,TopoDS_Shape> bodies;
    std::vector<std::pair<std::string, std::string>> solidHistory;
    std::map<std::string,modeling::TopologyNames> namedBodies;modeling::TopologyNames bodyNames;
    std::map<std::string,modeling::DatumGeometry> datums;
    std::set<std::string> ids;
    // An unchanged prefix has already been validated and regenerated.
    for (std::size_t i = 0; i < unchangedPrefix; ++i) {
        const auto& cached = m_features[i];
        ids.insert(cached.id);
        if (cached.kind == FeatureKind::Box) boxParameters = cached.box;
        if (cached.kind == FeatureKind::Sketch) sketches.emplace(cached.id, cached.sketch);
        if (cached.kind == FeatureKind::ReferenceGeometry) datums.emplace(cached.id, cached.datum);
        if (isSolidFeature(cached.kind)) {
            solidHistory.emplace_back(cached.id, bodyId);
            bodyId = cached.id;
            bodies.emplace(bodyId, m_bodies.at(bodyId));
            namedBodies.emplace(bodyId, m_namedBodies.at(bodyId));
        }
    }
    if (!bodyId.empty()) {
        body = bodies.at(bodyId);
        bodyNames = namedBodies.at(bodyId);
    }
    try {
        for (auto it = features.begin() + unchangedPrefix; it != features.end(); ++it) {
            auto& feature = *it;
            validId(feature.id);
            const auto name = q(feature.name);
            if (name.isEmpty() || name.size() > 128 || name.contains(QRegularExpression("[\\x00-\\x1f]")))
                fail("Feature names must contain 1 to 128 printable characters.");
            if (!ids.insert(feature.id).second) fail("Duplicate feature ID: " + q(feature.id));
            std::vector<std::string> expected;
            const auto previousBody = bodyId;
            if (feature.kind == FeatureKind::Box) {
                if (!body.IsNull()) fail("A legacy box cannot replace an existing solid in the history.");
                for (const double length : {feature.box.width, feature.box.depth, feature.box.height})
                    if (!std::isfinite(length) || length > maximumLength) fail("Invalid box dimensions.");
                body = modeling::makeBox(feature.box.width, feature.box.depth, feature.box.height,&bodyNames,feature.id);
                if (!BRepCheck_Analyzer(body).IsValid()) fail("Box parameters do not generate a valid solid.");
                bodyId = feature.id;
                boxParameters = feature.box;
            } else if(feature.kind==FeatureKind::ReferenceGeometry){
                std::vector<modeling::DatumGeometry> references;
                for(auto& ref:feature.reference.references){bindGeometryReference(ref,bodies,namedBodies);references.push_back(resolveGeometryReference(ref,datums,sketches,bodies,namedBodies));if(!ref.featureId.empty()&&std::find(expected.begin(),expected.end(),ref.featureId)==expected.end())expected.push_back(ref.featureId);}
                feature.datum=modeling::evaluateDatum(feature.reference,references);datums.emplace(feature.id,feature.datum);
            } else if (feature.kind == FeatureKind::Sketch) {
                std::set<std::string> entityIds;
                for (const auto& entity : feature.sketch.entities) {
                    if (!entity.id.empty()) {
                        validId(entity.id);
                        if (!entityIds.insert(entity.id).second) fail("Duplicate sketch entity ID.");
                    }
                }
                // A new entity must not inherit the identity of an entity removed
                // in this same edit; existing feature contours may reference it.
                // Preserve previous IDs as allocation reservations even when they
                // are absent from the candidate sketch.
                std::set<std::string> reservedIds = entityIds;
                const auto previousSketch = std::find_if(m_features.begin(), m_features.end(),
                    [&](const Feature& old) { return old.kind == FeatureKind::Sketch && old.id == feature.id; });
                if (previousSketch != m_features.end()) {
                    for (const auto& previousEntity : previousSketch->sketch.entities)
                        reservedIds.insert(previousEntity.id);
                }
                std::size_t entityNumber = 1;
                for (auto& entity : feature.sketch.entities) {
                    if (entity.id.empty()) {
                        if (validateDependencies) fail("Stored sketch entities require stable IDs.");
                        std::string candidate;
                        do { candidate = "entity-" + std::to_string(entityNumber++); } while (reservedIds.count(candidate));
                        entity.id = candidate;
                        entityIds.insert(candidate);
                        reservedIds.insert(candidate);
                    }
                }
                if(!feature.sketch.referencePlaneId.empty()){
                    if(feature.sketch.attachment)fail("A sketch cannot have both a face and reference-plane support.");
                    const auto support=datums.find(feature.sketch.referencePlaneId);
                    if(support==datums.end()||support->second.kind!=modeling::DatumKind::Plane)fail("Sketch must reference an earlier reference plane.");
                    feature.sketch.referencePlaneFrame=support->second.frame;expected.push_back(feature.sketch.referencePlaneId);
                }
                if(feature.sketch.attachment) {
                    auto& attachment=*feature.sketch.attachment;const auto source=bodies.find(attachment.sourceFeature);if(source==bodies.end())fail("Sketch face support is missing.");
                    modeling::GeometryReference ref;ref.source=modeling::ReferenceSource::BodyFace;ref.featureId=attachment.sourceFeature;ref.index=attachment.faceIndex;ref.signature=attachment.sourceSignature;ref.topologyName=attachment.topologyName;bindGeometryReference(ref,bodies,namedBodies);const auto supportGeometry=resolveGeometryReference(ref,datums,sketches,bodies,namedBodies);if(supportGeometry.kind!=modeling::DatumKind::Plane)fail("Sketch support is no longer planar. Reselect the intended support.");const auto actual=supportGeometry.frame;
                    if((validateDependencies||bodySignature(source->second)==attachment.sourceSignature)&&(actual.Location().Distance(attachment.frame.Location())>1e-7||!actual.Direction().IsEqual(attachment.frame.Direction(),1e-9)||!actual.XDirection().IsEqual(attachment.frame.XDirection(),1e-9)))fail("Stored sketch frame does not match its supporting face.");
                    attachment.topologyName=ref.topologyName;attachment.frame=actual;attachment.sourceSignature=bodySignature(source->second);
                    expected.push_back(attachment.sourceFeature);
                }
                // Legacy equalities must already be internally consistent; dimensional
                // targets and the new solver relations may intentionally require a solve.
                auto legacy=feature.sketch;legacy.dimensions.clear();
                legacy.relations.erase(std::remove_if(legacy.relations.begin(),legacy.relations.end(),[](const auto& r){return r.kind!=modeling::RelationKind::Coincident && r.kind!=modeling::RelationKind::Horizontal && r.kind!=modeling::RelationKind::Vertical;}),legacy.relations.end());
                validSketch(legacy);
                if (!feature.sketch.projections.empty())
                {
                    detail::refreshSketchProjections(feature.sketch, namedBodies);
                    for (const auto& link : feature.sketch.projections)
                        if (std::find(expected.begin(), expected.end(), link.sourceFeature) == expected.end())
                            expected.push_back(link.sourceFeature);
                }
                modeling::validateSketchConstraintReferences(feature.sketch);
                modeling::attachDimensionAnnotations(feature.sketch);
                feature.sketch=modeling::requireSolvedSketch(modeling::solvePlanarSketch(feature.sketch));
                validSketch(feature.sketch);
                sketches.emplace(feature.id, feature.sketch);
            } else if (isPatternFeature(feature.kind)) {
                if (bodyId.empty())
                    fail("Pattern and Mirror require an earlier solid.");
                auto& pattern = feature.pattern;
                const bool mirror = feature.kind == FeatureKind::Mirror;
                const bool circular = feature.kind == FeatureKind::CircularPattern;
                if (!pattern.reference)
                    fail(mirror ? "Select a mirror plane or planar face."
                                : circular ? "Select a pattern axis." : "Select a pattern direction.");
                auto& reference = *pattern.reference;
                using Source = modeling::ReferenceSource;
                const bool validReference = reference.source == Source::Datum ||
                    (mirror ? reference.source == Source::BasePlane || reference.source == Source::BodyFace
                            : reference.source == Source::BaseAxis || reference.source == Source::SketchLine ||
                                  reference.source == Source::BodyEdge ||
                                  (circular && (reference.source == Source::BodyFace ||
                                                reference.source == Source::BodyCircleAxis)));
                if (!validReference)
                    fail("This geometry cannot define the selected pattern or mirror.");
                bindGeometryReference(reference, bodies, namedBodies);
                if ((reference.source == Source::BodyFace || reference.source == Source::BodyEdge ||
                     reference.source == Source::BodyCircleAxis) &&
                    reference.topologyName.empty())
                    fail("Select an edge or face with a unique persistent name.");
                const auto geometry = resolveGeometryReference(reference, datums, sketches, bodies, namedBodies);
                if (geometry.kind != (mirror ? modeling::DatumKind::Plane : modeling::DatumKind::Axis))
                    fail(mirror ? "Mirror requires a plane or planar face."
                                : circular ? "Circular Pattern requires an axis." : "Pattern requires a straight direction.");

                expected.push_back(bodyId);
                const auto addDependency = [&](const std::string& id) {
                    if (!id.empty() && std::find(expected.begin(), expected.end(), id) == expected.end())
                        expected.push_back(id);
                };
                std::vector<modeling::PatternSeed> seeds;
                if (pattern.wholePart)
                {
                    if (!pattern.seeds.empty())
                        fail("Choose feature seeds or all solid bodies.");
                    seeds.push_back({bodyId, {}, body, nullptr, &namedBodies.at(bodyId)});
                }
                else
                {
                    std::set<std::string> remaining(pattern.seeds.begin(), pattern.seeds.end());
                    if (remaining.empty() || remaining.size() != pattern.seeds.size())
                        fail("Select one or more distinct earlier solid features.");
                    pattern.seeds.clear();
                    for (const auto& [id, prior] : solidHistory)
                    {
                        if (!remaining.erase(id))
                            continue;
                        pattern.seeds.push_back(id);
                        addDependency(id);
                        seeds.push_back({id, prior.empty() ? TopoDS_Shape{} : bodies.at(prior),
                                         bodies.at(id), prior.empty() ? nullptr : &namedBodies.at(prior),
                                         &namedBodies.at(id)});
                    }
                    if (!remaining.empty())
                        fail("A pattern seed is missing or later in history.");
                }
                addDependency(reference.featureId);
                const auto transforms = mirror
                    ? std::vector<gp_Trsf>{modeling::mirrorInstance(geometry.frame)}
                    : circular ? modeling::circularInstances(geometry.frame.Axis(), pattern.angle,
                                                              pattern.instances, pattern.equalSpacing, pattern.reverse)
                               : modeling::linearInstances(geometry.frame.Direction(), pattern.spacing,
                                                           pattern.instances, pattern.reverse);
                body = modeling::applyPattern(body, seeds, transforms, bodyNames, feature.id);
                bodyId = feature.id;
            } else if (isSweepFeature(feature.kind)) {
                const auto profile = sketches.find(feature.sketchId);
                const auto path = sketches.find(feature.pathSketchId);
                if (profile == sketches.end() || path == sketches.end() || profile == path)
                    fail("Sweep requires separate earlier sketches for the profile and path.");
                if (!feature.contours.empty())
                    fail("Select one closed sweep profile region.");
                expected = {feature.sketchId, feature.pathSketchId};
                if (!bodyId.empty())
                    expected.push_back(bodyId);
                const auto key = modeling::topologyKey("sweep", {feature.id, feature.sketchId, feature.pathSketchId});
                body = modeling::makeSweep(body, profile->second, path->second, feature.contour,
                                           feature.kind == FeatureKind::SweptCut, bodyNames, key);
                bodyId = feature.id;
            } else if (isRevolveFeature(feature.kind)) {
                const auto source = sketches.find(feature.sketchId);
                if(source==sketches.end()) fail("Revolve must reference an earlier sketch.");
                if(!feature.axis) fail("Select an axis of revolution.");
                auto& ref=*feature.axis;
                using Ref=modeling::ReferenceSource;
                if(ref.source!=Ref::BaseAxis&&ref.source!=Ref::Datum&&ref.source!=Ref::SketchLine&&ref.source!=Ref::BodyEdge)
                    fail("Select a straight sketch line, solid edge or reference axis for revolution.");
                bindGeometryReference(ref,bodies,namedBodies);
                const auto axis=resolveGeometryReference(ref,datums,sketches,bodies,namedBodies);
                if(axis.kind!=modeling::DatumKind::Axis) fail("Revolution requires a straight line or reference axis.");
                expected.push_back(feature.sketchId);
                if(!bodyId.empty()) expected.push_back(bodyId);
                if(!ref.featureId.empty()&&std::find(expected.begin(),expected.end(),ref.featureId)==expected.end())expected.push_back(ref.featureId);
                if(!feature.contour.empty()&&!feature.contours.empty())fail("Feature cannot contain both a single contour and multiple regions.");
                (void)directionName(feature.direction);
                body=modeling::makeRevolutions(body,source->second,feature.contours.empty()?modeling::RegionReferences{feature.contour}:feature.contours,
                    gp_Ax1(axis.frame.Location(),axis.frame.Direction()),feature.angle,feature.direction,feature.kind==FeatureKind::RevolvedCut,bodyNames,feature.id);
                bodyId=feature.id;
            } else if (isEdgeFeature(feature.kind)) {
                if(bodyId.empty())fail("Fillet and Chamfer require an earlier solid.");
                expected.push_back(bodyId);
                std::vector<TopoDS_Shape> edges;
                TopTools_IndexedMapOfShape available;
                TopExp::MapShapes(body, TopAbs_EDGE, available);
                const auto signature = referenceBodySignature(body);
                for(auto& ref:feature.edges){
                    if(ref.source!=modeling::ReferenceSource::BodyEdge||ref.featureId!=bodyId||ref.topologyName.empty()||!ref.entityId.empty()||ref.role!=modeling::PointRole::First)
                        fail("Edge treatments require persistent edges of the immediately preceding solid.");
                    if (ref.topologyName.size() > 8192) fail("Invalid edge topology name.");
                    const auto edge = bodyNames.resolve(ref.topologyName, TopAbs_EDGE);
                    ref.index = available.FindIndex(edge);
                    ref.signature = signature;
                    edges.push_back(edge);
                }
                body=modeling::makeEdgeTreatment(body,edges,feature.size,feature.kind==FeatureKind::Chamfer,bodyNames,feature.id);
                bodyId=feature.id;
            } else if (feature.kind == FeatureKind::Extrude || feature.kind == FeatureKind::Cut) {
                const auto source = sketches.find(feature.sketchId);
                if (source == sketches.end()) fail("Feature must reference an earlier sketch: " + q(feature.sketchId));
                (void)directionName(feature.direction);
                if (!std::isfinite(feature.length) || feature.length <= 0 || feature.length > maximumLength)
                    fail("Extrusion length/depth must be positive and no larger than 1e9 mm.");
                expected.push_back(feature.sketchId);
                if (!bodyId.empty()) expected.push_back(bodyId);
                if (feature.extrusion) {
                    auto& definition = *feature.extrusion;
                    modeling::ExtrudeSupports supports;
                    const auto resolve = [&](modeling::GeometryReference& ref, bool useBody) {
                        bindGeometryReference(ref, bodies, namedBodies);
                        if (!ref.featureId.empty() && std::find(expected.begin(), expected.end(), ref.featureId) == expected.end())
                            expected.push_back(ref.featureId);
                        modeling::ExtrudeReference support;
                        if (ref.source == modeling::ReferenceSource::BodyFace) {
                            if (ref.topologyName.empty() || !namedBodies.count(ref.featureId))
                                fail("Select a persistently named face for this extrusion reference.");
                            support.shape = namedBodies.at(ref.featureId).resolve(ref.topologyName, TopAbs_FACE);
                            if (useBody) {
                                if (ref.featureId != bodyId) fail("Select a body from the stage immediately before the extrusion.");
                                for (TopExp_Explorer solids(body, TopAbs_SOLID); solids.More(); solids.Next())
                                    for (TopExp_Explorer faces(solids.Current(), TopAbs_FACE); faces.More(); faces.Next())
                                        if (faces.Current().IsSame(support.shape)) {
                                            if (!support.body.IsNull()) fail("The selected face belongs to more than one body.");
                                            support.body = solids.Current();
                                        }
                                if (support.body.IsNull()) fail("The reference face no longer belongs to a solid body.");
                                return support;
                            }
                            if (BRepAdaptor_Surface(TopoDS::Face(support.shape)).GetType() != GeomAbs_Plane)
                                return support;
                        } else if (useBody) fail("Select a face of the target solid body.");
                        support.datum = resolveGeometryReference(ref, datums, sketches, bodies, namedBodies);
                        return support;
                    };
                    if (definition.startReference) supports.start = resolve(*definition.startReference, false);
                    if (definition.direction) supports.direction = resolve(*definition.direction, false);
                    if (definition.first.target) supports.first = resolve(*definition.first.target, definition.first.end == modeling::ExtrudeEnd::Body);
                    if (definition.second && definition.second->target)
                        supports.second = resolve(*definition.second->target, definition.second->end == modeling::ExtrudeEnd::Body);
                    for (auto& reference : definition.scope) {
                        const auto selected = resolve(reference, true).body;
                        if (std::none_of(supports.scope.begin(), supports.scope.end(), [&](const auto& item) { return item.IsSame(selected); }))
                            supports.scope.push_back(selected);
                    }
                    if (!feature.contour.empty() && !feature.contours.empty())
                        fail("Feature cannot contain both a single contour and multiple regions.");
                    const auto regions = feature.contours.empty() ? modeling::RegionReferences{feature.contour} : feature.contours;
                    body = modeling::extrudeProfile(source->second, regions, body, feature.kind == FeatureKind::Cut,
                                                    feature.direction, definition, supports, bodyNames, feature.id);
                } else if(!feature.contours.empty()) {
                    if(!feature.contour.empty())fail("Feature cannot contain both a single contour and multiple regions.");
                    body=feature.kind==FeatureKind::Cut
                        ? modeling::makeCuts(body,source->second,feature.contours,feature.length,feature.throughAll,feature.direction,&bodyNames,feature.id)
                        : modeling::makeExtrusions(source->second,feature.contours,feature.length,feature.direction,body,&bodyNames,feature.id);
                } else if (feature.kind == FeatureKind::Cut) {
                    if (body.IsNull()) fail("Extruded Cut requires an earlier solid.");
                    body = modeling::makeCuts(body,source->second,{feature.contour},feature.length,feature.throughAll,feature.direction,&bodyNames,feature.id);
                } else {
                    body = modeling::makeExtrusions(source->second,{feature.contour},feature.length,feature.direction,body,&bodyNames,feature.id);
                }
                bodyId = feature.id;
            } else fail("Unsupported feature kind.");
            if (validateDependencies && feature.dependencies != expected)
                fail("Invalid dependencies or feature order for " + q(feature.id));
            feature.dependencies = std::move(expected);
            if (isSolidFeature(feature.kind))
            {
                bodies[feature.id] = body;
                namedBodies[feature.id] = bodyNames;
                solidHistory.emplace_back(feature.id, previousBody);
            }
        }
    } catch (const Standard_Failure& error) {
        fail("Cannot regenerate part geometry: " + QString::fromUtf8(error.GetMessageString()));
    } catch (const std::invalid_argument& error) {
        fail(QString::fromUtf8(error.what()));
    }
    m_features = std::move(features);
    m_shape = body;
    m_parameters = boxParameters;
    m_bodies=std::move(bodies);m_namedBodies=std::move(namedBodies);
}

TopoDS_Shape PartDocument::solidBefore(const std::string& featureId) const
{
    TopoDS_Shape previous;
    for (const auto& feature : m_features)
    {
        if (feature.id == featureId)
            return previous;
        if (isSolidFeature(feature.kind))
            previous = solidResult(feature.id);
    }
    throw std::runtime_error("Feature is unavailable.");
}

const TopoDS_Shape& PartDocument::solidResult(const std::string& featureId) const
{
    return m_bodies.at(featureId);
}

std::string PartDocument::nextId(FeatureKind kind) const
{
    const std::string prefix = std::string(typeName(kind)) + "-";
    for (std::size_t number = 1; number <= maximumFeatures + 1; ++number) {
        const std::string candidate = prefix + std::to_string(number);
        if (std::none_of(m_features.begin(), m_features.end(), [&](const Feature& f) { return f.id == candidate; })) return candidate;
    }
    fail("Cannot allocate another feature ID.");
}

modeling::Sketch PartDocument::sketchOnFace(int index) const {
    if(m_shape.IsNull()) fail("A planar face sketch requires an existing solid.");
    modeling::Sketch sketch;modeling::FaceAttachment attachment;
    for(const auto& feature:m_features) if(isSolidFeature(feature.kind)) attachment.sourceFeature=feature.id;
    attachment.faceIndex=index;attachment.sourceSignature=bodySignature(m_shape);attachment.frame=faceFrame(m_shape,index);
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(m_shape,TopAbs_FACE,faces);attachment.topologyName=m_namedBodies.at(attachment.sourceFeature).nameOf(faces(index));sketch.attachment=attachment;return sketch;
}

std::string PartDocument::addSketch(const modeling::Sketch& sketch)
{
    Feature feature;
    feature.kind = FeatureKind::Sketch;
    feature.id = nextId(feature.kind);
    feature.name = "Sketch" + feature.id.substr(feature.id.find('-') + 1);
    feature.sketch = sketch;
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate));
    return feature.id;
}

std::string PartDocument::addExtrude(const std::string& sketchId, const std::vector<std::string>& contour, double length, modeling::Direction direction)
{
    Feature feature;
    feature.kind = FeatureKind::Extrude;
    feature.id = nextId(feature.kind);
    feature.name = "Extrude" + feature.id.substr(feature.id.find('-') + 1);
    feature.sketchId = sketchId;
    feature.contour = contour;
    feature.length = length;
    feature.direction = direction;
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate));
    return feature.id;
}

std::string PartDocument::addCut(const std::string& sketchId, const std::vector<std::string>& contour, double depth, bool throughAll, modeling::Direction direction)
{
    Feature feature;
    feature.kind = FeatureKind::Cut;
    feature.id = nextId(feature.kind);
    feature.name = "ExtrudedCut" + feature.id.substr(feature.id.find('-') + 1);
    feature.sketchId = sketchId;
    feature.contour = contour;
    feature.length = depth;
    feature.throughAll = throughAll;
    feature.direction = direction;
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate));
    return feature.id;
}

std::string PartDocument::addExtrudeRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double length,modeling::Direction direction)
{
    if(contours.size()==1)return addExtrude(sketchId,contours.front(),length,direction);
    if(contours.empty())fail("Select at least one closed region.");
    Feature feature;feature.kind=FeatureKind::Extrude;feature.id=nextId(feature.kind);
    feature.name="Extrude"+feature.id.substr(feature.id.find('-')+1);feature.sketchId=sketchId;
    feature.contours=contours;feature.length=length;feature.direction=direction;
    auto candidate=m_features;candidate.push_back(feature);apply(std::move(candidate));return feature.id;
}
std::string PartDocument::addCutRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double depth,bool throughAll,modeling::Direction direction)
{
    if(contours.size()==1)return addCut(sketchId,contours.front(),depth,throughAll,direction);
    if(contours.empty())fail("Select at least one closed region.");
    Feature feature;feature.kind=FeatureKind::Cut;feature.id=nextId(feature.kind);
    feature.name="ExtrudedCut"+feature.id.substr(feature.id.find('-')+1);feature.sketchId=sketchId;
    feature.contours=contours;feature.length=depth;feature.direction=direction;feature.throughAll=throughAll;
    auto candidate=m_features;candidate.push_back(feature);apply(std::move(candidate));return feature.id;
}

std::string PartDocument::addSolidFeature(Feature feature)
{
    feature.id=nextId(feature.kind);
    const char* label = nullptr;
    switch (feature.kind)
    {
    case FeatureKind::Extrude: label = "Extrude"; break;
    case FeatureKind::Cut: label = "Cut"; break;
    case FeatureKind::Revolve: label = "Revolve"; break;
    case FeatureKind::RevolvedCut: label = "RevolvedCut"; break;
    case FeatureKind::Fillet: label = "Fillet"; break;
    case FeatureKind::Chamfer: label = "Chamfer"; break;
    case FeatureKind::LinearPattern: label = "LinearPattern"; break;
    case FeatureKind::CircularPattern: label = "CircularPattern"; break;
    case FeatureKind::Mirror: label = "Mirror"; break;
    case FeatureKind::Sweep: label = "Sweep"; break;
    case FeatureKind::SweptCut: label = "SweptCut"; break;
    default: fail("Use the matching feature operation.");
    }
    feature.name=std::string(label)+feature.id.substr(feature.id.find('-')+1);
    auto candidate = m_features;
    candidate.push_back(feature);
    apply(std::move(candidate), false, isEdgeFeature(feature.kind) ? m_features.size() : 0);
    return feature.id;
}

std::vector<TopoDS_Shape> PartDocument::edgeTreatmentFaces(const std::string& featureId) const
{
    const auto feature = std::find_if(m_features.begin(), m_features.end(), [&](const auto& item) {
        return item.id == featureId;
    });
    if (feature == m_features.end() || !isEdgeFeature(feature->kind) || feature->dependencies.empty()) return {};
    const auto& support = m_namedBodies.at(feature->dependencies.front());
    const auto& result = m_namedBodies.at(featureId);
    TopTools_IndexedMapOfShape seen;
    std::vector<TopoDS_Shape> faces;
    for (const auto& edge : feature->edges) {
        const auto canonical = support.nameOf(support.resolve(edge.topologyName, TopAbs_EDGE));
        for (const auto* role : {"treatment", "treatment-start", "treatment-end"}) {
            const auto found = result.images.find(modeling::topologyKey(role, {featureId, canonical}));
            if (found == result.images.end()) continue;
            for (const auto& face : found->second)
                if (face.ShapeType() == TopAbs_FACE && !seen.Contains(face)) {
                    seen.Add(face);
                    faces.push_back(face);
                }
        }
    }
    return faces;
}

std::vector<ReferenceOption> PartDocument::edgeOptions(bool sharpOnly) const
{
    std::string bodyId;for(const auto& f:m_features)if(isSolidFeature(f.kind))bodyId=f.id;
    if(bodyId.empty())return {};
    const auto& body=m_bodies.at(bodyId);const auto& names=m_namedBodies.at(bodyId);
    const auto signature=referenceBodySignature(body);
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    TopTools_IndexedDataMapOfShapeListOfShape adjacent;
    if (sharpOnly) TopExp::MapShapesAndUniqueAncestors(body, TopAbs_EDGE, TopAbs_FACE, adjacent);
    std::vector<ReferenceOption> result;
    for (int i = 1; i <= edges.Extent(); ++i) {
        const auto& edge = edges(i);
        if (sharpOnly) {
            if (!adjacent.Contains(edge) || BRep_Tool::Degenerated(TopoDS::Edge(edge))) continue;
            const auto& faces = adjacent.FindFromKey(edge);
            if (faces.Extent() != 2 || BRep_Tool::Continuity(
                    TopoDS::Edge(edge), TopoDS::Face(faces.First()), TopoDS::Face(faces.Last())) != GeomAbs_C0)
                continue;
        }
        const auto name = names.nameOf(edge);
        if (name.empty()) continue;
        modeling::GeometryReference ref;
        ref.source = modeling::ReferenceSource::BodyEdge;
        ref.featureId = bodyId;
        ref.index = i;
        ref.signature = signature;
        ref.topologyName = name;
        result.push_back({"Edge " + std::to_string(i), ref, {modeling::DatumKind::Axis, {}}, edge});
    }
    return result;
}

void PartDocument::replaceFeature(const Feature& feature)
{
    auto candidate = m_features;
    const auto current = std::find_if(candidate.begin(), candidate.end(), [&](const Feature& f) { return f.id == feature.id; });
    if (current == candidate.end()) fail("Cannot edit an unknown feature.");
    if (current->kind != feature.kind) fail("Editing cannot change a feature's type.");
    const auto unchanged = isEdgeFeature(feature.kind) ? std::size_t(current - candidate.begin()) : 0;
    *current = feature;
    apply(std::move(candidate), false, unchanged);
}

std::vector<Feature> PartDocument::featureDeletionDependents(const std::vector<std::string>& ids) const
{
    if (ids.empty())
        fail("Select a sketch or feature to delete.");
    for (const auto& id : ids)
        if (std::none_of(m_features.begin(), m_features.end(),
                         [&](const auto& feature) { return feature.id == id; }))
            fail("Select an existing sketch or feature. Origin references are protected.");

    std::set<std::string> removed(ids.begin(), ids.end());
    std::vector<Feature> dependents;
    // History references point backwards, so one forward pass finds the closure.
    for (const auto& feature : m_features)
        if (!removed.count(feature.id) &&
            std::any_of(feature.dependencies.begin(), feature.dependencies.end(),
                        [&](const auto& dependency) { return removed.count(dependency) != 0; }))
        {
            removed.insert(feature.id);
            dependents.push_back(feature);
        }
    return dependents;
}

void PartDocument::deleteFeatures(const std::vector<std::string>& ids, bool includeDependents)
{
    const auto dependents = featureDeletionDependents(ids);
    if (!dependents.empty() && !includeDependents)
        fail("This selection has dependent features. Confirm their deletion first.");
    std::set<std::string> removed(ids.begin(), ids.end());
    for (const auto& dependent : dependents)
        removed.insert(dependent.id);
    auto candidate = m_features;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
                                   [&](const auto& feature) { return removed.count(feature.id) != 0; }),
                    candidate.end());
    apply(std::move(candidate));
}

PartDocument PartDocument::load(const QString& path){return PartFileStore::load(path);}
void PartDocument::save(const QString& path)const{PartFileStore::save(*this,path);}
}
