#pragma once

#include "modeling/Sketch.h"
#include "modeling/ReferenceGeometry.h"
#include <QString>
#include <TopoDS_Shape.hxx>
#include <string>
#include <vector>
#include <map>

namespace thecad::document {

struct BoxParameters { double width = 100.0, depth = 70.0, height = 40.0; };
enum class FeatureKind { Box, Sketch, Extrude, Cut, ReferenceGeometry, Revolve, RevolvedCut, Fillet, Chamfer, LinearPattern, Mirror, Sweep, SweptCut };
inline bool isRevolveFeature(FeatureKind kind){return kind==FeatureKind::Revolve||kind==FeatureKind::RevolvedCut;}
inline bool isEdgeFeature(FeatureKind kind){return kind==FeatureKind::Fillet||kind==FeatureKind::Chamfer;}
inline bool isSweepFeature(FeatureKind kind){return kind==FeatureKind::Sweep||kind==FeatureKind::SweptCut;}
inline bool usesProfile(FeatureKind kind){return kind==FeatureKind::Extrude||kind==FeatureKind::Cut||isRevolveFeature(kind)||isSweepFeature(kind);}
inline bool isPatternFeature(FeatureKind kind)
{
    return kind == FeatureKind::LinearPattern || kind == FeatureKind::Mirror;
}
inline bool isSolidFeature(FeatureKind kind){return kind==FeatureKind::Box||usesProfile(kind)||isEdgeFeature(kind)||isPatternFeature(kind);}
struct PatternParameters
{
    std::optional<modeling::GeometryReference> reference;
    std::vector<std::string> seeds;
    bool wholePart = false;
    double spacing = 10;
    int instances = 2;
    bool reverse = false;
};
struct Feature {
    std::string id;
    std::string name;
    FeatureKind kind = FeatureKind::Sketch;
    BoxParameters box;
    modeling::Sketch sketch;
    std::string sketchId;
    std::string pathSketchId; // Sweep path is a distinct, earlier sketch.
    std::vector<std::string> contour;
    modeling::RegionReferences contours;
    double length = 10.0;
    modeling::Direction direction = modeling::Direction::Positive;
    bool throughAll = false;
    double angle = 360.0;
    double size = 2.0; // Constant fillet radius / equal chamfer distance, in mm.
    std::optional<modeling::GeometryReference> axis;
    std::vector<modeling::GeometryReference> edges;
    PatternParameters pattern;
    std::vector<std::string> dependencies;
    modeling::DatumDefinition reference;
    modeling::DatumGeometry datum; // Derived by regeneration, not persisted.
    bool referenceVisible=true;
};
struct ReferenceOption {std::string label;modeling::GeometryReference reference;modeling::DatumGeometry geometry;TopoDS_Shape selectionShape;};

// Sequential feature history with persistent support names and no UI dependency.
// Every edit regenerates a candidate history before changing this document.
class PartDocument final {
    friend class PartFileStore;
public:
    static PartDocument createEmpty();
    static PartDocument createBox(BoxParameters parameters = {});
    static PartDocument load(const QString& path);
    void save(const QString& path) const;

    const BoxParameters& parameters() const { return m_parameters; }
    const TopoDS_Shape& shape() const { return m_shape; }
    const std::vector<Feature>& features() const { return m_features; }
    modeling::Sketch sketchOnFace(int faceIndex) const;
    modeling::Sketch sketchOnReferencePlane(const std::string& id) const;
    std::string addReferenceGeometry(const modeling::DatumDefinition& definition);
    modeling::DatumGeometry resolveReference(const modeling::GeometryReference& reference) const;
    std::vector<ReferenceOption> referenceOptions() const;
    // Includes curved edges, unlike axis-only referenceOptions(). No index-only
    // fallback is permitted for new edge treatments.
    std::vector<ReferenceOption> edgeOptions() const;
    PartDocument beforeFeature(const std::string& id) const;
    std::string addSketch(const modeling::Sketch& sketch);
    std::string addExtrude(const std::string& sketchId, const std::vector<std::string>& contour, double length, modeling::Direction direction);
    std::string addCut(const std::string& sketchId, const std::vector<std::string>& contour, double depth, bool throughAll, modeling::Direction direction);
    std::string addExtrudeRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double length,modeling::Direction direction);
    std::string addCutRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double depth,bool throughAll,modeling::Direction direction);
    std::string addSolidFeature(Feature feature);
    void replaceFeature(const Feature& feature);
    // Ordered transitive closure, excluding explicitly selected references.
    std::vector<Feature> referenceDeletionDependents(const std::vector<std::string>& ids) const;
    void deleteReferenceGeometry(const std::vector<std::string>& ids,bool includeDependents=false);

private:
    void apply(std::vector<Feature> features, bool validateDependencies = false);
    std::string nextId(FeatureKind kind) const;
    BoxParameters m_parameters;
    TopoDS_Shape m_shape;
    std::vector<Feature> m_features;
    std::map<std::string,TopoDS_Shape> m_bodies;
    std::map<std::string,modeling::TopologyNames> m_namedBodies;
};

} // namespace thecad::document
