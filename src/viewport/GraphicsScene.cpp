#include "viewport/GraphicsScene.h"
#include "presentation/SelectionAppearance.h"
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <WNT_Window.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_TypeOfLimit.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <algorithm>
namespace thecad::viewport
{
GraphicsScene::~GraphicsScene()
{
    reset();
}
void GraphicsScene::initialize(Aspect_Handle nativeWindow)
{
    Handle(Aspect_DisplayConnection) display;
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display);
    m_viewer = new V3d_Viewer(driver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();
    m_context = new AIS_InteractiveContext(m_viewer);
    for (const auto type : {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_LocalSelected})
    {
        const auto& style = m_context->HighlightStyle(type);
        style->SetColor(presentation::selectionBlue());
        style->SetMethod(Aspect_TOHM_COLOR);
        style->SetDisplayMode(AIS_Shaded);
        style->SetTransparency(0);
        style->SetZLayer(Graphic3d_ZLayerId_Topmost);
    }
    m_context->SetToHilightSelected(Standard_False);
    m_context->SetAutomaticHilight(Standard_False);
    m_view = m_viewer->CreateView();
    // Qt owns frame scheduling; camera setters must not render each input event.
    m_view->SetImmediateUpdate(Standard_False);
    Handle(WNT_Window) window = new WNT_Window(nativeWindow);
    m_view->SetWindow(window);
    const int maximumSamples = driver->InquireLimit(Graphic3d_TypeOfLimit_MaxMsaa);
    int samples = 0;
    for (int candidate = 2; candidate <= std::min(4, maximumSamples); candidate *= 2)
        samples = candidate;
    m_view->ChangeRenderingParams().NbMsaaSamples = samples;
    m_view->ChangeRenderingParams().IsAntialiasingEnabled = Standard_True;
    m_view->ChangeRenderingParams().TransparencyMethod = Graphic3d_RTM_BLEND_OIT;
    if (!window->IsMapped())
    {
        window->Map();
    }
}
bool GraphicsScene::synchronizeSurface()
{
    if (!ready())
        return false;
    Standard_Integer width = 0, height = 0;
    m_view->Window()->Size(width, height);
    const QSize pixels(width, height);
    if (width <= 0 || height <= 0 || pixels == m_pixelSize)
        return false;
    m_view->MustBeResized();
    m_view->Invalidate();
    m_pixelSize = pixels;
    return true;
}
void GraphicsScene::reset()
{
    m_context.Nullify();
    m_pixelSize = {};
    if (!m_view.IsNull())
    {
        m_view->Remove();
        m_view.Nullify();
    }
    m_viewer.Nullify();
}
} // namespace thecad::viewport
