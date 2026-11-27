#include "document/PartDocument.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <functional>
#include <gp_Trsf.hxx>
#include <iostream>
#include <cmath>
#include <set>
#include <stdexcept>
using namespace thecad::modeling;
constexpr double pi=3.14159265358979323846;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
double area(const Contour& region){GProp_GProps p;BRepGProp::SurfaceProperties(region.face,p);return p.Mass();}
double volume(const TopoDS_Shape& shape){require(BRepCheck_Analyzer(shape).IsValid(),"Invalid solid result");GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
void closeValue(double value,double expected){if(std::abs(value-expected)>std::max(1e-6,std::abs(expected)*1e-8))throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(value));}
std::vector<std::string> regionAt(const Sketch& sketch,Point2 point){std::vector<std::string> result;for(const auto& r:closedContours(sketch))if(contourContains(r,sketch,point)){require(result.empty(),"Interior point belongs to more than one region");result=r.entityIds;}require(!result.empty(),"Missing selected region");return result;}
std::set<std::vector<std::string>> identities(const Sketch& sketch){std::set<std::vector<std::string>> result;for(const auto& r:closedContours(sketch))result.insert(r.entityIds);return result;}
int bodies(const TopoDS_Shape& shape){int count=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++count;return count;}
void reject(const std::function<void()>& action){bool rejected=false;try{action();}catch(const std::exception& e){rejected=std::string(e.what()).size()>0;}require(rejected,"Invalid region edit did not reject clearly");}
Sketch lineRectangle(){Sketch s;s.entities={{EntityKind::Line,{0,0},{15,0},1,"top"},{EntityKind::Line,{15,0},{15,-15},1,"right"},{EntityKind::Line,{15,-15},{0,-15},1,"bottom"},{EntityKind::Line,{0,-15},{0,0},1,"left"},{EntityKind::Circle,{0,0},{},10,"circle"}};return s;}
void sharedSpans()
{
    Sketch sketch;
    sketch.entities = {{EntityKind::Rectangle, {0, 0}, {20, 20}, 1, "first"},
                       {EntityKind::Rectangle, {10, 0}, {30, 20}, 1, "second"}};
    const auto regions = closedContours(sketch);
    require(regions.size() == 3, "Two overlapping rectangles must form three cells.");
    RegionReferences all;
    for (const auto& region : regions)
    {
        closeValue(area(region), 200);
        all.push_back(region.entityIds);
    }
    const auto shared = regionAt(sketch, {15, 10});
    require(shared != regionAt(sketch, {5, 10}) && shared != regionAt(sketch, {25, 10}),
            "The overlap must be independently selectable.");
    const auto united = makeExtrusions(sketch, all, 2, Direction::Positive);
    closeValue(volume(united), 1200);
    require(bodies(united) == 1, "Shared spans should not create internal walls.");

    auto reordered = sketch;
    std::reverse(reordered.entities.begin(), reordered.entities.end());
    require(identities(reordered) == identities(sketch), "Shared-span identities must be order-independent.");
    auto adjacent = sketch;
    adjacent.entities[1].first.x = 20;
    adjacent.entities[1].second.x = 40;
    require(closedContours(adjacent).size() == 2, "Two rectangles sharing a full side need two cells.");

    Sketch spans;
    spans.entities = {{EntityKind::Rectangle, {0, 0}, {20, 20}, 1, "box"},
                      {EntityKind::Line, {8, 0}, {3, 0}, 1, "overlap-first"},
                      {EntityKind::Line, {15, 0}, {10, 0}, 1, "overlap-second"}};
    const auto single = closedContours(spans);
    require(single.size() == 1, "Redundant spans must not introduce new cells.");
    closeValue(area(single.front()), 400);
    for (const auto& [key, edges] : single.front().boundaryNames.images)
        require(edges.size() == 1, "Split boundary segments need unique topology names.");

    Sketch open;
    open.entities = {{EntityKind::Line, {0, 0}, {20, 0}, 1, "line"},
                     {EntityKind::Line, {30, 0}, {10, 0}, 1, "overlap"}};
    require(closedContours(open).empty(), "Overlapping open lines must remain open.");

    Sketch circles;
    circles.entities = {{EntityKind::Circle, {0, 0}, {}, 10, "circle"},
                        {EntityKind::Circle, {0, 0}, {}, 10, "mirror"}};
    const auto disk = closedContours(circles);
    require(disk.size() == 1, "Coincident circles must form one region.");
    closeValue(area(disk.front()), 100 * pi);
    circles.entities.push_back({EntityKind::Line, {-15, 0}, {15, 0}, 1, "divider"});
    auto original = circles;
    original.entities.erase(original.entities.begin() + 1);
    require(identities(circles) == identities(original),
            "A coincident circle must not introduce artificial seam identities.");

    auto part = thecad::document::PartDocument::createBox({50, 50, 10});
    const auto source = part.addSketch(sketch);
    part.addCutRegions(source, {shared}, 3, false, Direction::Positive);
    closeValue(volume(part.shape()), 24400);
    auto resized = part.features()[1];
    resized.sketch.entities[0].second.y = 25;
    resized.sketch.entities[1].second.y = 25;
    part.replaceFeature(resized);
    closeValue(volume(part.shape()), 24250);

    QTemporaryDir directory;
    require(directory.isValid(), "Could not create overlap fixture directory.");
    const auto path = directory.filePath("shared-spans.thepart");
    part.save(path);
    const auto reopened = thecad::document::PartDocument::load(path);
    closeValue(volume(reopened.shape()), 24250);
    require(reopened.features()[1].sketch.entities.size() == 2 &&
                reopened.features().back().contour == shared,
            "Reopening a cut must preserve both source entities and its selected cell.");
}

