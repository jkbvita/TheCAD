#include "presentation/SolidPresentation.h"
#include "presentation/SelectionAppearance.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <gp_Cone.hxx>
#include <Graphic3d_ArrayOfPrimitives.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <Precision.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <StdPrs_WFShape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <array>
#include <vector>

namespace thecad::presentation
{
namespace
{
void shadeConeTip(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Cone)
        return;
    TopLoc_Location location;
    const auto mesh = BRep_Tool::Triangulation(face, location);
    if (mesh.IsNull())
        return;
    const auto apex = surface.Cone().Apex().Transformed(location.Transformation().Inverted());
    const int nodeCount = mesh->NbNodes();
    std::vector<bool> isTip(nodeCount + 1, false);
    bool hasTip = false;
    for (int i = 1; i <= nodeCount; ++i)
    {
        isTip[i] = mesh->Node(i).SquareDistance(apex) <= Precision::SquareConfusion();
        hasTip |= isTip[i];
    }
    if (!hasTip)
        return;

    StdPrs_ToolTriangulatedShape::ComputeNormals(face, mesh);
    struct Corner
    {
        int triangle, original, replacement;
        std::array<int, 3> nodes;
        gp_Dir normal;
    };
    std::vector<Corner> corners;
    std::vector<bool> assigned(nodeCount + 1, false);
    int nextNode = nodeCount;
    for (int triangle = 1; triangle <= mesh->NbTriangles(); ++triangle)
    {
        std::array<int, 3> nodes;
        mesh->Triangle(triangle).Get(nodes[0], nodes[1], nodes[2]);
        int tip = -1, tips = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (isTip[nodes[i]])
            {
                tip = i;
                ++tips;
            }
        }
        if (tips != 1)
            continue;
        const auto normal =
            gp_Vec(mesh->Normal(nodes[(tip + 1) % 3])) + gp_Vec(mesh->Normal(nodes[(tip + 2) % 3]));
        if (normal.SquareMagnitude() <= Precision::SquareConfusion())
            continue;
        const int original = nodes[tip];
        const int replacement = assigned[original] ? ++nextNode : original;
        assigned[original] = true;
        nodes[tip] = replacement;
        corners.push_back({triangle, original, replacement, nodes, gp_Dir(normal)});
    }
    // A cone tip has no unique normal. Give each triangle at the tip the
    // limiting normal of its own sector, keeping its exact position.
    if (nextNode != nodeCount)
        mesh->ResizeNodes(nextNode, true);
    for (const auto& corner : corners)
    {
        if (corner.replacement != corner.original)
        {
            mesh->SetNode(corner.replacement, mesh->Node(corner.original));
            if (mesh->HasUVNodes())
                mesh->SetUVNode(corner.replacement, mesh->UVNode(corner.original));
            mesh->SetTriangle(corner.triangle,
                              Poly_Triangle(corner.nodes[0], corner.nodes[1], corner.nodes[2]));
        }
        mesh->SetNormal(corner.replacement, corner.normal);
    }
}

class SolidPresentation final : public AIS_Shape
{
public:
    explicit SolidPresentation(const TopoDS_Shape& shape) : AIS_Shape(shape)
    {
        SetMaterial(Graphic3d_NOM_PLASTIC);
        SetColor(modelGray());
        Attributes()->ShadingAspect()->Aspect()->SetShadingModel(Graphic3d_TOSM_FRAGMENT);
        Attributes()->SetDeviationCoefficient(0.0001);
        Attributes()->SetDeviationAngle(0.05);
        Attributes()->SetFaceBoundaryDraw(false);
        Attributes()->SetAutoTriangulation(false);
    }

protected:
    void Compute(const Handle(PrsMgr_PresentationManager) & manager,
                 const Handle(Prs3d_Presentation) & presentation, int mode) override
    {
        if (mode != AIS_Shaded)
        {
            AIS_Shape::Compute(manager, presentation, mode);
            return;
        }
        // A face highlight shares the body's mesh. Keep its boundary samples
        // aligned with the neighboring faces when highlighting it.
        if (Shape().ShapeType() != TopAbs_FACE ||
            !StdPrs_ToolTriangulatedShape::IsTriangulated(Shape()))
            StdPrs_ToolTriangulatedShape::Tessellate(Shape(), Attributes());
        for (TopExp_Explorer faces(Shape(), TopAbs_FACE); faces.More(); faces.Next())
            shadeConeTip(TopoDS::Face(faces.Current()));
        AIS_Shape::Compute(manager, presentation, mode);
        const auto edges = StdPrs_WFShape::AddAllEdges(selectionOutline(Shape()), Attributes());
        if (!edges.IsNull())
        {
            const auto group = presentation->NewGroup();
            group->SetGroupPrimitivesAspect(Attributes()->FaceBoundaryAspect()->Aspect());
            group->AddPrimitiveArray(edges);
        }
    }
};
} // namespace

Handle(AIS_Shape) makeSolidPresentation(const TopoDS_Shape& shape)
{
    return new SolidPresentation(shape);
}
} // namespace thecad::presentation
