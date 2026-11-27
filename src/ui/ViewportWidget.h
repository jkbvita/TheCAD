#pragma once

#include "ui/input/TouchpadNavigation.h"
#include "viewport/CameraNavigation.h"
#include "viewport/GraphicsScene.h"
#include <QWidget>

class QMouseEvent;
class QWheelEvent;

namespace thecad::ui
{
// Shared native Open CASCADE viewport shell. Workspace viewports provide their
// own document selection and interaction policies while using the same camera,
// surface lifetime, input routing, and standard-view behavior.
class ViewportWidget : public QWidget
{
public:
    enum class StandardView
    {
        Front,
        Back,
        Left,
        Right,
        Top,
        Bottom,
        Isometric
    };

    explicit ViewportWidget(QWidget* parent = nullptr);
    bool isReady() const { return m_graphics.ready(); }
    Handle(Graphic3d_Camera) camera() const;
    QPoint nativePosition(const QPointF& position) const;

    void fitScene(double margin = 0.1);
    void setStandardView(StandardView view);

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

    void initializeGraphics();
    void resetGraphics();
    bool synchronizeGraphics();
    void redrawGraphics();
    void applyCommonViewportTheme();

    bool beginCameraNavigation(QMouseEvent* event);
    bool moveCameraNavigation(QMouseEvent* event);
    bool endCameraNavigation(QMouseEvent* event);
    bool handleTouchpadPointerMove(QMouseEvent* event, bool allowed = true);
    bool handleWheelNavigation(QWheelEvent* event, bool allowed = true);
    void zoomAtCursor(const QPointF& position, double factor);

    virtual gp_Pnt navigationPivot(const QPointF& position) = 0;
    virtual gp_Pnt zoomAnchor(const QPointF& position);
    virtual bool cameraNavigationAllowed() const { return true; }
    virtual void cameraViewChanged() {}

    viewport::GraphicsScene m_graphics;
    viewport::CameraNavigation m_camera;
    TouchpadNavigation m_touchpad;
};
} // namespace thecad::ui
