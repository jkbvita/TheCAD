#include "ui/CadViewport.h"

#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_GradientFillMethod.hxx>
#include <OpenGl_GraphicDriver.hxx>
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

        Handle(AIS_Shape) presentation = new AIS_Shape(m_shape);
        presentation->SetColor(Quantity_Color(0.32, 0.62, 0.88, Quantity_TOC_RGB));
        presentation->Attributes()->SetFaceBoundaryDraw(Standard_True);
        m_context->SetDisplayMode(AIS_Shaded, Standard_False);
        m_context->Display(presentation, Standard_False);
        m_view->MustBeResized();
        resetView();
    } catch (const Standard_Failure& failure) {
        m_error = QString::fromUtf8(failure.GetMessageString());
        if (m_error.isEmpty()) {
            m_error = "Open CASCADE could not initialize the OpenGL viewport.";
        }
        releaseViewer();
        emit initializationFailed(m_error);
    }
}

void CadViewport::releaseViewer()
{
    m_context.Nullify();
    if (!m_view.IsNull()) {
        m_view->Remove();
        m_view.Nullify();
    }
    m_viewer.Nullify();
}

void CadViewport::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_attemptedInitialization) {
        initialize();
    }
}

void CadViewport::paintEvent(QPaintEvent*)
{
    if (isReady()) {
        m_view->Redraw();
    }
}

void CadViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (isReady()) {
        m_view->MustBeResized();
        update();
    }
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
    if (event->key() == Qt::Key_F) {
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
        m_view->FitAll(0.1, Standard_False);
        m_view->ZFitAll();
        update();
    }
}

void CadViewport::resetView()
{
    if (isReady()) {
        m_view->SetProj(V3d_XposYnegZpos);
        fitAll();
    }
}

} // namespace thecad::ui
