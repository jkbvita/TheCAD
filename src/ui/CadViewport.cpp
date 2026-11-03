#include "ui/CadViewport.h"

#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_GradientFillMethod.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Graphic3d_Camera.hxx>
#include <gp_Vec.hxx>
#include <Prs3d_Drawer.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <WNT_Window.hxx>

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace thecad::ui {

CadViewport::CadViewport(const TopoDS_Shape& shape, QWidget* parent)
    : QWidget(parent), m_shape(shape)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    qRegisterMetaType<modeling::SketchEntity>();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setObjectName("cadViewport");
    setAccessibleName("3D CAD viewport");
}

CadViewport::~CadViewport()
{
    releaseViewer();
}

bool CadViewport::isReady() const
{
    return !m_view.IsNull();
}

QString CadViewport::initializationError() const
{
    return m_error;
}

QPaintEngine* CadViewport::paintEngine() const
{
    // OCCT owns the native window's OpenGL surface; Qt must not paint over it.
    return nullptr;
}

void CadViewport::initialize()
{
    m_attemptedInitialization = true;
    try {
        Handle(Aspect_DisplayConnection) display;
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display);
        m_viewer = new V3d_Viewer(driver);
        m_viewer->SetDefaultLights();
        m_viewer->SetLightOn();
        m_context = new AIS_InteractiveContext(m_viewer);
        m_view = m_viewer->CreateView();
        Handle(WNT_Window) window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
        m_view->SetWindow(window);
        if (!window->IsMapped()) {
            window->Map();
        }
        m_view->SetBgGradientColors(
            Quantity_Color(0.12, 0.16, 0.22, Quantity_TOC_RGB),
            Quantity_Color(0.28, 0.34, 0.42, Quantity_TOC_RGB), Aspect_GFM_VER);
        m_view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_WHITE, 0.08, V3d_ZBUFFER);

        m_context->SetDisplayMode(AIS_Shaded, Standard_False);
        if (!setShape(m_shape)) {
            Standard_Failure::Raise(m_error.toUtf8().constData());
        }
        updateSketchPresentation();
        if (m_sketchEditing) { orientSketch(); }
    } catch (const Standard_Failure& failure) {
        m_error = QString::fromUtf8(failure.GetMessageString());
        if (m_error.isEmpty()) {
            m_error = "Open CASCADE could not initialize the OpenGL viewport.";
        }
        releaseViewer();
        emit initializationFailed(m_error);
    }
}

void CadViewport::releaseGraphics()
{
    releaseViewer();
    m_attemptedInitialization = false;
}

void CadViewport::releaseViewer()
{
    m_previewPresentation.Nullify();
    m_sketchPresentation.Nullify();
    m_presentation.Nullify();
    m_context.Nullify();
    m_renderSize = {};
    if (!m_view.IsNull()) {
        m_view->Remove();
        m_view.Nullify();
    }
    m_viewer.Nullify();
}

void CadViewport::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // A child's show/resize event can precede the final HWND client geometry.
    // Initialize and synchronize at paint time, after Qt has laid out the window.
    update();
}

void CadViewport::paintEvent(QPaintEvent*)
{
    if (!m_attemptedInitialization) {
        initialize();
    }
    if (isReady()) {
        synchronizeSurface();
        m_view->Redraw();
    }
}

void CadViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (isReady()) {
        update();
    }
}

void CadViewport::synchronizeSurface()
{
    if (!isReady()) {
        return;
    }
    // WNT_Window::Size reads GetClientRect: physical client pixels, not Qt DIPs.
    // resizeEvent can run before Qt updates this native child. Checking again at
    // paint time avoids OCCT retaining the initial minimum-size OpenGL viewport.
    Standard_Integer width = 0, height = 0;
    m_view->Window()->Size(width, height);
    const QSize pixels(width, height);
    if (width > 0 && height > 0 && pixels != m_renderSize) {
        m_view->MustBeResized();
        m_view->Invalidate();
        m_renderSize = pixels;
    }
}

bool CadViewport::setShape(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) {
        if (isReady() && !m_presentation.IsNull()) {
            m_context->Remove(m_presentation, Standard_False);
            m_presentation.Nullify();
        }
        m_shape.Nullify();
        m_error.clear();
        update();
        return true;
    }
    if (!isReady()) {
        m_shape = shape;
        return true;
    }
    Handle(AIS_Shape) candidate = new AIS_Shape(shape);
    try {
        candidate->SetColor(Quantity_Color(0.32, 0.62, 0.88, Quantity_TOC_RGB));
        candidate->Attributes()->SetFaceBoundaryDraw(Standard_True);
        m_context->Display(candidate, Standard_False);
        if (!m_presentation.IsNull()) {
            m_context->Erase(m_presentation, Standard_False);
        }
        resetView();
    } catch (const Standard_Failure& failure) {
        m_context->Remove(candidate, Standard_False);
        if (!m_presentation.IsNull()) {
            m_context->Display(m_presentation, Standard_False);
        }
        m_error = QString::fromUtf8(failure.GetMessageString());
        update();
        return false;
    }
    if (!m_presentation.IsNull()) {
        m_context->Remove(m_presentation, Standard_False);
    }
    m_presentation = candidate;
    m_shape = shape;
    m_error.clear();
    return true;
}
void CadViewport::beginSketch(const modeling::Sketch& sketch)
{
    clearPreview();
    m_regionSelecting = false;
    m_sketchEditing = true;
    m_sketchTool.reset();
    m_firstPoint.reset();
    setSketch(sketch);
    orientSketch();
    setFocus(Qt::OtherFocusReason);
}

