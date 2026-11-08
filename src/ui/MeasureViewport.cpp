#include "ui/CadViewport.h"
#include "ui/Theme.h"
#include "modeling/Measurement.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Precision.hxx>

namespace thecad::ui
{
std::vector<TopoDS_Shape> CadViewport::measurementShapes()
{
    const auto picks = selection();
    if (picks.empty())
        return {};
    prepareBodyPicking();
    std::vector<TopoDS_Shape> shapes;
    using Kind = modeling::SelectionKind;
    for (const auto& selected : picks)
    {
        if (selected.generation != m_shapeGeneration || selected.index < 1)
            continue;
        const TopTools_IndexedMapOfShape* map = nullptr;
        switch (selected.kind)
        {
        case Kind::Face: map = &m_selectionFaces; break;
        case Kind::Edge: map = &m_selectionEdges; break;
        case Kind::Vertex: map = &m_selectionVertices; break;
        default: break;
        }
        if (map && selected.index <= map->Extent())
            shapes.push_back(map->FindKey(selected.index));
    }
    return shapes;
}

void CadViewport::showMeasurement(const QString& text, const modeling::Measurement* result)
{
    if (!isReady())
        return;
    const auto context = m_graphics.context();
    if (!m_measureSpan.IsNull())
    {
        context->Remove(m_measureSpan, Standard_False);
        m_measureSpan.Nullify();
    }
    if (!m_measureLabel.IsNull())
    {
        context->Remove(m_measureLabel, Standard_False);
        m_measureLabel.Nullify();
    }
    if (result && result->anchor && !text.isEmpty())
    {
        const bool dark = ThemeManager::instance().isDark();
        const Quantity_Color ink(dark ? Quantity_NOC_WHITE : Quantity_NOC_BLACK);
        const Quantity_Color background(dark ? Quantity_NOC_GRAY15 : Quantity_NOC_GRAY95);
        if (result->span && result->span->first.Distance(result->span->second) > Precision::Confusion())
        {
            BRepBuilderAPI_MakeEdge line(result->span->first, result->span->second);
            if (line.IsDone())
            {
                m_measureSpan = new AIS_Shape(line.Shape());
                m_measureSpan->SetColor(viewportColor(ThemeColor::Accent));
                m_measureSpan->SetWidth(2 * devicePixelRatioF());
                m_measureSpan->SetInfiniteState(Standard_True);
                m_measureSpan->SetZLayer(Graphic3d_ZLayerId_Topmost);
                context->Display(m_measureSpan, AIS_WireFrame, -1, Standard_False);
            }
        }
        m_measureLabel = new AIS_TextLabel;
        m_measureLabel->SetText(TCollection_ExtendedString(text.toUtf8().constData(), Standard_True));
        m_measureLabel->SetPosition(*result->anchor);
        m_measureLabel->SetHeight(15 * devicePixelRatioF());
        m_measureLabel->SetHJustification(Graphic3d_HTA_CENTER);
        m_measureLabel->SetVJustification(Graphic3d_VTA_BOTTOM);
        m_measureLabel->SetColor(ink);
        m_measureLabel->SetColorSubTitle(background);
        m_measureLabel->SetDisplayType(Aspect_TODT_SUBTITLE);
        m_measureLabel->SetInfiniteState(Standard_True);
        m_measureLabel->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(m_measureLabel, 0, -1, Standard_False);
    }
    update();
}
}
