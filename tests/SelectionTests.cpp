#include "modeling/Selection.h"
#include "modeling/SketchTools.h"
#include "modeling/ConstraintSolver.h"
#include "document/PartDocument.h"
#include "document/UndoHistory.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
using namespace thecad::modeling;
using thecad::document::PartDocument;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid input was accepted");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    const SelectionRef face{SelectionKind::Face,"body",{},PointRole::First,1,42},edge{SelectionKind::Edge,"body",{},PointRole::First,2,42},vertex{SelectionKind::Vertex,"body",{},PointRole::First,3,42};
    const SelectionRef back{SelectionKind::Edge,"body",{},PointRole::First,4,42};
    const std::vector<SelectionCandidate> hits{{face,0,1},{edge,3,1},{vertex,5,1},{back,0,10,true},{edge,3,1}};
    SelectionPolicy policy;auto ordered=policy.order(hits);require(ordered.size()==3&&ordered[0].reference==vertex&&ordered[1].reference==edge,"Visible points/edges must precede faces with deduplication");
    policy.filters=FilterEdges;ordered=policy.order(hits);require(ordered.size()==1&&ordered[0].reference==edge,"Filter must exclude faces, vertices and occluded edges");
    ordered=policy.order(hits,true);require(ordered.size()==2&&ordered[1].reference==back,"Select Other explicitly includes hidden alternatives");
    policy.enabled=false;require(policy.order(hits).size()==3,"F6 bypasses chosen filters without losing them");policy.enabled=true;require(policy.filters==FilterEdges,"Filter mask must survive temporary disable");
    policy.filters=FilterFaces|FilterReferences;require(policy.allows(SelectionKind::Face)&&policy.allows(SelectionKind::ReferenceAxis)&&!policy.allows(SelectionKind::Dimension),"Combined filters use union semantics");
    SelectionModel selected;selected.select(edge);selected.select(vertex,true);require(selected.items().size()==2,"Additive selection retains earlier types");selected.select(edge,true);require(selected.items().size()==1&&selected.items()[0]==vertex,"Additive repeat toggles one entity");
    selected.select({SelectionKind::ReferencePlane,"datum"},true);selected.invalidateTopology(43);require(selected.items().size()==1&&selected.items()[0].featureId=="datum","Regeneration invalidates topology indices but retains stable datum IDs");selected.clear();require(selected.items().empty(),"Empty click clears selection");
    Sketch sketch;sketch.entities={{EntityKind::Line,{-20,-20},{20,-20},1,"line"},{EntityKind::Circle,{10,10},{},5,"circle"},{EntityKind::Point,{0,0},{},1,"point"}};
    sketch.relations={{"horizontal",RelationKind::Horizontal,{"line",PointRole::First},{"line",PointRole::Second}}};
    SketchDimension radius;radius.id="radius";radius.kind=DimensionKind::CircleRadius;radius.first={"circle",PointRole::Center};radius.value=5;sketch=setSketchDimension(sketch,radius);
    const std::vector<std::string> curves{"line","circle"};require(constructionState(sketch,curves)==ConstructionState::Normal,"Normal selection state");
    const auto mixed=setSketchConstruction(sketch,{"line"},true);require(constructionState(mixed,curves)==ConstructionState::Mixed,"Mixed construction states must be explicit");
    const auto construction=setSketchConstruction(mixed,curves,true);require(constructionState(construction,curves)==ConstructionState::Construction,"Selected curves convert together");
    require(construction.entities[0].id==sketch.entities[0].id&&construction.entities[1].radius==5&&construction.relations[0].id=="horizontal"&&construction.dimensions[0].id=="radius","Conversion preserves identity, geometry, dimensions and relations");
    require(closedContours(construction).empty(),"Construction circle and lines must not form extrusion profiles");
    require(closedContours(setSketchConstruction(construction,curves,false)).size()==1,"Restoring normal circle restores its profile");
    rejects([&]{setSketchConstruction(sketch,{"missing"},true);});rejects([&]{setSketchConstruction(sketch,{"point"},false);});
    thecad::document::UndoHistory<Sketch> history;history.record(sketch);auto undone=history.undoTarget();history.commitUndo(construction);require(!undone.entities[0].construction&&history.redoTarget().entities[1].construction,"Construction history restores exact flags");
    QTemporaryDir directory;const auto path=directory.filePath("selection.thepart");auto doc=PartDocument::createEmpty();const auto sketchId=doc.addSketch(construction);DatumDefinition plane;plane.method=DatumMethod::PlaneOffset;GeometryReference origin;origin.source=ReferenceSource::BasePlane;origin.index=0;plane.references={origin};plane.value=10;const auto planeId=doc.addReferenceGeometry(plane);
    auto feature=doc.features().back();feature.referenceVisible=false;doc.replaceFeature(feature);const auto attached=doc.sketchOnReferencePlane(planeId);require(attached.referencePlaneId==planeId,"Hidden references remain valid downstream supports");doc.save(path);
    const auto reopened=PartDocument::load(path);require(reopened.features()[0].id==sketchId&&reopened.features()[0].sketch.entities[1].construction&&reopened.features()[0].sketch.dimensions[0].id=="radius"&&!reopened.features()[1].referenceVisible,"Construction flags, constraints and datum visibility persist");
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Read current fixture");auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();auto features=root["features"].toArray();auto old=features[1].toObject();old.remove("visible");auto def=old["definition"].toObject();auto refs=def["references"].toArray();for(int n=0;n<refs.size();++n){auto r=refs[n].toObject();r.remove("topology");refs[n]=r;}def["references"]=refs;old["definition"]=def;features[1]=old;root["features"]=features;root["version"]=13;require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write legacy fixture");file.write(QJsonDocument(root).toJson());file.close();require(PartDocument::load(path).features()[1].referenceVisible,"Schema 13 reference geometry defaults to visible");
    std::cout<<"PASS: typed selection, priorities, filters, occlusion, topology invalidation, construction conversion/history/persistence, hidden datum validity and schema compatibility\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
