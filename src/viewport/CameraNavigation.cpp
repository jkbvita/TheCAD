#include "viewport/CameraNavigation.h"
#include <Graphic3d_Camera.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <algorithm>
namespace thecad::viewport
{
CameraNavigation::Mode CameraNavigation::modeFor(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(Qt::ControlModifier) ? Mode::Pan
           : modifiers.testFlag(Qt::ShiftModifier) ? Mode::Zoom
                                                   : Mode::Orbit;
}
void CameraNavigation::begin(Qt::KeyboardModifiers modifiers, QPointF logical, QPoint physical,
                             gp_Pnt pivot, gp_Pnt zoomDepth)
{
    m_mode = modeFor(modifiers);
    m_position = logical;
    m_physical = physical;
    m_pivot = pivot;
    m_zoomDepth = zoomDepth;
    if (m_mode == Mode::Orbit)
        m_anchor = pivot;
}
void CameraNavigation::move(const Handle(V3d_View) & view, QPointF logical, QPoint physical,
                            QSize viewport, std::optional<gp_Pnt> zoomDepth)
{
    const auto delta = logical - m_position;
    if (m_mode == Mode::Orbit)
    {
        const auto camera = view->Camera();
        const auto up = camera->Up(),
                   right = gp_Dir(gp_Vec(camera->Direction()).Crossed(gp_Vec(up)));
        const double speed =
            3.141592653589793 / std::max(240, std::min(viewport.width(), viewport.height()));
        gp_Trsf yaw, pitch;
        yaw.SetRotation(gp_Ax1(m_pivot, up), -delta.x() * speed);
        pitch.SetRotation(gp_Ax1(m_pivot, right), -delta.y() * speed);
        camera->Transform(pitch * yaw);
    }
    else if (m_mode == Mode::Pan)
    {
        const auto change = physical - m_physical;
        view->Pan(change.x(), -change.y());
    }
    else if (m_mode == Mode::Zoom)
        zoom(view, m_physical, std::exp(std::clamp(-delta.y() * 0.012, -2.0, 2.0)),
             zoomDepth.value_or(m_zoomDepth));
    m_physical = physical;
    m_position = logical;
}
void CameraNavigation::zoomCentered(const Handle(V3d_View) & view, double factor)
{
    if (view.IsNull() || !std::isfinite(factor) || factor <= 0)
        return;
    const auto camera = view->Camera();
    camera->SetScale(std::clamp(camera->Scale() / factor, 1e-6, 1e12));
}
void CameraNavigation::zoom(const Handle(V3d_View) & view, QPoint pixel, double factor,
                            gp_Pnt depth)
{
    if (view.IsNull() || !std::isfinite(factor) || factor <= 0)
        return;
    const auto camera = view->Camera();
    const double next = std::clamp(camera->Scale() / factor, 1e-6, 1e12);
    const auto planePoint = [&](gp_Pnt planeOrigin)
    {
        double x, y, z, dx, dy, dz;
        view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
        const gp_Vec ray(dx, dy, dz), normal(camera->Direction());
        const double dot = ray.Dot(normal);
        if (std::abs(dot) < 1e-12)
            return planeOrigin;
        const gp_Pnt p(x, y, z);
        return p.Translated(ray * (gp_Vec(p, planeOrigin).Dot(normal) / dot));
    };
    const auto anchor = planePoint(depth);
    camera->SetScale(next);
    const auto after = planePoint(anchor);
    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(after, anchor));
    camera->Transform(translation);
}
} // namespace thecad::viewport
