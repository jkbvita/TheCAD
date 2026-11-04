#include "document/PartDocument.h"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <cmath>
#include <functional>
#include <algorithm>
using namespace thecad::modeling;
using namespace thecad::document;
constexpr double pi=3.14159265358979323846;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape){require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Invalid resulting BRep");GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
int bodies(const TopoDS_Shape& shape){int n=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++n;return n;}
void closeVolume(double value,double expected){require(std::abs(value-expected)<std::max(1e-6,std::abs(expected)*1e-8),"Precise volume mismatch");}
void reject(const std::function<void()>& action){bool failed=false;try{action();}catch(const std::exception& e){failed=std::string(e.what()).size()>0;}require(failed,"Expected invalid references/geometry rejection");}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 QTemporaryDir directory;require(directory.isValid(),"Temporary directory");
 Sketch sketch;sketch.entities={{EntityKind::Rectangle,{0,0},{40,30},1,"outer"},{EntityKind::Circle,{10,15},{},3,"left"},{EntityKind::Circle,{30,15},{},3,"right"}};
 const auto regions=closedContours(sketch);require(regions.size()==3,"Outer and two inner regions must remain selectable");
 require(regions[0].entityIds.size()==3,"Outer region must include both inner boundary references");
 require(contourContains(regions[0],sketch,{20,15})&&!contourContains(regions[0],sketch,{10,15}),"Annulus region hit testing must exclude holes");
 require(contourContains(regions[1],sketch,{10,15})&&!contourContains(regions[1],sketch,{20,15}),"Inner region hit testing");
 closeVolume(volume(makeExtrusion(selectContour(sketch,regions[0].entityIds),7,Direction::Positive)),(1200-18*pi)*7);
 closeVolume(volume(makeExtrusions(sketch,{regions[0].entityIds,regions[1].entityIds,regions[2].entityIds},7,Direction::Positive)),1200*7);
 auto part=PartDocument::createEmpty();const auto id=part.addSketch(sketch);part.addExtrudeRegions(id,{{"left"},{"right"}},7,Direction::Positive);
 require(bodies(part.shape())==2,"Disconnected selected contours must form two part bodies");closeVolume(volume(part.shape()),18*pi*7);
 const auto path=directory.filePath("multi.thepart");part.save(path);auto reopened=PartDocument::load(path);require(bodies(reopened.shape())==2&&reopened.features()[1].contours.size()==2,"Multi-body feature selections must persist");
 auto feature=reopened.features()[1];feature.length=11;reopened.replaceFeature(feature);closeVolume(volume(reopened.shape()),18*pi*11);
 auto prior=reopened.shape();auto missing=reopened.features()[0];missing.sketch.entities.erase(missing.sketch.entities.begin()+1);reject([&]{reopened.replaceFeature(missing);});require(reopened.shape().IsSame(prior),"Failed region recompute must preserve all bodies");
 auto block=PartDocument::createBox({40,30,10});const auto holes=block.addSketch(sketch);block.addCutRegions(holes,{{"left"},{"right"}},3,true,Direction::Positive);closeVolume(volume(block.shape()),12000-180*pi);require(bodies(block.shape())==1,"Two selected holes must preserve surrounding body");
 block.save(path);closeVolume(volume(PartDocument::load(path).shape()),12000-180*pi);
 auto bosses=PartDocument::createBox({40,30,10});auto topSketch=sketch;FaceAttachment frame;frame.frame=gp_Ax3(gp_Pnt(0,0,10),gp_Dir(0,0,1),gp_Dir(1,0,0));topSketch.attachment=frame;
 closeVolume(volume(makeExtrusions(topSketch,{{"left"},{"right"}},4,Direction::Positive,bosses.shape())),12000+72*pi);
 Sketch nested;nested.entities={{EntityKind::Circle,{0,0},{},10,"a"},{EntityKind::Circle,{0,0},{},6,"b"},{EntityKind::Circle,{0,0},{},2,"c"}};auto rings=closedContours(nested);require(rings.size()==3,"Three nested loops");
 closeVolume(volume(makeExtrusions(nested,{rings[0].entityIds},2,Direction::Positive)),128*pi);closeVolume(volume(makeExtrusions(nested,{rings[1].entityIds},2,Direction::Positive)),64*pi);
 closeVolume(volume(makeExtrusions(nested,{rings[0].entityIds,rings[1].entityIds,rings[2].entityIds},2,Direction::Positive)),200*pi);
 reject([&]{makeExtrusions(nested,{},2,Direction::Positive);});reject([&]{makeExtrusions(nested,{{"missing"}},2,Direction::Positive);});reject([&]{makeExtrusions(nested,{rings[0].entityIds,rings[0].entityIds},2,Direction::Positive);});
 // Owner regression: rectangular wall and triangular roof share their top edge.
 Sketch house;house.entities={{EntityKind::Line,{0,0},{30,0},1,"bottom"},{EntityKind::Line,{30,0},{30,30},1,"right"},{EntityKind::Line,{30,30},{0,30},1,"shared"},{EntityKind::Line,{0,30},{0,0},1,"left"},{EntityKind::Line,{0,30},{15,40},1,"roof-left"},{EntityKind::Line,{15,40},{30,30},1,"roof-right"}};
 for(auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}) {
  house.plane=plane;const auto cells=closedContours(house);require(cells.size()==2,"Shared-edge house must have two bounded cells");RegionReferences both;int wall=-1,roof=-1;
  for(std::size_t i=0;i<cells.size();++i){both.push_back(cells[i].entityIds);if(contourContains(cells[i],house,{15,15}))wall=int(i);if(contourContains(cells[i],house,{15,35}))roof=int(i);}
  require(wall>=0 && roof>=0 && wall!=roof,"Wall/roof actual region classification");closeVolume(volume(makeExtrusions(house,{cells[wall].entityIds},10,Direction::Positive)),9000);closeVolume(volume(makeExtrusions(house,{cells[roof].entityIds},10,Direction::Positive)),1500);
  auto document=PartDocument::createEmpty();const auto sketchId=document.addSketch(house);document.addExtrudeRegions(sketchId,both,10,Direction::Positive);closeVolume(volume(document.shape()),10500);require(bodies(document.shape())==1,"Adjacent regions must fuse to one body");document.save(path);closeVolume(volume(PartDocument::load(path).shape()),10500);
 }
 house.plane=SketchPlane::XY;house.entities.push_back({EntityKind::Line,{0,30},{-10,35},1,"branch"});require(closedContours(house).size()==2,"Shared regions survive dangling branches");
 auto enclosed=house;enclosed.entities.push_back({EntityKind::Rectangle,{-20,-20},{50,60},1,"enclosure"});const auto enclosedRegions=closedContours(enclosed);require(enclosedRegions.size()==3,"Enclosing loop and adjacent internal cells");
 for(const auto& region:enclosedRegions)if(contourContains(region,enclosed,{-15,-15})){require(std::find(region.entityIds.begin(),region.entityIds.end(),"shared")==region.entityIds.end(),"A shared internal edge is not part of the enclosing hole boundary");closeVolume(volume(makeExtrusions(enclosed,{region.entityIds},10,Direction::Positive)),45500);}
 auto strip=house;strip.entities={{EntityKind::Line,{0,0},{10,0},1,"a"},{EntityKind::Line,{10,0},{20,0},1,"b"},{EntityKind::Line,{20,0},{20,10},1,"c"},{EntityKind::Line,{20,10},{10,10},1,"d"},{EntityKind::Line,{10,10},{0,10},1,"e"},{EntityKind::Line,{0,10},{0,0},1,"f"},{EntityKind::Line,{10,0},{10,10},1,"divider"}};require(closedContours(strip).size()==2,"Internal divider produces two regions");
 auto many=PartDocument::createBox({40,30,10});Sketch pattern;pattern.entities={{EntityKind::Rectangle,{0,0},{40,30},1,"outline"}};RegionReferences refs;
 for(int y=0;y<4;++y)for(int x=0;x<10;++x){const auto name="hole-"+std::to_string(y*10+x);pattern.entities.push_back({EntityKind::Circle,{2.0+3.5*x,3.0+6*y},{},0.75,name});refs.push_back({name});}
 require(closedContours(pattern).size()==41,"Forty contours must coexist with enclosing region");auto patternId=many.addSketch(pattern);many.addCutRegions(patternId,refs,10,true,Direction::Positive);closeVolume(volume(many.shape()),12000-40*pi*0.75*0.75*10);
 many.save(path);auto loaded=PartDocument::load(path);require(loaded.features().back().contours.size()==40&&loaded.features()[1].sketch.entities.size()==41,"Many-region save/reopen");closeVolume(volume(loaded.shape()),volume(many.shape()));
 std::cout<<"PASS: nested and independent regions, multiple bodies, batch holes/bosses, forty-region operations, explicit identity and transactional save/load\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
