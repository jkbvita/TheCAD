#include "ui/CadViewport.h"
#include "ui/Theme.h"
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
namespace
{
Quantity_Color inkColor(MeasureInk ink, bool dark)
{
    switch (ink)
    {
    case MeasureInk::X:
        return dark ? Quantity_Color(.99, .43, .41, Quantity_TOC_sRGB)
                    : Quantity_Color(.74, .12, .13, Quantity_TOC_sRGB);
    case MeasureInk::Y:
        return dark ? Quantity_Color(.49, .91, .49, Quantity_TOC_sRGB)
                    : Quantity_Color(.05, .48, .13, Quantity_TOC_sRGB);
    case MeasureInk::Z:
        return dark ? Quantity_Color(.47, .72, 1., Quantity_TOC_sRGB)
                    : Quantity_Color(.08, .31, .78, Quantity_TOC_sRGB);
    case MeasureInk::Projection:
        return Quantity_Color(dark ? .98 : .64, dark ? .81 : .48, .12, Quantity_TOC_sRGB);
    case MeasureInk::Normal:
        return Quantity_Color(dark ? .82 : .49, .37, dark ? .99 : .67, Quantity_TOC_sRGB);
    default:
        return Quantity_Color(dark ? Quantity_NOC_WHITE : Quantity_NOC_BLACK);
    }
}
} // namespace
void CadViewport::showMeasurement(std::vector<MeasureGraphic> graphics, int textPixels)
{
    m_measureGraphics = std::move(graphics);
    m_measureTextPixels = textPixels;
    m_measureOffsets.resize(m_measureGraphics.size());
    renderMeasurement();
    update();
}
void CadViewport::measureCameraUpdate()
{
    auto cam = m_graphics.view()->Camera();
    if (m_measureViewStamp == cam->WorldViewState() &&
        m_measureProjectionStamp == cam->ProjectionState() && m_measureViewSize == size())
        return;
    m_measureViewStamp = cam->WorldViewState();
    m_measureProjectionStamp = cam->ProjectionState();
    m_measureViewSize = size();
    renderMeasurement();
    emit measureViewChanged();
}
void CadViewport::renderMeasurement()
{
    if (!isReady())
        return;
    const auto ctx = m_graphics.context();
    const auto view = m_graphics.view();
    for (const auto& h : m_measureDecorations)
        ctx->Remove(h, false);
    m_measureDecorations.clear();
    m_measureSpan.Nullify();
    m_measureLabel.Nullify();
    m_measureLabelBounds.clear();
    if (m_measureGraphics.empty())
        return;
    const double ratio = devicePixelRatioF();
    const bool dark = ThemeManager::instance().isDark();
    std::vector<Handle(AIS_TextLabel)> labels;
    auto display = [&](Handle(AIS_InteractiveObject) h, int mode)
    {
        h->SetInfiniteState(true);
        h->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->Display(h, mode, -1, false);
        m_measureDecorations.push_back(h);
    };
    auto line = [&](const gp_Pnt& a, const gp_Pnt& b, Quantity_Color c, bool dashed = false)
    {
        Handle(AIS_Shape) h;
        if (a.Distance(b) <= Precision::Confusion())
            return h;
        BRepBuilderAPI_MakeEdge builder(a, b);
        if (!builder.IsDone())
            return h;
        h = new AIS_Shape(builder.Edge());
        h->SetColor(c);
        h->SetWidth(ratio * (dashed ? 1. : 1.6));
        if (dashed)
            h->Attributes()->SetLineAspect(new Prs3d_LineAspect(c, Aspect_TOL_DASH, ratio));
        display(h, AIS_WireFrame);
        return h;
    };
    const gp_Vec normal(view->Camera()->Direction());
    auto atPixel = [&](QPointF pixel, const gp_Pnt& depth)
    {
        double x, y, z, dx, dy, dz;
        view->ConvertWithProj(qRound(pixel.x() * ratio), qRound(pixel.y() * ratio), x, y, z, dx, dy,
                              dz);
        gp_Pnt p(x, y, z);
        gp_Vec ray(dx, dy, dz);
        return p.Translated(ray * (gp_Vec(p, depth).Dot(normal) / ray.Dot(normal)));
    };
    QFont font("Arial");
    font.setPixelSize(m_measureTextPixels);
    QFontMetricsF metrics(font);
    for (size_t i = 0; i < m_measureGraphics.size(); ++i)
    {
        const auto& g = m_measureGraphics[i];
        const auto color = inkColor(g.ink, dark);
        gp_Pnt anchor = g.anchor;
        if (g.span)
        {
            const auto& [a, b] = *g.span;
            auto h =
                line(a, b, color, g.ink == MeasureInk::Projection || g.ink == MeasureInk::Normal);
            if (i == 0)
                m_measureSpan = h;
            auto pa = projectWorldPoint(a), pb = projectWorldPoint(b);
            QLineF screen(pa, pb);
            for (const auto& end : {a, b})
            {
                Handle(AIS_Shape) dot = new AIS_Shape(BRepBuilderAPI_MakeVertex(end).Vertex());
                dot->Attributes()->SetPointAspect(
                    new Prs3d_PointAspect(Aspect_TOM_POINT, color, 4 * ratio));
                display(dot, AIS_WireFrame);
            }
            if (screen.length() > 18)
            {
                auto delta = (pb - pa) / screen.length();
                auto side = QPointF(-delta.y(), delta.x()) * 2.8;
                for (double s : {-1., 1.})
                {
                    auto p = s > 0 ? pa : pb;
                    auto end = s > 0 ? a : b;
                    for (double sign : {-1., 1.})
                        line(end, atPixel(p + delta * (s * 7.) + side * sign, end), color);
                }
            }
        }
        if (g.angle)
        {
            auto u = gp_Vec(g.angle->first), v = gp_Vec(g.angle->second);
            auto angle = u.Angle(v);
            auto tangent = v - u * v.Dot(u);
            if (tangent.Magnitude() > 1e-8)
            {
                tangent.Normalize();
                const double radius = 26. * view->Camera()->Scale() / std::max(1, height());
                auto at = [&](double t)
                { return g.anchor.Translated((u * std::cos(t) + tangent * std::sin(t)) * radius); };
                line(g.anchor, at(0), color, true);
                line(g.anchor, at(angle), color, true);
                for (int n = 1; n <= 16; ++n)
                    line(at(angle * (n - 1) / 16), at(angle * n / 16), color);
                anchor = at(angle / 2);
            }
        }
        const auto pixel = projectWorldPoint(anchor);
        const auto text = metrics.elidedText(g.text, Qt::ElideRight, std::max(1, width() - 20));
        QSizeF extent(metrics.horizontalAdvance(text) + 12, metrics.height() + 6);
        auto pos = pixel + QPointF(24, -35) + m_measureOffsets[i];
        auto clamp = [&](QPointF p)
        {
            return QPointF(std::clamp(p.x(), 3., std::max(3., width() - extent.width() - 4)),
                           std::clamp(p.y(), 3., std::max(3., height() - extent.height() - 4)));
        };
        QRectF rect(clamp(pos), extent);
        if (m_measureOffsets[i].isNull())
        {
            for (size_t n = 0; n < m_measureGraphics.size() &&
                               std::any_of(
                                   m_measureLabelBounds.begin(), m_measureLabelBounds.end(),
                                   [&](auto r) { return r.intersects(rect); });
                 ++n)
                rect.moveTopLeft(clamp(pos + QPointF(0, (extent.height() + 2) * (n + 1))));
        }
        m_measureLabelBounds.push_back(rect);
        auto leader = QPointF(std::clamp(pixel.x(), rect.left(), rect.right()),
                              std::clamp(pixel.y(), rect.top(), rect.bottom()));
        line(anchor, atPixel(leader, anchor), color, true);
        Handle(AIS_TextLabel) label = new AIS_TextLabel;
        label->SetText(TCollection_ExtendedString(text.toUtf8().constData(), true));
        label->SetFont("Arial");
        label->SetHeight(m_measureTextPixels * ratio);
        label->SetPosition(atPixel(rect.center(), anchor));
        label->SetHJustification(Graphic3d_HTA_CENTER);
        label->SetVJustification(Graphic3d_VTA_CENTER);
        label->SetColor(color);
        label->SetColorSubTitle(Quantity_Color(dark ? Quantity_NOC_GRAY15 : Quantity_NOC_WHITE));
        label->SetDisplayType(Aspect_TODT_SUBTITLE);
        label->SetZLayer(Graphic3d_ZLayerId_Topmost);
        labels.push_back(label);
        if (i == 0)
            m_measureLabel = label;
    }
    // Draw text after every dimension line, so later component spans cannot
    // paint across labels belonging to earlier measurements.
    for (const auto& label : labels)
        display(label, 0);
}
} // namespace thecad::ui
