#include "ui/CadViewport.h"

#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_GradientFillMethod.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Graphic3d_Camera.hxx>
#include <gp_Vec.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Bnd_Box.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TCollection_ExtendedString.hxx>
#include <QApplication>
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
    qRegisterMetaType<modeling::SketchPlane>();
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
        updateReferencePlanes();
        if (m_sketchEditing) { orientSketch(); updateSketchAxes(); }
        else if (m_planesVisible) { resetView(); }
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
    for (auto& plane : m_referencePlanes) { plane.Nullify(); }
    for (auto& label : m_planeLabels) { label.Nullify(); }
    for (auto& axis : m_sketchAxes) { axis.Nullify(); }
    m_sketchOrigin.Nullify();
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
void CadViewport::beginPlaneSelection()
{
    clearPreview();
    m_regionSelecting = false;
    m_selectingPlane = true;
    m_selectedPlane.reset();
    showReferencePlanes(true);
    setFocus(Qt::OtherFocusReason);
}

void CadViewport::cancelPlaneSelection()
{
    const bool wasSelecting = m_selectingPlane;
    m_selectingPlane = false;
    m_selectedPlane.reset();
    m_hoveredPlane.reset();
    m_planeClickCandidate = false;
    showReferencePlanes(false);
    if (wasSelecting) { emit planeSelectionCanceled(); }
}

void CadViewport::showReferencePlanes(bool visible)
{
    const bool becameVisible = visible && !m_planesVisible;
    m_planesVisible = visible;
    if (!visible) { m_hoveredPlane.reset(); }
    updateReferencePlanes();
    if (becameVisible && isReady()) { fitAll(); }
}

void CadViewport::setSelectedReferencePlane(std::optional<modeling::SketchPlane> plane)
{
    m_selectedPlane = plane;
    updatePlaneStyles();
}

void CadViewport::updateReferencePlanes()
{
    if (!isReady()) { return; }
    for (auto& plane : m_referencePlanes) {
        if (!plane.IsNull()) { m_context->Remove(plane, Standard_False); plane.Nullify(); }
    }
    for (auto& label : m_planeLabels) {
        if (!label.IsNull()) { m_context->Remove(label, Standard_False); label.Nullify(); }
    }
    if (m_planesVisible) {
        m_planeHalfExtent = 50.0; // Model-space default for an empty part, not screen pixels.
        if (!m_shape.IsNull()) {
            Bnd_Box bounds;
            BRepBndLib::Add(m_shape, bounds);
            if (!bounds.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                bounds.Get(x0, y0, z0, x1, y1, z1);
                m_planeHalfExtent = std::max({std::abs(x0), std::abs(y0), std::abs(z0),
                    std::abs(x1), std::abs(y1), std::abs(z1), 1.0}) * 1.15;
                m_planeHalfExtent = std::min(m_planeHalfExtent, 1e9);
            }
        }
        for (int i = 0; i < 3; ++i) {
            const auto type = static_cast<modeling::SketchPlane>(i);
            auto& plane = m_referencePlanes[std::size_t(i)];
            plane = new AIS_Shape(modeling::referencePlaneFace(type, m_planeHalfExtent));
            plane->Attributes()->SetFaceBoundaryDraw(Standard_True);
            m_context->Display(plane, AIS_Shaded, -1, Standard_False);
            auto& label = m_planeLabels[std::size_t(i)];
            label = new AIS_TextLabel;
            label->SetText(TCollection_ExtendedString(modeling::referencePlaneName(type)));
            const modeling::Point2 corner = i == 1 ? modeling::Point2{-m_planeHalfExtent, m_planeHalfExtent}
                                                   : modeling::Point2{m_planeHalfExtent, m_planeHalfExtent};
            label->SetPosition(modeling::sketchPoint(type, corner));
            label->SetHeight(16.0 * devicePixelRatioF());
            label->SetInfiniteState(Standard_True);
            m_context->Display(label, 0, -1, Standard_False); // Labels are never pick targets.
        }
        updatePlaneStyles();
    }
    update();
}

void CadViewport::updatePlaneStyles()
{
    if (!isReady()) { return; }
    for (int i = 0; i < 3; ++i) {
        auto& plane = m_referencePlanes[std::size_t(i)];
        if (plane.IsNull()) { continue; }
        const auto type = static_cast<modeling::SketchPlane>(i);
        const bool selected = m_selectedPlane && *m_selectedPlane == type;
        const bool hovered = m_hoveredPlane && *m_hoveredPlane == type;
        const Quantity_Color color = selected ? Quantity_Color(0.20, 0.95, 0.45, Quantity_TOC_RGB)
            : hovered ? Quantity_Color(1.0, 0.65, 0.10, Quantity_TOC_RGB)
                      : Quantity_Color(0.25, 0.55, 1.0, Quantity_TOC_RGB);
        plane->SetColor(color);
        plane->SetTransparency(selected ? 0.40 : hovered ? 0.50 : 0.80);
        plane->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(color, Aspect_TOL_SOLID, selected ? 3.0 : 2.0));
        m_context->Redisplay(plane, Standard_False);
        if (!m_planeLabels[std::size_t(i)].IsNull()) {
            m_planeLabels[std::size_t(i)]->SetColor(selected || hovered ? color : Quantity_Color(Quantity_NOC_WHITE));
            m_context->Redisplay(m_planeLabels[std::size_t(i)], Standard_False);
        }
    }
    update();
}

