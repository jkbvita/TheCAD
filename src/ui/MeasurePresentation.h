#pragma once
#include "modeling/Measurement.h"
#include "modeling/Selection.h"
#include <QPointF>
#include <QString>

namespace thecad::ui
{
inline constexpr int defaultMeasureTextPixels = 16;

struct MeasurePick
{
    modeling::SelectionRef ref;
    modeling::MeasureEntity entity;
    QString label;
    TopoDS_Shape highlight;
    bool location = false;
};
enum class MeasureInk
{
    Reading,
    X,
    Y,
    Z,
    Projection,
    Normal
};
struct MeasureGraphic
{
    QString text;
    gp_Pnt anchor;
    std::optional<modeling::MeasureSpan> span;
    MeasureInk ink = MeasureInk::Reading;
    std::optional<std::pair<gp_Dir, gp_Dir>> angle;
};
} // namespace thecad::ui
