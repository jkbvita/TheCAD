#pragma once
#include "viewport/CameraNavigation.h"
#include <QElapsedTimer>
#include <functional>
class QMouseEvent;
class QNativeGestureEvent;
class QWheelEvent;
class QPointingDevice;

namespace thecad::ui
{
class NavigationInputSettings;
// Input translation only. Both workspaces retain their document/selection tools.
class TouchpadNavigation final
{
public:
    explicit TouchpadNavigation(NavigationInputSettings* settings = nullptr);
    using Pivot = std::function<gp_Pnt()>;
    using Zoom = std::function<void(QPointF, double)>;
    bool move(QMouseEvent* event, const Handle(V3d_View) & view, QSize size, double ratio,
              bool allowed, const Pivot& pivot);
    bool wheel(QWheelEvent* event, const Handle(V3d_View) & view, QSize size, double ratio,
               const Pivot& pivot, const Zoom& zoom);
    bool gesture(QNativeGestureEvent* event, const Handle(V3d_View) & view, double ratio,
                 const Zoom& zoom);
    void reset();

private:
    void pan(const Handle(V3d_View) & view, QPointF logical, double ratio);
    bool usesTouchpad(const QPointingDevice* device) const;
    NavigationInputSettings* m_settings;
    viewport::CameraNavigation m_pointer;
    viewport::CameraNavigation m_scroll;
    QElapsedTimer m_scrollTime;
    QPointF m_panRemainder;
};
} // namespace thecad::ui
