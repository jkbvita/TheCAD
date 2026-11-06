#pragma once
#include <V3d_View.hxx>
#include <QPointF>
#include <QSize>
#include <Qt>
#include <optional>
namespace thecad::viewport
{
// Camera math and gesture state; independent of widgets, sketches and selection.
class CameraNavigation final
{
public:
    enum class Mode
    {
        None,
        Orbit,
        Pan,
        Zoom
    };
    static Mode modeFor(Qt::KeyboardModifiers modifiers);
    void begin(Qt::KeyboardModifiers modifiers, QPointF logical, QPoint physical, gp_Pnt pivot,
               gp_Pnt zoomDepth);
    void move(const Handle(V3d_View) & view, QPointF logical, QPoint physical, QSize viewport,
              std::optional<gp_Pnt> zoomDepth = {});
    static void zoom(const Handle(V3d_View) & view, QPoint pixel, double factor, gp_Pnt depth);
    static void zoomCentered(const Handle(V3d_View) & view, double factor);
    void stop() { m_mode = Mode::None; }
    Mode mode() const { return m_mode; }
    Qt::MouseButton button() const
    {
        return m_mode == Mode::None ? Qt::NoButton : Qt::MiddleButton;
    }
    const QPointF& position() const { return m_position; }
    const std::optional<gp_Pnt>& anchor() const { return m_anchor; }
    void resetAnchor() { m_anchor.reset(); }

private:
    Mode m_mode = Mode::None;
    QPointF m_position;
    QPoint m_physical;
    gp_Pnt m_pivot, m_zoomDepth;
    std::optional<gp_Pnt> m_anchor;
};
} // namespace thecad::viewport
