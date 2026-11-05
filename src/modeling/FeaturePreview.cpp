#include "modeling/FeaturePreview.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>
#include <stdexcept>
namespace thecad::modeling {
namespace {
TopoDS_Shape difference(const TopoDS_Shape& first,const TopoDS_Shape& second){
    BRepAlgoAPI_Cut operation;operation.SetNonDestructive(true);
    TopTools_ListOfShape arguments,tools;arguments.Append(first);tools.Append(second);
    operation.SetArguments(arguments);operation.SetTools(tools);operation.Build();
    if(!operation.IsDone())throw std::runtime_error("Cannot display material changes for this preview.");
    const auto result=operation.Shape();
    return TopExp_Explorer(result,TopAbs_SOLID).More()?result:TopoDS_Shape{};
}
}
MaterialChanges materialChanges(const TopoDS_Shape& before,const TopoDS_Shape& after){
    try{
    if(before.IsNull())return {after,{}};
    if(after.IsNull())return {{},before};
    if(before.IsSame(after))return {};
    return {difference(after,before),difference(before,after)};
    }catch(const Standard_Failure& error){throw std::runtime_error(std::string("Cannot display material changes: ")+error.GetMessageString());}
}
}
