#include "ui/ViewportWidget.h"

#include "presentation/ViewportAppearance.h"
#include "ui/Theme.h"
#include "ui/input/NavigationInputSettings.h"
#include <QEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
ViewportWidget::ViewportWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    connect(&NavigationInputSettings::instance(), &NavigationInputSettings::changed, this,
            [this] { m_touchpad.reset(); });
}

Handle(Graphic3d_Camera) ViewportWidget::camera() const
{
    return isReady() ? m_graphics.view()->Camera() : Handle(Graphic3d_Camera){};
}

QPoint ViewportWidget::nativePosition(const QPointF& position) const
{
    return {qRound(position.x() * devicePixelRatioF()), qRound(position.y() * devicePixelRatioF())};
}

void ViewportWidget::initializeGraphics()
{
    m_graphics.initialize(reinterpret_cast<Aspect_Handle>(winId()));
}

void ViewportWidget::resetGraphics()
{
    m_graphics.reset();
}

bool ViewportWidget::synchronizeGraphics()
{
    return m_graphics.synchronizeSurface();
}

void ViewportWidget::redrawGraphics()
{
    if (isReady())
        m_graphics.view()->Redraw();
}

void ViewportWidget::applyCommonViewportTheme()
{
    if (!isReady())
        return;
    presentation::applyViewportAppearance(m_graphics.view(), ThemeManager::instance().isDark());
}

void ViewportWidget::fitScene(double margin)
{
    if (!isReady())
        return;
    synchronizeGraphics();
    m_camera.resetAnchor();
    m_graphics.view()->FitAll(margin, Standard_False);
    m_graphics.view()->ZFitAll();
    update();
}

void ViewportWidget::setStandardView(StandardView view)
{
    if (!isReady())
        return;

    gp_Dir towardViewer(0, 0, 1);
    gp_Dir up(0, 1, 0);
    switch (view)
    {
    case StandardView::Front:
        break;
    case StandardView::Back:
        towardViewer = gp_Dir(0, 0, -1);
        break;
    case StandardView::Left:
        towardViewer = gp_Dir(-1, 0, 0);
        break;
    case StandardView::Right:
        towardViewer = gp_Dir(1, 0, 0);
        break;
    case StandardView::Top:
        towardViewer = gp_Dir(0, 1, 0);
        up = gp_Dir(0, 0, -1);
        break;
    case StandardView::Bottom:
        towardViewer = gp_Dir(0, -1, 0);
        up = gp_Dir(0, 0, 1);
        break;
    case StandardView::Isometric:
        towardViewer = gp_Dir(1, 1, 1);
        break;
    }

    m_camera.resetAnchor();
    const auto viewObject = m_graphics.view();
    const auto cameraObject = viewObject->Camera();
    cameraObject->SetDirection(towardViewer.Reversed());
    cameraObject->SetUp(up);
    cameraObject->OrthogonalizeUp();
    viewObject->ZFitAll();
    cameraViewChanged();
    update();
}

bool ViewportWidget::beginCameraNavigation(QMouseEvent* event)
{
    if (!isReady() || !cameraNavigationAllowed())
        return false;
    const auto mode = viewport::CameraNavigation::modeFor(event->modifiers());
    const auto pivot = mode == viewport::CameraNavigation::Mode::Orbit
                           ? navigationPivot(event->position())
                           : camera()->Center();
    const auto depth = mode == viewport::CameraNavigation::Mode::Zoom &&
                               !camera()->IsOrthographic()
                           ? zoomAnchor(event->position())
                           : camera()->Center();
    m_camera.begin(mode, event->position(), nativePosition(event->position()), pivot, depth);
    setFocus(Qt::MouseFocusReason);
    grabMouse();
    setCursor(mode == viewport::CameraNavigation::Mode::Pan ? Qt::ClosedHandCursor
                                                            : Qt::SizeAllCursor);
    event->accept();
    return true;
}

