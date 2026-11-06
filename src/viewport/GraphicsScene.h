#pragma once
#include <AIS_InteractiveContext.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Aspect_Handle.hxx>
#include <QSize>
namespace thecad::viewport
{
// Owns the native OCCT graphics lifetime. No QWidget or document dependency.
class GraphicsScene final
{
public:
    GraphicsScene() = default;
    ~GraphicsScene();
    GraphicsScene(const GraphicsScene&) = delete;
    GraphicsScene& operator=(const GraphicsScene&) = delete;
    void initialize(Aspect_Handle nativeWindow);
    void reset();
    bool ready() const { return !m_view.IsNull(); }
    bool synchronizeSurface();
    void invalidateSize() { m_pixelSize = {}; }
    const QSize& pixelSize() const { return m_pixelSize; }
    const Handle(V3d_View) & view() const { return m_view; }
    const Handle(V3d_Viewer) & viewer() const { return m_viewer; }
    const Handle(AIS_InteractiveContext) & context() const { return m_context; }

private:
    Handle(V3d_Viewer) m_viewer;
    Handle(AIS_InteractiveContext) m_context;
    Handle(V3d_View) m_view;
    QSize m_pixelSize;
};
} // namespace thecad::viewport
