#pragma once

#include "modeling/Sketch.h"
#include <QString>
#include <TopoDS_Shape.hxx>
#include <string>
#include <vector>

namespace thecad::document {

struct BoxParameters { double width = 100.0, depth = 70.0, height = 40.0; };
enum class FeatureKind { Box, Sketch, Extrude, Cut };
struct Feature {
    std::string id;
    std::string name;
    FeatureKind kind = FeatureKind::Sketch;
    BoxParameters box;
    modeling::Sketch sketch;
    std::string sketchId;
    std::vector<std::string> contour;
    modeling::RegionReferences contours;
    double length = 10.0;
    modeling::Direction direction = modeling::Direction::Positive;
    bool throughAll = false;
    std::vector<std::string> dependencies;
};

// Sequential prototype feature history. No UI widgets or permanent topology naming.
// Every edit regenerates a candidate history before changing this document.
class PartDocument final {
public:
    static PartDocument createEmpty();
    static PartDocument createBox(BoxParameters parameters = {});
    static PartDocument load(const QString& path);
    void save(const QString& path) const;

    const BoxParameters& parameters() const { return m_parameters; }
    const TopoDS_Shape& shape() const { return m_shape; }
    const std::vector<Feature>& features() const { return m_features; }
    modeling::Sketch sketchOnFace(int faceIndex) const;
    std::string addSketch(const modeling::Sketch& sketch);
    std::string addExtrude(const std::string& sketchId, const std::vector<std::string>& contour, double length, modeling::Direction direction);
    std::string addCut(const std::string& sketchId, const std::vector<std::string>& contour, double depth, bool throughAll, modeling::Direction direction);
    std::string addExtrudeRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double length,modeling::Direction direction);
    std::string addCutRegions(const std::string& sketchId,const modeling::RegionReferences& contours,double depth,bool throughAll,modeling::Direction direction);
    void replaceFeature(const Feature& feature);

private:
    void apply(std::vector<Feature> features, bool validateDependencies = false);
    std::string nextId(FeatureKind kind) const;
    BoxParameters m_parameters;
    TopoDS_Shape m_shape;
    std::vector<Feature> m_features;
};

} // namespace thecad::document
