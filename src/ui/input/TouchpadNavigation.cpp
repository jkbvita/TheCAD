#include "ui/input/TouchpadNavigation.h"
#include "ui/input/NavigationInputSettings.h"
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <Graphic3d_Camera.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
using Camera = viewport::CameraNavigation;
TouchpadNavigation::TouchpadNavigation(NavigationInputSettings* settings)
    : m_settings(settings ? settings : &NavigationInputSettings::instance())
{
}
void TouchpadNavigation::reset()
{
    m_pointer.stop();
    m_scroll.stop();
    m_scrollTime.invalidate();
    m_panRemainder = {};
}
bool TouchpadNavigation::usesTouchpad(const QPointingDevice* device) const
{
    if (m_settings->mode() == NavigationInputMode::Mouse)
        return false;
    return m_settings->touchpadEnabled() ||
           (device && device->type() == QInputDevice::DeviceType::TouchPad);
}
bool TouchpadNavigation::move(QMouseEvent* event, const Handle(V3d_View) & view, QSize size,
                              double ratio, bool allowed, const Pivot& pivot)
{
    const auto modifiers = event->modifiers();
    const auto mode = modifiers.testFlag(Qt::ControlModifier) ? Camera::Mode::Pan
                      : modifiers.testFlag(Qt::ShiftModifier) ? Camera::Mode::Orbit
                                                              : Camera::Mode::None;
    // No button grabs: selection clicks and sketch/component drags retain priority.
    if (!allowed || view.IsNull() || !m_settings->touchpadEnabled() ||
        event->buttons() != Qt::NoButton || mode == Camera::Mode::None ||
        modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::MetaModifier))
    {
        m_pointer.stop();
        return false;
    }
    const auto logical = event->position();
    const QPoint physical(qRound(logical.x() * ratio), qRound(logical.y() * ratio));
    if (m_pointer.mode() != mode)
        m_pointer.begin(mode, logical, physical, mode == Camera::Mode::Orbit ? pivot() : gp_Pnt(),
                        view->Camera()->Center());
    else
        m_pointer.move(view, logical, physical, size);
    event->accept();
    return true;
}
void TouchpadNavigation::pan(const Handle(V3d_View) & view, QPointF logical, double ratio)
{
    const auto delta = logical * ratio + m_panRemainder;
    const QPoint pixels(qRound(delta.x()), qRound(delta.y()));
    m_panRemainder = delta - pixels;
    view->Pan(pixels.x(), -pixels.y());
}
bool TouchpadNavigation::wheel(QWheelEvent* event, const Handle(V3d_View) & view, QSize size,
                               double ratio, const Pivot& pivot, const Zoom& zoom)
{
    if (view.IsNull() || !usesTouchpad(event->pointingDevice()))
        return false;
    // Windows may translate a pinch to Ctrl+wheel; native pinch is handled below.
    const QPointF delta = !event->pixelDelta().isNull() ? QPointF(event->pixelDelta())
                                                        : QPointF(event->angleDelta()) / 3.0;
    if (event->phase() == Qt::ScrollBegin)
    {
        m_scroll.stop();
        m_panRemainder = {};
    }
    if (!delta.isNull())
    {
        if (event->modifiers().testFlag(Qt::ControlModifier))
        {
            zoom(event->position(), std::exp(std::clamp(delta.y() * 0.004, -2.0, 2.0)));
            m_scroll.stop();
        }
        else if (event->modifiers().testFlag(Qt::ShiftModifier))
        {
            if (!m_scrollTime.isValid() || m_scrollTime.elapsed() > 250 ||
                m_scroll.mode() != Camera::Mode::Orbit)
                m_scroll.begin(Camera::Mode::Orbit, {}, {}, pivot(), view->Camera()->Center());
            const auto position = m_scroll.position() + delta;
            m_scroll.move(view, position,
                          {qRound(position.x() * ratio), qRound(position.y() * ratio)}, size);
        }
        else
        {
            pan(view, delta, ratio);
            m_scroll.stop();
        }
        m_scrollTime.restart();
    }
    if (event->phase() == Qt::ScrollEnd)
    {
        m_scroll.stop();
        m_scrollTime.invalidate();
        m_panRemainder = {};
    }
    event->accept();
    return true;
}
bool TouchpadNavigation::gesture(QNativeGestureEvent* event, const Handle(V3d_View) & view,
                                 double ratio, const Zoom& zoom)
{
    if (view.IsNull() || m_settings->mode() == NavigationInputMode::Mouse)
        return false;
    switch (event->gestureType())
    {
    case Qt::BeginNativeGesture:
    case Qt::EndNativeGesture:
        reset();
        break;
    case Qt::PanNativeGesture:
        pan(view, event->delta(), ratio);
        break;
    case Qt::ZoomNativeGesture:
        if (std::isfinite(event->value()) && event->value() > -1)
            zoom(event->position(), 1 + event->value());
        break;
    default:
        return false;
    }
    event->accept();
    return true;
}
} // namespace thecad::ui
