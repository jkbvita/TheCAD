#pragma once
#include "modeling/Sketch.h"
namespace thecad::modeling {
const char* relationName(RelationKind kind);
std::vector<RelationKind> availableRelations(const Sketch&,const std::vector<std::string>& entities);
// A batch is solved once; callers accept it as one edit/history operation.
Sketch addSelectionRelations(const Sketch&,RelationKind,const std::vector<std::string>& entities,const std::string& idPrefix);
bool isNewRelation(RelationKind kind);
void validateNewRelation(const Sketch&,const SketchRelation&,bool geometry);
}