void CadViewport::endSketch()
{
    m_sketchEditing = false;
    m_sketchTool.reset();
    m_firstPoint.reset();
    m_sketch.reset();
    updateSketchPresentation();
    unsetCursor();
    resetView();
}

void CadViewport::beginContourSelection(const modeling::Sketch& sketch)
{
    m_sketchEditing = false;
    m_regionSelecting = true;
    m_firstPoint.reset();
    setSketch(sketch);
    orientSketch();
}

void CadViewport::endContourSelection()
{
    m_regionSelecting = false;
    m_sketch.reset();
    updateSketchPresentation();
}

void CadViewport::previewShape(const TopoDS_Shape& shape)
{
    if (!isReady() || shape.IsNull()) { return; }
    const bool firstPreview = m_previewPresentation.IsNull();
    clearPreview();
    m_previewPresentation = new AIS_Shape(shape);
    m_previewPresentation->SetColor(Quantity_Color(0.30, 0.75, 0.83, Quantity_TOC_RGB));
    m_previewPresentation->Attributes()->SetFaceBoundaryDraw(Standard_True);
    if (!m_presentation.IsNull()) { m_context->Erase(m_presentation, Standard_False); }
    m_context->Display(m_previewPresentation, Standard_False);
    if (firstPreview) { resetView(); }
    update();
}

void CadViewport::clearPreview()
{
    if (!isReady()) { return; }
    if (!m_previewPresentation.IsNull()) {
        m_context->Remove(m_previewPresentation, Standard_False);
        m_previewPresentation.Nullify();
        if (!m_presentation.IsNull()) { m_context->Display(m_presentation, Standard_False); }
        update();
    }
}
void CadViewport::setSketchTool(modeling::EntityKind kind)
{
    m_sketchTool = kind;
    m_firstPoint.reset();
    if (m_sketchEditing) { setCursor(Qt::CrossCursor); }
    updateSketchPresentation();
}

void CadViewport::setSketch(const modeling::Sketch& sketch)
{
    m_sketch = sketch;
    updateSketchPresentation();
}

void CadViewport::orientSketch()
{
    if (!isReady() || !m_sketch) { return; }
    const auto axes = modeling::sketchAxes(m_sketch->plane);
    const auto normal = axes.Direction();
    const auto up = axes.YDirection();
    m_view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    m_view->SetProj(normal.X(), normal.Y(), normal.Z());
    m_view->SetUp(up.X(), up.Y(), up.Z());
    if (m_shape.IsNull() && m_sketch->entities.empty()) {
        m_view->SetAt(0.0, 0.0, 0.0);
        m_view->Camera()->SetScale(150.0); // Initial model-space span in mm, not a pixel rectangle.
    } else {
        fitAll();
    }
    update();
}

void CadViewport::updateSketchPresentation(const std::optional<modeling::SketchEntity>& preview)
{
    if (!isReady()) { return; }
    if (!m_sketchPresentation.IsNull()) {
        m_context->Remove(m_sketchPresentation, Standard_False);
        m_sketchPresentation.Nullify();
    }
    if (m_sketch) {
        try {
            auto visible = *m_sketch;
            if (preview) { visible.entities.push_back(*preview); }
            const auto shape = modeling::sketchShape(visible);
            if (!shape.IsNull()) {
                m_sketchPresentation = new AIS_Shape(shape);
                m_sketchPresentation->SetColor(Quantity_NOC_ORANGE);
                m_sketchPresentation->SetWidth(3.0);
                m_context->Display(m_sketchPresentation, AIS_WireFrame, -1, Standard_False);
                m_context->SetDisplayPriority(m_sketchPresentation, Graphic3d_DisplayPriority_Topmost);
            }
        } catch (const std::exception&) {
            // A zero-size pointer preview is transient, never stored as model data.
        } catch (const Standard_Failure&) {
        }
    }
    update();
}

