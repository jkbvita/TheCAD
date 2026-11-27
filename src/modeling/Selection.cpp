#include "modeling/Selection.h"
#include <algorithm>
#include <cmath>
namespace thecad::modeling {
bool operator==(const SelectionRef& a,const SelectionRef& b){return a.kind==b.kind&&a.featureId==b.featureId&&a.entityId==b.entityId&&a.role==b.role&&a.index==b.index&&a.generation==b.generation;}
bool isTopology(SelectionKind k){return k==SelectionKind::Body||k==SelectionKind::Face||k==SelectionKind::Edge||k==SelectionKind::Vertex;}
std::uint32_t selectionFilter(SelectionKind k){switch(k){
    case SelectionKind::Face:return FilterFaces;case SelectionKind::Edge:return FilterEdges;case SelectionKind::Vertex:return FilterVertices;
    case SelectionKind::Body:return FilterBodies;case SelectionKind::SketchEntity:case SelectionKind::SketchPoint:return FilterSketch;
    case SelectionKind::Dimension:case SelectionKind::Relation:return FilterAnnotations;default:return FilterReferences;}}
const char* selectionKindName(SelectionKind k){switch(k){
    case SelectionKind::Body:return "Solid body";case SelectionKind::Face:return "Face";case SelectionKind::Edge:return "Edge";case SelectionKind::Vertex:return "Vertex";
    case SelectionKind::SketchEntity:return "Sketch entity";case SelectionKind::SketchPoint:return "Sketch point";case SelectionKind::Dimension:return "Dimension";
    case SelectionKind::Relation:return "Relation";case SelectionKind::OriginPlane:return "Origin plane";case SelectionKind::ReferencePlane:return "Reference plane";
    case SelectionKind::ReferenceAxis:return "Reference axis";case SelectionKind::ReferencePoint:return "Reference point";}return "Geometry";}
bool SelectionPolicy::allows(SelectionKind kind) const{return !enabled||filters==0||(filters&selectionFilter(kind))!=0;}
std::vector<SelectionCandidate> SelectionPolicy::order(std::vector<SelectionCandidate> values,bool hidden) const{
    values.erase(std::remove_if(values.begin(),values.end(),[&](const auto& c){return !allows(c.reference.kind)||(!hidden&&c.occluded)||!std::isfinite(c.distance)||!std::isfinite(c.depth);}),values.end());
    const auto priority=[](SelectionKind k){switch(k){
        case SelectionKind::Vertex:case SelectionKind::SketchPoint:case SelectionKind::ReferencePoint:return 0;
        case SelectionKind::Relation:return 1;case SelectionKind::Dimension:return 2;
        case SelectionKind::Edge:case SelectionKind::SketchEntity:case SelectionKind::ReferenceAxis:return 3;
        case SelectionKind::Face:return 4;case SelectionKind::OriginPlane:case SelectionKind::ReferencePlane:return 5;default:return 6;}};
    std::stable_sort(values.begin(),values.end(),[&](const auto& a,const auto& b){
        // Hidden alternatives are explicit and appear after visible candidates.
        if(a.occluded!=b.occluded)return !a.occluded;
        const int pa=priority(a.reference.kind),pb=priority(b.reference.kind);if(pa!=pb)return pa<pb;
        if(a.distance!=b.distance)return a.distance<b.distance;return a.depth<b.depth;
    });
    std::vector<SelectionCandidate> result;for(const auto& c:values)if(std::none_of(result.begin(),result.end(),[&](const auto& v){return v.reference==c.reference;}))result.push_back(c);return result;
}
void SelectionModel::select(const SelectionRef& ref,bool toggle){
    if(!toggle){m_items={ref};return;}const auto found=std::find(m_items.begin(),m_items.end(),ref);if(found==m_items.end())m_items.push_back(ref);else m_items.erase(found);
}
void SelectionModel::invalidateTopology(std::uint64_t generation){m_items.erase(std::remove_if(m_items.begin(),m_items.end(),[&](const auto& ref){return isTopology(ref.kind)&&ref.generation!=generation;}),m_items.end());}
}
