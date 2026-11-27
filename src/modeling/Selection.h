#pragma once
#include "modeling/Sketch.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace thecad::modeling {
enum class SelectionKind { Body, Face, Edge, Vertex, SketchEntity, SketchPoint,
    Dimension, Relation, OriginPlane, ReferencePlane, ReferenceAxis, ReferencePoint };
enum SelectionFilter : std::uint32_t {
    FilterFaces=1, FilterEdges=2, FilterVertices=4, FilterSketch=8,
    FilterReferences=16, FilterBodies=32, FilterAnnotations=64, FilterAll=127
};
struct SelectionRef {
    SelectionKind kind=SelectionKind::Body;
    std::string featureId,entityId;
    PointRole role=PointRole::First;
    int index=0;
    std::uint64_t generation=0; // Topology indices are valid only for this displayed body.
};
bool operator==(const SelectionRef&,const SelectionRef&);
bool isTopology(SelectionKind);
std::uint32_t selectionFilter(SelectionKind);
const char* selectionKindName(SelectionKind);
struct SelectionCandidate {
    SelectionRef reference;
    double distance=0,depth=0;
    bool occluded=false;
};
struct SelectionPolicy {
    std::uint32_t filters=0; // No chosen filters means unrestricted picking.
    bool enabled=true;
    bool allows(SelectionKind kind) const;
    std::vector<SelectionCandidate> order(std::vector<SelectionCandidate>,bool includeOccluded=false) const;
};
class SelectionModel {
public:
    const std::vector<SelectionRef>& items() const {return m_items;}
    void select(const SelectionRef&,bool toggle=false);
    void clear(){m_items.clear();}
    void invalidateTopology(std::uint64_t generation);
private:
    std::vector<SelectionRef> m_items;
};
}
