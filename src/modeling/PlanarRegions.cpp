#include "modeling/PlanarRegions.h"
#include "modeling/SketchArcs.h"
#include <BRepAlgoAPI_Splitter.hxx>
#include <BOPAlgo_BuilderFace.hxx>
#include <BOPTools_AlgoTools2D.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace thecad::modeling {
namespace {
constexpr double tolerance=1e-6;
constexpr const char* fragmentPrefix="@region-edge-v1:";
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
std::string field(const std::string& value){return std::to_string(value.size())+":"+value;}
int sign(double value,double scale){return value>tolerance*scale?1:value<-tolerance*scale?-1:0;}
struct Primitive {
    std::string entityId,key;
    EntityKind kind;
    Point2 first,second,center;
    double radius;
    TopoDS_Edge edge;
    std::set<int> fragments;
};
struct Piece {int owner;std::string first,last;int direction;};
}
bool isFragmentReference(const std::string& reference){return reference.rfind(fragmentPrefix,0)==0;}

std::vector<Contour> intersectedContours(const Sketch& sketch){
    std::vector<Primitive> primitives;
    const auto axes=sketchAxes(sketch);
    const auto local=[&](const gp_Pnt& p){const gp_Vec v(axes.Location(),p);return Point2{v.Dot(gp_Vec(axes.XDirection())),v.Dot(gp_Vec(axes.YDirection()))};};
    const auto append=[&](const SketchEntity& e,const std::string& id,int side,Point2 a,Point2 b){
        Primitive p{id,field(id)+":"+std::to_string(side),e.kind,a,b,circularCenter(e),e.radius,{},{}};
        if(e.kind==EntityKind::Circle)p.edge=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(sketchPoint(sketch,e.first),axes.Direction(),axes.XDirection()),e.radius));
        else if (e.kind == EntityKind::Arc) p.edge = sketchArcEdge(sketch, e);
        else p.edge=BRepBuilderAPI_MakeEdge(sketchPoint(sketch,a),sketchPoint(sketch,b));
        primitives.push_back(std::move(p));
    };
    for(std::size_t i=0;i<sketch.entities.size();++i){
        const auto& e=sketch.entities[i];if(e.construction || e.kind==EntityKind::Point)continue;
        const auto id=e.id.empty()?"entity-"+std::to_string(i+1):e.id;
        if(e.kind==EntityKind::Rectangle){
            const Point2 points[]{e.first,{e.second.x,e.first.y},e.second,{e.first.x,e.second.y}};
            for(int n=0;n<4;++n)append(e,id,n,points[n],points[(n+1)%4]);
        }else append(e,id,0,e.first,e.second);
    }
    if(primitives.size()<2)return {};
    // Source order must not decide topology or persistent region identities.
    std::sort(primitives.begin(),primitives.end(),[](const auto& a,const auto& b){return a.key<b.key;});
    TopTools_ListOfShape arguments;for(const auto& p:primitives)arguments.Append(p.edge);
    BRepAlgoAPI_Splitter splitter;splitter.SetArguments(arguments);splitter.SetNonDestructive(Standard_True);splitter.SetFuzzyValue(tolerance);splitter.Build();
    if(!splitter.IsDone() || splitter.HasErrors())fail("The intersecting sketch edges could not be subdivided.");
    TopTools_IndexedMapOfShape fragments;TopExp::MapShapes(splitter.Shape(),TopAbs_EDGE,fragments);
    if(fragments.Extent()>12000)fail("The sketch has too many intersection fragments; simplify its profile.");
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(splitter.Shape(), TopAbs_VERTEX, vertices);
    std::vector<std::set<int>> vertexOwners(std::size_t(vertices.Extent() + 1));
    std::vector<std::string> overlapEndpoints(vertexOwners.size());
    std::vector<int> owner(std::size_t(fragments.Extent() + 1), -1);
    for (std::size_t i = 0; i < primitives.size(); ++i)
    {
        auto& source = primitives[i];
        auto images = splitter.Modified(source.edge);
        if (images.IsEmpty())
            images.Append(source.edge);
        for (TopTools_ListIteratorOfListOfShape it(images); it.More(); it.Next())
        {
            const int index = fragments.FindIndex(it.Value());
            if (!index)
                continue;
            // Splitter returns one edge for the shared span. The first stable
            // source key names it, while every source keeps its complete image
            // set so a partially owned source cannot be mistaken for a whole edge.
            source.fragments.insert(index);
            auto& primary = owner[std::size_t(index)];
            if (primary < 0)
                primary = int(i);
            else if (primary != int(i) && source.kind != EntityKind::Circle)
            {
                // A shorter coincident segment can end within one longer edge.
                // Keep that endpoint as a fallback name for the split vertex.
                for (TopExp_Explorer v(fragments(index), TopAbs_VERTEX); v.More(); v.Next())
                {
                    auto& endpoint = overlapEndpoints[std::size_t(vertices.FindIndex(v.Current()))];
                    if (!endpoint.empty())
                        continue;
                    const auto point = local(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())));
                    if (std::hypot(point.x - source.first.x, point.y - source.first.y) <= tolerance * 2)
                        endpoint = source.key + ":first";
                    else if (std::hypot(point.x - source.second.x, point.y - source.second.y) <= tolerance * 2)
                        endpoint = source.key + ":second";
                }
            }
        }
    }
    for(int i=1;i<=fragments.Extent();++i){
        if(owner[std::size_t(i)]<0)fail("An intersection fragment lost its source geometry reference.");
        for(TopExp_Explorer v(fragments(i),TopAbs_VERTEX);v.More();v.Next()){
            const int index=vertices.FindIndex(v.Current());vertexOwners[std::size_t(index)].insert(owner[std::size_t(i)]);
        }
    }
    const auto vertexKey=[&](const TopoDS_Vertex& v){
        const int index=vertices.FindIndex(v);if(!index)fail("A region vertex lost its intersection identity.");
        const auto point=local(BRep_Tool::Pnt(v));const auto& owners=vertexOwners[std::size_t(index)];std::vector<std::string> labels;
        for(const auto n:owners){const auto& p=primitives[std::size_t(n)];if(p.kind!=EntityKind::Circle){
            if(std::hypot(point.x-p.first.x,point.y-p.first.y)<=tolerance*2)labels.push_back(p.key+":first");
            if(std::hypot(point.x-p.second.x,point.y-p.second.y)<=tolerance*2)labels.push_back(p.key+":second");
        }}
        for(auto a=owners.begin();a!=owners.end();++a)for(auto b=std::next(a);b!=owners.end();++b){
            const auto& x=primitives[std::size_t(*a)];const auto& y=primitives[std::size_t(*b)];int branch=0;
            if(isCircular(x.kind) && isCircular(y.kind)){
                const double dx=y.center.x-x.center.x,dy=y.center.y-x.center.y;branch=sign(dx*(point.y-x.center.y)-dy*(point.x-x.center.x),std::hypot(dx,dy));
            }else if(isCircular(x.kind) || isCircular(y.kind)){
                const auto& line=isCircular(x.kind)?y:x;const auto& circle=isCircular(x.kind)?x:y;
                const double dx=line.second.x-line.first.x,dy=line.second.y-line.first.y;branch=sign(dx*(point.x-circle.center.x)+dy*(point.y-circle.center.y),std::hypot(dx,dy));
            }
            labels.push_back("intersection:"+field(x.key)+field(y.key)+":"+std::to_string(branch));
        }
        if (labels.empty() && !overlapEndpoints[std::size_t(index)].empty())
            labels.push_back(overlapEndpoints[std::size_t(index)]);
        std::sort(labels.begin(),labels.end());std::string result;for(const auto& label:labels)result+=field(label);return result;
    };
    // Build planar pcurves, then walk both sides of every subdivided edge.
    // OCCT discards dangling/internal wires when assembling bounded areas.
    const TopoDS_Face plane=BRepBuilderAPI_MakeFace(gp_Pln(axes));TopTools_ListOfShape directed;
    for(int i=1;i<=fragments.Extent();++i){
        const auto edge=TopoDS::Edge(fragments(i));BOPTools_AlgoTools2D::BuildPCurveForEdgeOnFace(edge,plane);
        directed.Append(edge.Oriented(TopAbs_FORWARD));directed.Append(edge.Oriented(TopAbs_REVERSED));
    }
    BOPAlgo_BuilderFace builder;builder.SetFace(plane);builder.SetShapes(directed);builder.SetAvoidInternalShapes(Standard_True);builder.Perform();
    if(builder.HasErrors())fail("The subdivided sketch could not form valid planar regions.");
    std::vector<Contour> regions;std::set<std::vector<std::string>> identities;
    for(TopTools_ListIteratorOfListOfShape it(builder.Areas());it.More();it.Next()){
        auto face=TopoDS::Face(it.Value());BRepTopAdaptor_FClass2d classifier(face,tolerance);
        if(classifier.PerformInfinitePoint()==TopAbs_IN)continue;
        GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);
        if(!std::isfinite(properties.Mass()) || properties.Mass()<=tolerance*tolerance)continue;
        if(!BRepCheck_Analyzer(face).IsValid())fail("An intersection produces an invalid or zero-width region; adjust the touching geometry.");
        std::vector<Piece> pieces;std::map<int,std::set<int>> used;
        for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){
            std::vector<Piece> wire;
            for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()),face);e.More();e.Next()){
                const auto edge=e.Current();const int index=fragments.FindIndex(edge);if(!index)fail("A region boundary lost its source edge.");
                const int n=owner[std::size_t(index)];used[n].insert(index);
                const auto first=TopExp::FirstVertex(edge,Standard_True),last=TopExp::LastVertex(edge,Standard_True);
                BRepAdaptor_Curve curve(edge);gp_Pnt point;gp_Vec tangent;curve.D1((curve.FirstParameter()+curve.LastParameter())/2,point,tangent);if(edge.Orientation()==TopAbs_REVERSED)tangent.Reverse();
                const auto& source=primitives[std::size_t(n)];int direction;
                if(isCircular(source.kind)){const gp_Vec radial(sketchPoint(sketch,source.center),point);direction=radial.Crossed(tangent).Dot(gp_Vec(axes.Direction()))>0?1:-1;}
                else direction=tangent.Dot(gp_Vec(sketchPoint(sketch,source.first),sketchPoint(sketch,source.second)))>0?1:-1;
                wire.push_back({n,vertexKey(first),vertexKey(last),direction});
            }
            // A circle's artificial parameter seam must not enter saved IDs.
            bool changed=true;while(changed && wire.size()>1){changed=false;for(std::size_t i=0;i<wire.size();++i){const auto j=(i+1)%wire.size();if(wire[i].owner==wire[j].owner && wire[i].direction==wire[j].direction && wire[i].last.empty() && wire[j].first.empty()){
                if(j){wire[i].last=wire[j].last;wire.erase(wire.begin()+std::ptrdiff_t(j));}else {wire[0].first=wire[i].first;wire.erase(wire.begin()+std::ptrdiff_t(i));}changed=true;break;
            }}}
            pieces.insert(pieces.end(),wire.begin(),wire.end());
        }
        std::map<std::string,bool> whole;
        for(std::size_t n=0;n<primitives.size();++n){const auto& p=primitives[n];if(!whole.count(p.entityId))whole[p.entityId]=true;if(used[int(n)]!=p.fragments || p.fragments.empty())whole[p.entityId]=false;}
        Contour region;region.face=face;std::set<std::string> references;
        for(const auto& piece:pieces){const auto& p=primitives[std::size_t(piece.owner)];
            if(whole[p.entityId])references.insert(p.entityId);
            else references.insert(std::string(fragmentPrefix)+field(p.key)+field(piece.first)+field(piece.last)+(piece.direction>0?"+":"-"));
        }
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){const auto edge=TopoDS::Edge(e.Current());const int index=fragments.FindIndex(edge);const auto& p=primitives[std::size_t(owner[std::size_t(index)])];auto first=vertexKey(TopExp::FirstVertex(edge,Standard_True)),last=vertexKey(TopExp::LastVertex(edge,Standard_True));BRepAdaptor_Curve curve(edge);gp_Pnt pos;gp_Vec tangent;curve.D1((curve.FirstParameter()+curve.LastParameter())/2,pos,tangent);if(edge.Orientation()==TopAbs_REVERSED)tangent.Reverse();if(isCircular(p.kind)){if(gp_Vec(sketchPoint(sketch,p.center),pos).Crossed(tangent).Dot(gp_Vec(axes.Direction()))<0)std::swap(first,last);}else if(last<first)std::swap(first,last);region.boundaryNames.add(topologyKey("fragment",{p.key+":"+field(first)+field(last)}),edge);}
        region.entityIds.assign(references.begin(),references.end());
        if(region.entityIds.empty() || region.entityIds.size()>1000)fail("A region exceeds the supported boundary-reference limit.");
        if(!identities.insert(region.entityIds).second)fail("Two sketch regions have ambiguous identities; adjust the intersecting geometry.");
        regions.push_back(std::move(region));if(regions.size()>1000)fail("The sketch contains too many closed regions.");
    }
    std::sort(regions.begin(),regions.end(),[](const auto& a,const auto& b){return a.entityIds<b.entityIds;});return regions;
}
}