std::optional<modeling::ReferencePlaneHit> CadViewport::planeAt(const QPointF& position)
{
    if (!isReady() || !m_planesVisible) { return {}; }
    synchronizeSurface();
    const auto pixel = nativePosition(position);
    double x, y, z, dx, dy, dz;
    m_view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Pnt projected(x, y, z);
    const gp_Dir direction(dx, dy, dz);
    // ConvertWithProj can put the origin on the target plane. Move it onto the
    // eye plane so depth comparisons are forward and correct for orthographic
    // and perspective cameras alike, even after orbiting and panning.
    const double depth = gp_Vec(m_view->Camera()->Eye(), projected).Dot(gp_Vec(direction));
    const auto origin = projected.Translated(-gp_Vec(direction) * depth);
    return modeling::pickReferencePlane(gp_Lin(origin, direction), m_planeHalfExtent);
}

void CadViewport::updateSketchAxes()
{
    if (!isReady()) { return; }
    for (auto& axis : m_sketchAxes) {
        if (!axis.IsNull()) { m_context->Remove(axis, Standard_False); axis.Nullify(); }
    }
    if (!m_sketchOrigin.IsNull()) { m_context->Remove(m_sketchOrigin, Standard_False); m_sketchOrigin.Nullify(); }
    if (m_sketchEditing && m_sketch) {
        const auto axes = modeling::sketchAxes(m_sketch->plane);
        const double span = std::max(m_view->Camera()->Scale(), 1.0);
        for (int i = 0; i < 2; ++i) {
            const gp_Vec direction(i == 0 ? axes.XDirection() : axes.YDirection());
            const auto shape = BRepBuilderAPI_MakeEdge(axes.Location().Translated(-direction * span),
                                                       axes.Location().Translated(direction * span)).Shape();
            auto& axis = m_sketchAxes[std::size_t(i)];
            axis = new AIS_Shape(shape);
            axis->SetColor(i == 0 ? Quantity_NOC_RED : Quantity_NOC_GREEN);
            axis->SetWidth(1.5);
            axis->SetInfiniteState(Standard_True); // Guides must not change Fit All or restrict sketch coordinates.
            m_context->Display(axis, AIS_WireFrame, -1, Standard_False);
        }
        m_sketchOrigin = new AIS_TextLabel;
        m_sketchOrigin->SetText(TCollection_ExtendedString("O"));
        m_sketchOrigin->SetPosition(axes.Location());
        m_sketchOrigin->SetColor(Quantity_NOC_WHITE);
        m_sketchOrigin->SetHeight(14.0 * devicePixelRatioF());
        m_sketchOrigin->SetInfiniteState(Standard_True);
        m_context->Display(m_sketchOrigin, 0, -1, Standard_False);
    }
    update();
}
void CadViewport::beginSketch(const modeling::Sketch& sketch)
{
    m_selectingPlane = false;
    showReferencePlanes(false);
    clearPreview();
    m_regionSelecting = false;
    m_sketchEditing = true;
    m_sketchTool.reset();
    m_firstPoint.reset();
    setSketch(sketch);
    orientSketch();
    updateSketchAxes();
    setFocus(Qt::OtherFocusReason);
}

void CadViewport::endSketch()
{
    m_sketchEditing = false;
    updateSketchAxes();
    m_sketchTool.reset();
    m_firstPoint.reset();
    m_sketch.reset();
    updateSketchPresentation();
    unsetCursor();
    resetView();
}

void CadViewport::beginContourSelection(const modeling::Sketch& sketch)
{
    showReferencePlanes(false);
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
        m_planeClickCandidate = m_planesVisible;
        m_planePressPosition = event->position();
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
    if (m_planesVisible && m_navigation == Navigation::None) {
        const auto hit = planeAt(event->position());
        const std::optional<modeling::SketchPlane> hovered = hit ? std::optional<modeling::SketchPlane>(hit->plane) : std::nullopt;
        if (hovered != m_hoveredPlane) {
            m_hoveredPlane = hovered;
            updatePlaneStyles();
        }
    }
    if (m_planeClickCandidate
        && (event->position() - m_planePressPosition).manhattanLength() > QApplication::startDragDistance()) {
        m_planeClickCandidate = false;
    }
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
        const bool click = m_planeClickCandidate && event->button() == Qt::LeftButton
            && (event->position() - m_planePressPosition).manhattanLength() <= QApplication::startDragDistance();
        m_navigation = Navigation::None;
        m_navigationButton = Qt::NoButton;
        m_planeClickCandidate = false;
        unsetCursor();
        if (click) {
            const auto hit = planeAt(event->position());
            if (hit) {
                setSelectedReferencePlane(hit->plane);
                emit referencePlaneSelected(hit->plane);
            }
        }
        event->accept();
    }
}

bool CadViewport::event(QEvent* event)
{
    if (event->type() == QEvent::Leave && m_hoveredPlane) {
        m_hoveredPlane.reset();
        updatePlaneStyles();
    }
    if (event->type() == QEvent::DevicePixelRatioChange || event->type() == QEvent::ScreenChangeInternal) {
        m_renderSize = {};
        update();
    }
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
        m_navigation = Navigation::None;
        m_navigationButton = Qt::NoButton;
        m_planeClickCandidate = false;
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
    if (event->key() == Qt::Key_Escape && m_selectingPlane) {
        cancelPlaneSelection();
    } else if (event->key() == Qt::Key_Escape && m_sketchEditing) {
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