std::optional<modeling::Point2> CadViewport::sketchPosition(const QPointF& position)
{
    if (!isReady() || !m_sketch) { return {}; }
    synchronizeSurface();
    const QPoint pixel = nativePosition(position);
    double x, y, z, dx, dy, dz;
    m_view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const auto axes = modeling::sketchAxes(m_sketch->plane);
    const gp_Vec normal(axes.Direction());
    const gp_Vec ray(dx, dy, dz);
    const double divisor = ray.Dot(normal);
    if (std::abs(divisor) < 1e-10) { return {}; }
    const gp_Pnt origin(x, y, z);
    const double distance = gp_Vec(origin, axes.Location()).Dot(normal) / divisor;
    const auto onPlane = origin.Translated(ray * distance);
    const gp_Vec local(axes.Location(), onPlane);
    return modeling::Point2{local.Dot(gp_Vec(axes.XDirection())), local.Dot(gp_Vec(axes.YDirection()))};
}
QPoint CadViewport::nativePosition(const QPointF& position) const
{
    // Qt events use device-independent coordinates; OCCT uses native pixels.
    return QPoint(qRound(position.x() * devicePixelRatioF()),
                  qRound(position.y() * devicePixelRatioF()));
}

void CadViewport::mousePressEvent(QMouseEvent* event)
{
    if (!isReady() || m_navigation != Navigation::None) {
        return;
    }
    if (event->button() == Qt::LeftButton && m_regionSelecting) {
        const auto point = sketchPosition(event->position());
        if (point) { emit sketchRegionClicked(*point); }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_sketchEditing) {
        setFocus(Qt::MouseFocusReason);
        const auto point = sketchPosition(event->position());
        if (m_sketchTool && point) {
            if (!m_firstPoint) {
                m_firstPoint = point;
            } else {
                modeling::SketchEntity entity;
                entity.kind = *m_sketchTool;
                entity.first = *m_firstPoint;
                entity.second = *point;
                entity.radius = std::hypot(point->x - m_firstPoint->x, point->y - m_firstPoint->y);
                m_firstPoint.reset();
                emit sketchEntityCreated(entity);
                updateSketchPresentation();
            }
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_navigation = Navigation::Orbit;
        const QPoint position = nativePosition(event->position());
        m_view->StartRotation(position.x(), position.y());
    } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_navigation = Navigation::Pan;
    } else {
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_navigationButton = event->button();
    m_previousPosition = nativePosition(event->position());
    setCursor(m_navigation == Navigation::Pan ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
    event->accept();
}

void CadViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (isReady() && m_sketchEditing && m_navigation == Navigation::None && m_sketchTool && m_firstPoint) {
        const auto point = sketchPosition(event->position());
        if (point) {
            modeling::SketchEntity preview;
            preview.kind = *m_sketchTool;
            preview.first = *m_firstPoint;
            preview.second = *point;
            preview.radius = std::hypot(point->x - m_firstPoint->x, point->y - m_firstPoint->y);
            updateSketchPresentation(preview);
        }
        return;
    }
    if (!isReady() || m_navigation == Navigation::None) {
        return;
    }
    const QPoint position = nativePosition(event->position());
    if (m_navigation == Navigation::Orbit) {
        m_view->Rotation(position.x(), position.y());
    } else {
        const QPoint delta = position - m_previousPosition;
        m_view->Pan(delta.x(), -delta.y());
    }
    m_previousPosition = position;
    update();
    event->accept();
}

void CadViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == m_navigationButton) {
        m_navigation = Navigation::None;
        m_navigationButton = Qt::NoButton;
        unsetCursor();
        event->accept();
    }
}

bool CadViewport::event(QEvent* event)
{
    if (event->type() == QEvent::DevicePixelRatioChange || event->type() == QEvent::ScreenChangeInternal) {
        m_renderSize = {};
        update();
    }
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
        m_navigation = Navigation::None;
        m_navigationButton = Qt::NoButton;
        unsetCursor();
    }
    return QWidget::event(event);
}

void CadViewport::wheelEvent(QWheelEvent* event)
{
    if (!isReady()) {
        return;
    }
    const double steps = !event->angleDelta().isNull()
        ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 40.0;
    const double factor = std::pow(1.15, std::clamp(steps, -10.0, 10.0));
    m_view->SetZoom(factor);
    update();
    event->accept();
}

void CadViewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_sketchEditing) {
        m_firstPoint.reset();
        updateSketchPresentation();
    } else if (event->key() == Qt::Key_F) {
        fitAll();
    } else if (event->key() == Qt::Key_Home) {
        resetView();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void CadViewport::fitAll()
{
    if (isReady()) {
        synchronizeSurface();
        m_view->FitAll(0.1, Standard_False);
        m_view->ZFitAll();
        update();
    }
}

void CadViewport::resetView()
{
    if (isReady()) {
        if (m_sketchEditing) { orientSketch(); return; }
        m_view->SetProj(V3d_XposYnegZpos);
        fitAll();
    }
}

} // namespace thecad::ui