bool ViewportWidget::moveCameraNavigation(QMouseEvent* event)
{
    if (!isReady() || m_camera.mode() == viewport::CameraNavigation::Mode::None)
        return false;
    const auto depth = m_camera.mode() == viewport::CameraNavigation::Mode::Zoom
                           ? std::optional<gp_Pnt>(zoomAnchor(m_camera.position()))
                           : std::optional<gp_Pnt>{};
    m_camera.move(m_graphics.view(), event->position(), nativePosition(event->position()), size(),
                  depth);
    cameraViewChanged();
    update();
    event->accept();
    return true;
}

bool ViewportWidget::endCameraNavigation(QMouseEvent* event)
{
    if (event->button() != m_camera.button() ||
        m_camera.mode() == viewport::CameraNavigation::Mode::None)
        return false;
    m_camera.stop();
    cameraViewChanged();
    update();
    unsetCursor();
    if (QWidget::mouseGrabber() == this)
        releaseMouse();
    event->accept();
    return true;
}

bool ViewportWidget::handleTouchpadPointerMove(QMouseEvent* event, bool allowed)
{
    if (!isReady() || !allowed ||
        !m_touchpad.move(event, m_graphics.view(), size(), devicePixelRatioF(), true,
                         [this, event] { return navigationPivot(event->position()); }))
        return false;
    cameraViewChanged();
    update();
    return true;
}

void ViewportWidget::zoomAtCursor(const QPointF& position, double factor)
{
    if (!isReady())
        return;
    viewport::CameraNavigation::zoom(m_graphics.view(), nativePosition(position), factor,
                                     zoomAnchor(position));
}

gp_Pnt ViewportWidget::zoomAnchor(const QPointF&)
{
    return camera()->Center();
}

bool ViewportWidget::handleWheelNavigation(QWheelEvent* event, bool allowed)
{
    if (!isReady() || !allowed)
        return false;
    if (m_touchpad.wheel(event, m_graphics.view(), size(), devicePixelRatioF(),
                         [this, event] { return navigationPivot(event->position()); },
                         [this](QPointF point, double factor) {
                             if (factor > 1.0)
                                 zoomAtCursor(point, factor);
                             else
                                 viewport::CameraNavigation::zoomCentered(m_graphics.view(), factor);
                         }))
    {
        cameraViewChanged();
        update();
        return true;
    }

    const auto angle = event->angleDelta();
    const auto pixels = event->pixelDelta();
    const double steps = !angle.isNull() ? angle.y() / 120.0 : pixels.y() / 40.0;
    if (steps != 0.0)
    {
        const double factor = std::pow(1.15, -std::clamp(steps, -10.0, 10.0));
        if (factor > 1.0)
            zoomAtCursor(event->position(), factor);
        else
            viewport::CameraNavigation::zoomCentered(m_graphics.view(), factor);
        cameraViewChanged();
        update();
    }
    event->accept();
    return true;
}

void ViewportWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    update();
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_graphics.invalidateSize();
    update();
}

bool ViewportWidget::event(QEvent* event)
{
    if (event->type() == QEvent::NativeGesture && isReady() && cameraNavigationAllowed() &&
        m_camera.mode() == viewport::CameraNavigation::Mode::None &&
        m_touchpad.gesture(static_cast<QNativeGestureEvent*>(event), m_graphics.view(),
                           devicePixelRatioF(), [this](QPointF point, double factor) {
                               if (factor > 1.0)
                                   zoomAtCursor(point, factor);
                               else
                                   viewport::CameraNavigation::zoomCentered(m_graphics.view(),
                                                                           factor);
                           }))
    {
        cameraViewChanged();
        update();
        return true;
    }
    if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut ||
        event->type() == QEvent::WindowDeactivate || event->type() == QEvent::UngrabMouse ||
        event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::KeyRelease ||
        event->type() == QEvent::KeyPress)
        m_touchpad.reset();
    if (event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::ScreenChangeInternal)
    {
        m_graphics.invalidateSize();
        update();
    }
    return QWidget::event(event);
}
} // namespace thecad::ui
