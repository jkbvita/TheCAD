#include "modeling/FeaturePreview.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <memory>
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

TopAbs_State treatmentSide(const TopoDS_Face& face,
                           const std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>>& stock)
{
    const double tolerance = Precision::Confusion();
    double u0, u1, v0, v1;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const BRepAdaptor_Surface surface(face);
    for (const double u : {0.5, 0.25, 0.75, 0.125, 0.875}) {
        for (const double v : {0.5, 0.25, 0.75, 0.125, 0.875}) {
            const gp_Pnt2d uv(u0 + (u1-u0)*u, v0 + (v1-v0)*v);
            if (BRepClass_FaceClassifier(face, uv, tolerance).State() != TopAbs_IN) continue;
            const auto point = surface.Value(uv.X(), uv.Y());
            bool outside = !stock.empty();
            for (const auto& classifier : stock) {
                classifier->Perform(point, tolerance);
                if (classifier->State() == TopAbs_IN) return TopAbs_IN;
                outside &= classifier->State() == TopAbs_OUT;
            }
            if (outside) return TopAbs_OUT;
        }
    }
    return TopAbs_UNKNOWN;
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

MaterialChanges edgeTreatmentHighlights(const TopoDS_Shape& stock, const std::vector<TopoDS_Shape>& faces)
{
    if (stock.IsNull() || faces.empty()) return {};
    std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>> classifiers;
    for (TopExp_Explorer solids(stock, TopAbs_SOLID); solids.More(); solids.Next())
        classifiers.push_back(std::make_unique<BRepClass3d_SolidClassifier>(solids.Current()));

    BRep_Builder builder;
    TopoDS_Compound added, removed;
    builder.MakeCompound(added);
    builder.MakeCompound(removed);
    for (const auto& face : faces) {
        try {
            const auto side = treatmentSide(TopoDS::Face(face), classifiers);
            if (side == TopAbs_OUT) builder.Add(added, face);
            if (side == TopAbs_IN) builder.Add(removed, face);
        } catch (const Standard_Failure&) {
            // A classification failure only omits this color overlay.
        }
    }
    return {added.NbChildren() ? TopoDS_Shape(added) : TopoDS_Shape{},
            removed.NbChildren() ? TopoDS_Shape(removed) : TopoDS_Shape{}};
}
}
