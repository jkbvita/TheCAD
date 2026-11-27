#pragma once

#include "modeling/ReferenceGeometry.h"
#include <optional>

namespace thecad::modeling {
enum class ExtrudeStart { SketchPlane, Surface, Vertex, Offset };
enum class ExtrudeEnd {
    Blind, ThroughAll, ThroughAllBoth, UpToNext, Vertex, Surface, SurfaceOffset, Body, MidPlane
};
enum class ThinMode { OneDirection, MidPlane, TwoDirections };

struct ExtrudeLeg {
    ExtrudeEnd end = ExtrudeEnd::Blind;
    double depth = 10;
    double offset = 0;
    bool reverseOffset = false;
    bool translateSurface = false;
    double draftAngle = 0;
    bool draftOutward = false;
    std::optional<GeometryReference> target;
};

struct ExtrudeThin {
    ThinMode mode = ThinMode::OneDirection;
    double thickness = 1;
    double secondThickness = 1;
    bool reverse = false;
    double endCap = 0;
    double cornerRadius = 0;
};

struct ExtrudeDefinition {
    ExtrudeStart start = ExtrudeStart::SketchPlane;
    double startOffset = 0;
    std::optional<GeometryReference> startReference;
    std::optional<GeometryReference> direction;
    ExtrudeLeg first;
    std::optional<ExtrudeLeg> second;
    std::optional<ExtrudeThin> thin;
    bool merge = true;
    bool flipCut = false;
    std::vector<GeometryReference> scope;
};

struct ExtrudeReference {
    DatumGeometry datum;
    TopoDS_Shape shape;
    TopoDS_Shape body;
};

struct ExtrudeSupports {
    ExtrudeReference start, direction, first, second;
    std::vector<TopoDS_Shape> scope;
};

std::vector<std::string> openExtrusionProfile(const Sketch&);
TopoDS_Shape extrudeProfile(const Sketch&, const RegionReferences&, const TopoDS_Shape& stock,
                            bool cut, Direction, const ExtrudeDefinition&, const ExtrudeSupports&,
                            TopologyNames&, const std::string& featureId);
}
