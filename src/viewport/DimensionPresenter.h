#pragma once
#include "modeling/Sketch.h"
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <QString>
namespace thecad::viewport
{
struct DimensionVisualState
{
    std::optional<modeling::SketchDimension> preview;
    std::string selectedEntity, hovered;
    std::vector<std::string> selected, errors;
    Quantity_Color defined, underdefined, error, selection, hover;
};
class DimensionPresenter final
{
public:
    static QString text(const modeling::Sketch& sketch, const modeling::SketchDimension& dimension);
    void clear(const Handle(AIS_InteractiveContext) & context = {});
    void rebuild(const Handle(AIS_InteractiveContext) & context, const modeling::Sketch& sketch,
                 double unit, double pixelRatio, const DimensionVisualState& state);
    double unit() const { return m_unit; }
    const std::vector<Handle(AIS_TextLabel)>& labels() const { return m_labels; }
    const std::vector<Handle(AIS_Shape)>& guides() const { return m_guides; }

private:
    double m_unit = 0;
    std::vector<Handle(AIS_TextLabel)> m_labels;
    std::vector<Handle(AIS_Shape)> m_guides;
};
} // namespace thecad::viewport