int main(){try{
    sharedSpans();
    Sketch sketch;sketch.entities={{EntityKind::Circle,{0,0},{},10,"circle"},{EntityKind::Rectangle,{0,0},{15,-15},1,"rectangle"}};
    auto regions=closedContours(sketch);require(regions.size()==3,"Circle/rectangle must form three selectable regions");
    std::set<std::vector<std::string>> ids;RegionReferences all;double total=0;
    for(const auto& r:regions){require(ids.insert(r.entityIds).second,"Every region must have a distinct reference");all.push_back(r.entityIds);total+=area(r);closeValue(volume(makeExtrusions(sketch,{r.entityIds},2,Direction::Positive)),area(r)*2);}
    closeValue(total,225+75*pi);closeValue(volume(makeExtrusions(sketch,all,2,Direction::Positive)),2*(225+75*pi));
    bool overlap=false;for(const auto& r:regions)if(contourContains(r,sketch,{3,-3})){closeValue(area(r),25*pi);overlap=true;}require(overlap,"The overlap must be independently selectable");
    Sketch circles;circles.entities={{EntityKind::Circle,{-5,0},{},10,"a"},{EntityKind::Circle,{5,0},{},10,"b"}};
    regions=closedContours(circles);require(regions.size()==3,"Overlapping circles must form three regions");ids.clear();for(const auto& r:regions)require(ids.insert(r.entityIds).second,"Circle fragments must distinguish both crescents and lens");
    const double lens=200*std::acos(0.5)-5*std::sqrt(300.0);for(const auto& r:regions)closeValue(area(r),contourContains(r,circles,{0,0})?lens:100*pi-lens);
    Sketch grid;grid.entities={{EntityKind::Rectangle,{0,0},{20,20},1,"box"},{EntityKind::Line,{-5,10},{25,10},1,"horizontal"},{EntityKind::Line,{10,-5},{10,25},1,"vertical"}};
    regions=closedContours(grid);require(regions.size()==4,"Unsplit crossed dividers must form four bounded cells");for(const auto& r:regions)closeValue(area(r),100);
    Sketch chord;chord.entities={{EntityKind::Circle,{0,0},{},10,"circle"},{EntityKind::Line,{-15,0},{15,0},1,"line"}};regions=closedContours(chord);require(regions.size()==2,"A circle diameter must create two semicircular regions");for(const auto& r:regions)closeValue(area(r),50*pi);
    const auto circleIds=identities(circles);auto moved=circles;for(auto& e:moved.entities){e.first={40-e.first.y,30+e.first.x};}require(identities(moved)==circleIds,"Region IDs must survive rigid rotation across circle seams");std::reverse(moved.entities.begin(),moved.entities.end());require(identities(moved)==circleIds,"Entity order must not change region references");
    auto scaled=circles;for(auto& e:scaled.entities){e.first.x*=1e-4;e.first.y*=1e-4;e.radius*=1e-4;}require(identities(scaled)==circleIds,"Rescaling above kernel tolerance must preserve intersection branch identities");
    const auto left=regionAt(circles,{-12,0}),middle=regionAt(circles,{0,0}),right=regionAt(circles,{12,0});
    for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}){
        circles.plane=plane;closeValue(volume(makeExtrusions(circles,{left,middle},3,Direction::Positive)),300*pi);closeValue(volume(makeExtrusions(circles,{middle,right},3,Direction::Negative)),300*pi);require(bodies(makeExtrusions(circles,{left,middle,right},3,Direction::Positive))==1,"Adjacent curved regions should fuse into one body");
        auto lines=lineRectangle();lines.plane=plane;require(closedContours(lines).size()==3,"Modern line rectangles intersect circles too");
        auto construction=lines;construction.entities.push_back({EntityKind::Line,{7.5,-20},{7.5,5},1,"center-v",true});construction.entities.push_back({EntityKind::Line,{-5,-7.5},{20,-7.5},1,"center-h",true});construction.entities.push_back({EntityKind::Point,{7.5,-7.5},{},1,"point"});require(identities(construction)==identities(lines),"Construction lines and points must not divide extrusion regions");
    }
    circles.plane=SketchPlane::XY;
    auto source=sketch;source.relations.push_back({"origin",RelationKind::Coincident,{"circle",PointRole::Center},{}});SketchDimension diameter;diameter.id="diameter";diameter.kind=DimensionKind::CircleDiameter;diameter.first={"circle",PointRole::Center};diameter.value=20;source.dimensions.push_back(diameter);
    const auto overlapIds=regionAt(source,{3,-3});auto part=thecad::document::PartDocument::createEmpty();const auto sourceId=part.addSketch(source);part.addExtrudeRegions(sourceId,{overlapIds},4,Direction::Positive);closeValue(volume(part.shape()),100*pi);require(part.features()[0].sketch.entities.size()==2 && part.features()[0].sketch.relations.size()==1 && part.features()[0].sketch.dimensions.size()==1,"Region extraction must preserve source geometry, IDs, relations and dimensions");
    QTemporaryDir directory;const auto path=directory.filePath("intersection.thepart");part.save(path);auto restored=thecad::document::PartDocument::load(path);closeValue(volume(restored.shape()),100*pi);require(restored.features()[1].contour==overlapIds,"Save/reopen must retain the exact chosen cell");
    auto edit=restored.features()[0];edit.sketch.dimensions[0].value=22;restored.replaceFeature(edit);closeValue(volume(restored.shape()),121*pi);auto previous=restored.shape();edit=restored.features()[0];edit.sketch.dimensions[0].value=60;reject([&]{restored.replaceFeature(edit);});require(restored.shape().IsSame(previous),"A disappeared intersection region must retain the last valid solid");
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Read saved intersection");const auto good=QJsonDocument::fromJson(file.readAll()).object();file.close();for(int mutation=0;mutation<3;++mutation){auto bad=good;if(mutation==0)bad["version"]=11;else{auto features=bad["features"].toArray();auto f=features[1].toObject();auto refs=f["contour"].toArray();if(mutation==1)refs[0]=refs[0].toString()+"invalid";else refs.append(refs[0]);f["contour"]=refs;features[1]=f;bad["features"]=features;}require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write invalid intersection");file.write(QJsonDocument(bad).toJson());file.close();reject([&]{thecad::document::PartDocument::load(path);});}
    auto cutSketch=sketch;for(auto& e:cutSketch.entities){e.first.x+=25;e.first.y+=25;e.second.x+=25;e.second.y+=25;}const auto cutRegions=closedContours(cutSketch);RegionReferences cutAll;for(const auto& r:cutRegions)cutAll.push_back(r.entityIds);
    auto block=thecad::document::PartDocument::createBox({60,60,10});const auto cutId=block.addSketch(cutSketch);block.addCutRegions(cutId,{regionAt(cutSketch,{28,22})},3,false,Direction::Positive);closeValue(volume(block.shape()),36000-75*pi);block.save(path);closeValue(volume(thecad::document::PartDocument::load(path).shape()),36000-75*pi);
    auto through=thecad::document::PartDocument::createBox({60,60,10});const auto throughId=through.addSketch(cutSketch);through.addCutRegions(throughId,cutAll,3,true,Direction::Positive);closeValue(volume(through.shape()),36000-10*(225+75*pi));through.save(path);closeValue(volume(thecad::document::PartDocument::load(path).shape()),volume(through.shape()));
    auto many=thecad::document::PartDocument::createEmpty();auto bothId=many.addSketch(circles);many.addExtrudeRegions(bothId,{left,right},2,Direction::Positive);closeValue(volume(many.shape()),4*(100*pi-lens));many.save(path);closeValue(volume(thecad::document::PartDocument::load(path).shape()),volume(many.shape()));
    auto t=grid;t.entities.resize(1);t.entities.push_back({EntityKind::Line,{0,10},{20,10},1,"divider"});require(closedContours(t).size()==2,"Unsplit endpoint T-junctions should form regions");
    Sketch open;open.entities={{EntityKind::Line,{-10,0},{10,0},1,"a"},{EntityKind::Line,{0,-10},{0,10},1,"b"}};require(closedContours(open).empty(),"Crossed open lines are not closed extrusion profiles");
    auto duplicate=grid;duplicate.entities.push_back(grid.entities[1]);duplicate.entities.back().id="redundant-divider";
    std::swap(duplicate.entities.back().first, duplicate.entities.back().second);
    require(identities(duplicate)==identities(grid),"A duplicate divider must leave the same cells and references.");
    Sketch rings;rings.entities={{EntityKind::Circle,{0,0},{},20,"outer"},{EntityKind::Circle,{0,0},{},10,"inner"},{EntityKind::Line,{-25,0},{25,0},1,"divider"}};require(closedContours(rings).size()==4,"Intersected nested loops must retain annular and inner cells");closeValue(volume(makeExtrusions(rings,{regionAt(rings,{0,15}),regionAt(rings,{0,-15})},2,Direction::Positive)),600*pi);
    auto hole=sketch;hole.entities.push_back({EntityKind::Circle,{3,-3},{},1,"hole"});require(closedContours(hole).size()==4,"A hole inside an intersection cell stays separately selectable");const auto holed=regionAt(hole,{5,-5});closeValue(volume(makeExtrusions(hole,{holed},2,Direction::Positive)),48*pi);
    auto inclined=cutSketch;FaceAttachment frame;frame.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(1,0,0));inclined.attachment=frame;gp_Trsf transform;transform.SetDisplacement(gp_Ax3(),frame.frame);const auto stock=thecad::document::PartDocument::createBox({60,60,10});const auto inclinedStock=BRepBuilderAPI_Transform(stock.shape(),transform,Standard_True).Shape();closeValue(volume(makeCuts(inclinedStock,inclined,{regionAt(cutSketch,{28,22})},3,false,Direction::Positive)),36000-75*pi);
    std::cout<<"PASS: exact intersection cells, stable references, all planes, volumes, fusion, cuts, regeneration, persistence, construction exclusion and invalid-topology rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
