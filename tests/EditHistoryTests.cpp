#include "document/UndoHistory.h"
#include "modeling/DimensionInference.h"
#include "modeling/ConstraintSolver.h"
#include <iostream>
#include <stdexcept>
using namespace thecad;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    document::UndoHistory<int> history(10);int current=0;
    for(int i=0;i<15;++i){history.record(current);++current;}
    for(int i=0;i<10;++i){const auto target=history.undoTarget();history.commitUndo(current);current=target;}
    require(current==5 && !history.canUndo() && history.canRedo(),"Exactly ten retained actions");
    for(int i=0;i<10;++i){const auto target=history.redoTarget();history.commitRedo(current);current=target;}
    require(current==15 && !history.canRedo(),"Redo restores exact state");
    auto target=history.undoTarget();history.commitUndo(current);current=target;history.record(current);current=100;
    require(!history.canRedo(),"New edit clears abandoned future");history.clear();require(!history.canUndo() && !history.canRedo(),"New document clears history");
    using namespace modeling;Sketch sketch;sketch.entities={{EntityKind::Line,{0,0},{30,0},1,"line"},{EntityKind::Line,{0,10},{30,10},1,"other"},{EntityKind::Circle,{50,20},{},5,"circle"}};
    auto length=inferDimension(sketch,{}, {"line"},{15,-10});length.id="length";
    auto spacing=inferDimension(sketch,{}, {"line","other"},{40,5});spacing.id="spacing";spacing.driving=false;
    auto diameter=inferDimension(sketch,{}, {"circle"},{65,25});diameter.id="diameter";
    sketch.dimensions={length,spacing,diameter};sketch.relations={{"horizontal",RelationKind::Horizontal,{"line",PointRole::First},{"line",PointRole::Second}}};
    const auto deleted=removeSketchEntity(sketch,"line");require(deleted.entities.size()==2 && deleted.dimensions.size()==1 && deleted.relations.empty(),"Geometry deletion cleans all attached dimensions and relations");
    require(deleted.dimensions[0].id=="diameter" && sketch.dimensions.size()==3,"Unrelated dimensions and source sketch preserved");validateSketchRelations(deleted);
    auto undimensioned=removeSketchDimension(sketch,"length");require(undimensioned.entities.size()==3 && undimensioned.dimensions.size()==2,"Dimension deletion preserves entities");
    require(undimensioned.entities[0].second.x==30,"Removing a dimension does not resize geometry");
    auto circleDeleted=removeSketchEntity(sketch,"circle");require(circleDeleted.dimensions.size()==2,"Circle dimension cleanup");validateSketchRelations(circleDeleted);
    std::cout<<"PASS: ten-action limit, ordered undo/redo, branching/reset, dimensioned geometry cleanup and independent dimension removal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
