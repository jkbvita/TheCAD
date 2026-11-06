#include "viewport/DimensionPresenter.h"
#include "modeling/DimensionAnnotation.h"
#include "modeling/ConstraintSolver.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <TCollection_ExtendedString.hxx>
#include <algorithm>
#include <cmath>
namespace thecad::viewport
{
QString DimensionPresenter::text(const modeling::Sketch& sketch, const modeling::SketchDimension& d)
{
    const QString prefix = d.kind == modeling::DimensionKind::CircleRadius ? "R"
                           : d.kind == modeling::DimensionKind::CircleDiameter
                               ? QString::fromUtf8("Ø")
                               : "";
    const double value = d.driving ? d.value : modeling::dimensionMeasurement(sketch, d);
    const QString valueText =
        prefix + QString::number(value, 'g', 6) +
        (d.kind == modeling::DimensionKind::LineAngle ? QString::fromUtf8("°") : " mm");
    return d.driving ? valueText : "(" + valueText + ")";
}
void DimensionPresenter::clear(const Handle(AIS_InteractiveContext) & context)
{
    if (!context.IsNull())
    {
        for (auto& label : m_labels)
            context->Remove(label, Standard_False);
        for (auto& guide : m_guides)
            context->Remove(guide, Standard_False);
    }
    m_labels.clear();
    m_guides.clear();
}
void DimensionPresenter::rebuild(const Handle(AIS_InteractiveContext) & context,
                                 const modeling::Sketch& sketch, double unit, double ratio,
                                 const DimensionVisualState& state)
{
    clear(context);
    m_unit = unit;
    const auto label =
        [&](modeling::Point2 position, const QString& value, const Quantity_Color& color)
    {
        Handle(AIS_TextLabel) item = new AIS_TextLabel;
        item->SetText(TCollection_ExtendedString(value.toStdWString().c_str()));
        item->SetFont("Arial");
        item->SetHJustification(Graphic3d_HTA_LEFT);
        item->SetVJustification(Graphic3d_VTA_BOTTOM);
        item->SetPosition(modeling::sketchPoint(sketch, position));
        item->SetColor(color);
        item->SetHeight(14 * ratio);
        item->SetZLayer(Graphic3d_ZLayerId_Topmost);
        item->SetInfiniteState(Standard_True);
        context->Display(item, 0, -1, Standard_False);
        m_labels.push_back(item);
    };
    const auto guide = [&](modeling::Point2 a, modeling::Point2 b, const Quantity_Color& color)
    {
        if (std::hypot(a.x - b.x, a.y - b.y) < 1e-7)
            return;
        Handle(AIS_Shape) item =
            new AIS_Shape(BRepBuilderAPI_MakeEdge(modeling::sketchPoint(sketch, a),
                                                  modeling::sketchPoint(sketch, b))
                              .Shape());
        item->SetColor(color);
        item->SetWidth(ratio);
        item->SetZLayer(Graphic3d_ZLayerId_Topmost);
        item->SetInfiniteState(Standard_True);
        context->Display(item, AIS_WireFrame, -1, Standard_False);
        m_guides.push_back(item);
    };
    auto dimensions = sketch.dimensions;
    if (state.preview)
        dimensions.push_back(*state.preview);
    for (const auto& d : dimensions)
    {
        const bool entitySelected =
            d.first.entityId == state.selectedEntity || d.second.entityId == state.selectedEntity;
        const bool error =
            std::find(state.errors.begin(), state.errors.end(), d.id) != state.errors.end();
        const bool selected =
            std::find(state.selected.begin(), state.selected.end(), d.id) != state.selected.end();
        auto color = error                         ? state.error
                     : d.id == state.hovered       ? state.hover
                     : d.id == "pending-dimension" ? state.underdefined
                                                   : state.defined;
        if (!error && (selected || entitySelected))
            color = state.selection;
        const auto graphics = modeling::dimensionGraphics(sketch, d, unit);
        label(graphics.text, text(sketch, d), color);
        for (const auto& segment : graphics.segments)
            guide(segment.first, segment.second, color);
    }
}
} // namespace thecad::viewport
