#include "modeling/TopologyNames.h"
#include <BRepBuilderAPI_MakeShape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <algorithm>
#include <stdexcept>
namespace thecad::modeling {
std::string topologyKey(const std::string& role,std::vector<std::string> sources){std::sort(sources.begin(),sources.end());sources.erase(std::unique(sources.begin(),sources.end()),sources.end());std::string key="tn1:"+role+":";for(const auto& s:sources)key+=std::to_string(s.size())+":"+s;return key;}
void TopologyNames::add(const std::string& key,const TopoDS_Shape& shape){if(key.empty()||shape.IsNull())return;auto& list=images[key];if(std::none_of(list.begin(),list.end(),[&](const auto& s){return s.IsSame(shape);}))list.push_back(shape);}
std::string TopologyNames::nameOf(const TopoDS_Shape& shape)const{std::string result;for(const auto& [key,list]:images)if(key.size()<=8192&&list.size()==1&&list[0].IsSame(shape)&&(result.empty()||key.size()<result.size()||(key.size()==result.size()&&key<result)))result=key;return result;}
TopoDS_Shape TopologyNames::resolve(const std::string& key,TopAbs_ShapeEnum type)const{const auto found=images.find(key);if(found==images.end()||found->second.size()!=1||found->second[0].ShapeType()!=type)throw std::runtime_error("The named support was removed, split or became ambiguous. Reselect the intended geometry; no automatic reattachment was performed.");return found->second[0];}
TopologyNames propagateTopology(const TopologyNames& names,BRepBuilderAPI_MakeShape& operation,const TopoDS_Shape& result){TopologyNames next;TopTools_IndexedMapOfShape present;TopExp::MapShapes(result,present);for(const auto& [key,list]:names.images){next.images[key];for(const auto& input:list){const auto& modified=operation.Modified(input);if(modified.IsEmpty()){if(!operation.IsDeleted(input)&&present.Contains(input))next.add(key,input);}else for(TopTools_ListIteratorOfListOfShape it(modified);it.More();it.Next())if(it.Value().ShapeType()==input.ShapeType()&&present.Contains(it.Value()))next.add(key,it.Value());}}return next;}
void TopologyNames::complete(const TopoDS_Shape& body){
    TopTools_IndexedMapOfShape all;TopExp::MapShapes(body,all);for(auto& [key,list]:images){list.erase(std::remove_if(list.begin(),list.end(),[&](const auto& s){return !all.Contains(s);}),list.end());for(auto& shape:list)shape=all(all.FindIndex(shape));}
    // Stable edge/vertex identities come from incident generated faces/edges.
    for(const auto type:{TopAbs_EDGE,TopAbs_VERTEX}){TopTools_IndexedMapOfShape targets;TopExp::MapShapes(body,type,targets);for(int n=1;n<=targets.Extent();++n){std::vector<std::string> parents;for(const auto& [key,list]:images){if(list.size()!=1||list[0].ShapeType()!=(type==TopAbs_EDGE?TopAbs_FACE:TopAbs_EDGE))continue;for(TopExp_Explorer e(list[0],type);e.More();e.Next())if(e.Current().IsSame(targets(n))){parents.push_back(key);break;}}if(!parents.empty())add(topologyKey(type==TopAbs_EDGE?"edge":"vertex",parents),targets(n));}}
}
}
