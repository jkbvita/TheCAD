#include "presentation/ViewportAppearance.h"
#include "ui/assembly/AssemblyViewport.h"
#include "viewport/ComponentRotationDrag.h"
#include "ui/Theme.h"
#include "ui/input/NavigationInputSettings.h"
#include <QNativeGestureEvent>
#include "presentation/SolidPresentation.h"
#include "presentation/SelectionAppearance.h"
#include <AIS_Shape.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopLoc_Location.hxx>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
namespace
{
bool samePose(const gp_Trsf& a, const gp_Trsf& b)
{
    for (int r = 1; r <= 3; ++r)
        for (int c = 1; c <= 4; ++c)
            if (std::abs(a.Value(r, c) - b.Value(r, c)) > 1e-12)
                return false;
    return true;
}
} // namespace
AssemblyViewport::AssemblyViewport(QWidget* parent) : QWidget(parent)
{
    setObjectName("assemblyViewport");
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &AssemblyViewport::applyTheme);
    connect(&NavigationInputSettings::instance(), &NavigationInputSettings::changed, this,
            [this]{m_touchpad.reset();});
}
QPoint AssemblyViewport::native(const QPointF& point) const
{
    return {qRound(point.x() * devicePixelRatioF()), qRound(point.y() * devicePixelRatioF())};
}
Handle(Graphic3d_Camera) AssemblyViewport::camera() const
{
    return isReady() ? m_graphics.view()->Camera() : Handle(Graphic3d_Camera){};
}
void AssemblyViewport::initialize()
{
    m_attempted = true;
    try
    {
        m_graphics.initialize(reinterpret_cast<Aspect_Handle>(winId()));
        applyTheme();
        standardView(6);
        const auto visuals = m_visuals;
        m_visuals.clear();
        setComponents(visuals);
        if (m_fitPending)
            fitAll();
    }
    catch (const Standard_Failure& error)
    {
        m_error = QString::fromUtf8(error.GetMessageString());
        emit initializationFailed(m_error);
    }
    catch (const std::exception& error)
    {
        m_error = QString::fromUtf8(error.what());
        emit initializationFailed(m_error);
    }
}
void AssemblyViewport::showEvent(QShowEvent*)
{
    if (!m_attempted)
        initialize();
}
void AssemblyViewport::paintEvent(QPaintEvent*)
{
    if (!m_attempted)
        initialize();
    if (isReady())
    {
        m_graphics.synchronizeSurface();
        m_graphics.view()->Redraw();
        emit framePresented();
    }
}
void AssemblyViewport::resizeEvent(QResizeEvent*)
{
    if (isReady())
        m_graphics.invalidateSize();
    update();
}
void AssemblyViewport::applyTheme()
{
    if (!isReady())
        return;
    const bool dark = ThemeManager::instance().isDark();
    presentation::applyViewportAppearance(m_graphics.view(),dark);
    update();
}
void AssemblyViewport::setComponents(const std::vector<AssemblyVisual>& visuals)
{
    const bool unchanged =
        visuals.size() == m_visuals.size() &&
        std::equal(visuals.begin(), visuals.end(), m_visuals.begin(),
                   [](const auto& a, const auto& b)
                   {
                       return a.id == b.id && a.shape.IsSame(b.shape) && a.visible == b.visible &&
                              a.fixed == b.fixed && a.preview == b.preview &&
                              a.constrained == b.constrained && samePose(a.placement, b.placement);
                   });
    m_visuals = visuals;
    if (!isReady())
        return;
    if (unchanged)
        return;
    const auto context = m_graphics.context();
    if (!m_hover.IsNull())
        context->Remove(m_hover, false);
    m_hover.Nullify();
    m_hovered.clear();
    m_hoveredFace = 0;
    for (auto it = m_presentations.begin(); it != m_presentations.end();)
    {
        if (std::none_of(visuals.begin(), visuals.end(),
                         [&](const auto& v) { return v.id == it->first; }))
        {
            context->Remove(it->second, false);
            it = m_presentations.erase(it);
        }
        else
            ++it;
    }
    for (const auto& visual : visuals)
    {
        auto& item = m_presentations[visual.id];
        const bool replaced = item.IsNull() || !item->Shape().IsSame(visual.shape);
        if (replaced)
        {
            if (!item.IsNull())
                context->Remove(item, false);
            // Selection is activated before the first paint. The shared solid
            // presentation deliberately disables automatic meshing.
            presentation::prepareSolidTriangulation(visual.shape);
            item = presentation::makeSolidPresentation(visual.shape);
        }
        bool moved = false;
        for (int r = 1; r <= 3; ++r)
            for (int c = 1; c <= 4; ++c)
                moved |= std::abs(item->LocalTransformation().Value(r, c) -
                                  visual.placement.Value(r, c)) > 1e-12;
        if (moved)
        {
            if (replaced)
                item->SetLocalTransformation(visual.placement);
            else
                context->SetLocation(item, TopLoc_Location(visual.placement));
        }
        if (std::abs(item->Transparency() - (visual.preview ? 0.3 : 0)) > 1e-9)
            item->SetTransparency(visual.preview ? 0.3 : 0);
        if (visual.visible && (replaced || !context->IsDisplayed(item)))
        {
            context->Display(item, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
            context->Activate(item, AIS_Shape::SelectionMode(TopAbs_EDGE), false);
            context->Activate(item, AIS_Shape::SelectionMode(TopAbs_VERTEX), false);
        }
        else if (!visual.visible && context->IsDisplayed(item))
            context->Erase(item, false);
    }
    refreshSelection();
    m_graphics.view()->ZFitAll();
    update();
}
TopoDS_Shape AssemblyViewport::selectedShape(const std::string& id, int face,
                                             TopAbs_ShapeEnum kind) const
{
    const auto visual = std::find_if(m_visuals.begin(), m_visuals.end(),
                                     [&](const auto& v) { return v.id == id && v.visible; });
    if (visual == m_visuals.end())
        return {};
    if (!face)
        return visual->shape;
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(visual->shape, kind, faces);
    return face > 0 && face <= faces.Extent() ? faces(face) : TopoDS_Shape{};
}
void AssemblyViewport::setSelected(const std::string& id, int face, TopAbs_ShapeEnum kind)
{
    if (m_selected == id && m_selectedFace == face && m_selectedKind == kind)
        return;
    m_selected = id;
    m_selectedFace = face;
    m_selectedKind = kind;
    refreshSelection();
    update();
}
void AssemblyViewport::refreshSelection()
{
    if (!isReady())
        return;
    const auto shape = selectedShape(m_selected, m_selectedFace, m_selectedKind);
    if (shape.IsNull())
    {
        if (!m_highlight.IsNull())
            m_graphics.context()->Remove(m_highlight, false);
        m_highlight.Nullify();
        return;
    }
    const bool replaced = m_highlight.IsNull() || !m_highlight->Shape().IsSame(shape);
    if (replaced)
    {
        if (!m_highlight.IsNull())
            m_graphics.context()->Remove(m_highlight, false);
        m_highlight = presentation::makeSolidPresentation(shape);
        presentation::styleSelectedShape(m_highlight, devicePixelRatioF());
    }
    const auto found = std::find_if(m_visuals.begin(), m_visuals.end(),
                                    [&](const auto& v) { return v.id == m_selected; });
    if (replaced)
    {
        m_highlight->SetLocalTransformation(found->placement);
        m_graphics.context()->Display(m_highlight, AIS_Shaded, -1, false);
    }
    else if (!samePose(m_highlight->LocalTransformation(), found->placement))
        m_graphics.context()->SetLocation(m_highlight, TopLoc_Location(found->placement));
}
void AssemblyViewport::setReferenceHighlights(const std::vector<AssemblyVisual>& references)
{
    if (!isReady())
        return;
    std::vector<AssemblyVisual> valid;
    for (const auto& ref : references)
        if (!ref.shape.IsNull())
            valid.push_back(ref);
    const auto context = m_graphics.context();
    bool changed = false;
    while (m_references.size() > valid.size())
    {
        context->Remove(m_references.back(), false);
        m_references.pop_back();
        changed = true;
    }
    for (std::size_t i = 0; i < valid.size(); ++i)
    {
        const auto& ref = valid[i];
        const bool replace = i >= m_references.size() ||
                             !m_referenceVisuals[i].shape.IsSame(ref.shape) ||
                             m_referenceVisuals[i].filled != ref.filled ||
                             m_referenceVisuals[i].preview != ref.preview;
        if (replace)
        {
            if (i < m_references.size())
                context->Remove(m_references[i], false);
            else
                m_references.emplace_back();
            auto& item = m_references[i];
            if (ref.filled)
            {
                item = presentation::makeSolidPresentation(ref.shape);
                presentation::styleSelectedShape(item, devicePixelRatioF());
            }
            else
            {
                item = new AIS_Shape(presentation::selectionOutline(ref.shape));
                item->SetColor(ref.preview ? Quantity_Color(Quantity_NOC_YELLOW)
                                           : presentation::selectionBlue());
                item->SetWidth(2 * devicePixelRatioF());
            }
            item->SetLocalTransformation(ref.placement);
            context->Display(item, ref.filled ? AIS_Shaded : AIS_WireFrame, -1, false);
            changed = true;
        }
        else if (!samePose(m_references[i]->LocalTransformation(), ref.placement))
        {
            context->SetLocation(m_references[i], TopLoc_Location(ref.placement));
            changed = true;
        }
    }
    m_referenceVisuals = std::move(valid);
    if (changed)
        update();
}
AssemblyHit AssemblyViewport::hit(const QPointF& point)
{
    m_hitPoint.reset();
    if (!isReady())
        return {};
    m_graphics.synchronizeSurface();
    const auto pixel = native(point);
    const auto selector = m_graphics.context()->MainSelector();
    const int tolerance = qMax(1, qRound(8 * devicePixelRatioF()));
    if (selector->CustomPixelTolerance() != tolerance)
    {
        selector->SetPixelTolerance(tolerance + 1);
        selector->SetPixelTolerance(tolerance);
    }
    selector->SetPickClosest(false);
    selector->SetDepthTolerance(SelectMgr_TypeOfDepthTolerance_UniformPixels,
                                8 * devicePixelRatioF());
    selector->Pick(pixel.x(), pixel.y(), m_graphics.view());
    for (int index = 1; index <= selector->NbPicked(); ++index)
    {
        const auto owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(index));
        if (owner.IsNull())
            continue;
        for (const auto& [id, item] : m_presentations)
            if (owner->Selectable() == item && m_graphics.context()->IsDisplayed(item))
            {
                TopTools_IndexedMapOfShape faces;
                const auto kind = owner->Shape().ShapeType();
                if (kind != TopAbs_VERTEX && kind != TopAbs_EDGE && kind != TopAbs_FACE)
                    continue;
                TopExp::MapShapes(item->Shape(), kind, faces);
                m_hitPoint = selector->PickedPoint(index);
                return {id, faces.FindIndex(owner->Shape()), kind};
            }
    }
    return {};
}
void AssemblyViewport::hover(const QPointF& point)
{
    const auto selected = hit(point);
    if (selected.first == m_hovered && selected.second == m_hoveredFace &&
        selected.kind == m_hoveredKind)
        return;
    m_hovered = selected.first;
    m_hoveredFace = selected.second;
    m_hoveredKind = selected.kind;
    if (!m_hover.IsNull())
        m_graphics.context()->Remove(m_hover, false);
    m_hover.Nullify();
    const auto shape = selectedShape(m_hovered, m_hoveredFace, m_hoveredKind);
    if (shape.IsNull())
    {
        update();
        return;
    }
    m_hover = new AIS_Shape(presentation::selectionOutline(shape));
    m_hover->SetColor(presentation::hoverOrange());
    m_hover->SetWidth(2 * devicePixelRatioF());
    const auto visual = std::find_if(m_visuals.begin(), m_visuals.end(),
                                     [&](const auto& v) { return v.id == m_hovered; });
    m_hover->SetLocalTransformation(visual->placement);
    m_graphics.context()->Display(m_hover, AIS_WireFrame, -1, false);
    update();
}
gp_Pnt AssemblyViewport::cursorPoint(const QPointF& point) const
{
    if (!isReady())
        return {};
    const auto pixel = native(point);
    double x, y, z;
    m_graphics.view()->Convert(pixel.x(), pixel.y(), x, y, z);
    return {x, y, z};
}
gp_Pnt AssemblyViewport::cursorPointAtDepth(const QPointF& point, const gp_Pnt& planePoint) const
{
    if (!isReady())
        return {};
    const auto pixel = native(point);
    double x, y, z, dx, dy, dz;
    m_graphics.view()->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Pnt projected(x, y, z);
    const gp_Dir rayDirection(dx, dy, dz);
    const gp_Dir viewDirection = camera()->Direction();
    const double eyeDepth = gp_Vec(camera()->Eye(), projected).Dot(gp_Vec(viewDirection));
    const gp_Pnt rayOrigin = projected.Translated(-gp_Vec(viewDirection) * eyeDepth);
    const gp_Vec ray(rayDirection);
    const double denominator = ray.Dot(gp_Vec(viewDirection));
    if (std::abs(denominator) < 1e-12)
        return planePoint;
    const double distance = gp_Vec(rayOrigin, planePoint).Dot(gp_Vec(viewDirection)) / denominator;
    return rayOrigin.Translated(ray * distance);
}
QPoint AssemblyViewport::project(const gp_Pnt& point) const
{
    if (!isReady())
        return {};
    int x, y;
    m_graphics.view()->Convert(point.X(), point.Y(), point.Z(), x, y);
    return {qRound(x / devicePixelRatioF()), qRound(y / devicePixelRatioF())};
}
void AssemblyViewport::fitAll()
{
    if (!isReady())
    {
        m_fitPending = true;
        return;
    }
    m_fitPending = false;
    m_navigation.resetAnchor();
    m_graphics.view()->FitAll(0.05, false);
    m_graphics.view()->ZFitAll();
    update();
}
void AssemblyViewport::standardView(int index)
{
    if (!isReady())
        return;
    const gp_Dir normals[]{gp_Dir(0, 0, 1), gp_Dir(0, 0, -1), gp_Dir(-1, 0, 0), gp_Dir(1, 0, 0),
                           gp_Dir(0, 1, 0), gp_Dir(0, -1, 0), gp_Dir(1, 1, 1)};
    if (index < 0 || index > 6)
        return;
    auto view = camera();
    view->SetDirection(normals[index].Reversed());
    view->SetUp(index == 4 ? gp_Dir(0, 0, -1) : index == 5 ? gp_Dir(0, 0, 1) : gp_Dir(0, 1, 0));
    view->OrthogonalizeUp();
    m_navigation.resetAnchor();
    m_graphics.view()->ZFitAll();
    update();
}
void AssemblyViewport::mousePressEvent(QMouseEvent* event)
{
    m_touchpad.reset();
    if (!isReady())
        return;
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton && m_dragId.empty())
    {
        auto pivot = camera()->Center();
        (void)hit(event->position());
        const auto selector = m_graphics.context()->MainSelector();
        if (selector->NbPicked())
            pivot = selector->PickedPoint(1);
        m_navigation.begin(event->modifiers(), event->position(), native(event->position()), pivot,
                           pivot);
        grabMouse();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
        return;
    if (!m_dragId.empty()) return;
    if (event->button() == Qt::RightButton && (m_inserting || m_matePicking))
        return;
    if (m_inserting)
    {
        emit insertionConfirmed();
        return;
    }
    const auto pickedValue = hit(event->position());
    auto visual = std::find_if(m_visuals.begin(), m_visuals.end(),
                               [&](const auto& v) { return v.id == pickedValue.first; });
    const auto value = visual == m_visuals.end() ? AssemblyVisual{} : *visual;
    emit entityPicked(QString::fromStdString(pickedValue.first), pickedValue.kind,
                      pickedValue.second, event->modifiers());
    if (pickedValue.kind == TopAbs_FACE)
        emit picked(QString::fromStdString(pickedValue.first), pickedValue.second,
                    event->modifiers());
    if (!m_matePicking && !value.id.empty() && !value.fixed &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        m_dragId = value.id;
        m_dragButton = event->button();
        m_press = event->position();
        m_dragPoint = m_hitPoint.value_or(cursorPoint(m_press));
        m_dragLocalPoint = m_dragPoint.Transformed(value.placement.Inverted());
        const double unitsPerPixel = cursorPointAtDepth(m_press, m_dragPoint).Distance(
            cursorPointAtDepth(m_press + QPointF(1, 0), m_dragPoint));
        m_dragTolerance = std::max(1e-5, 0.5 * unitsPerPixel);
        // Ignore small transverse hand wobble when the mates also permit sliding.
        m_dragRotationDeadband = 6.0 * unitsPerPixel;
        m_dragMoved = false;
        m_dragPrevious = m_press;
        if (m_dragButton == Qt::RightButton)
        {
            Bnd_Box bounds;
            BRepBndLib::Add(value.shape, bounds);
            m_dragLocalPoint = bounds.IsVoid() ? gp_Pnt() :
                gp_Pnt((bounds.CornerMin().XYZ() + bounds.CornerMax().XYZ()) * 0.5);
            m_rotationCenter = project(m_dragLocalPoint.Transformed(value.placement));
            m_rotationRadius = std::max(60.0, 0.35 * std::min(width(), height()));
        }
    }
}
void AssemblyViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (!isReady())
        return;
    if (m_navigation.mode() != viewport::CameraNavigation::Mode::None)
    {
        m_navigation.move(m_graphics.view(), event->position(), native(event->position()), size(),
                          {});
        update();
        return;
    }
    if (m_touchpad.move(event,m_graphics.view(),size(),devicePixelRatioF(),
        isActiveWindow() && m_dragId.empty(),[this,event]{return navigationPivot(event->position());}))
    {
        update();return;
    }
    if (m_inserting)
    {
        emit insertionMoved(cursorPoint(event->position()));
        return;
    }
    if (!m_dragId.empty() && event->buttons().testFlag(m_dragButton))
    {
        if (!m_dragMoved &&
            (event->position() - m_press).manhattanLength() < QApplication::startDragDistance())
            return;
        if (!m_dragMoved)
        {
            m_dragMoved = true;
            emit dragStarted();
            grabMouse();
        }
        if (std::hypot(event->position().x() - m_dragPrevious.x(),
                       event->position().y() - m_dragPrevious.y()) < 1.0)
            return;
        updateComponentDrag(event->position());
        return;
    }
    hover(event->position());
}
void AssemblyViewport::updateComponentDrag(const QPointF& position)
{
    if (m_dragButton == Qt::RightButton)
    {
        const auto found = std::find_if(m_visuals.begin(), m_visuals.end(),
            [&](const auto& visual) { return visual.id == m_dragId; });
        if (found == m_visuals.end()) return;
        const auto delta = viewport::componentRotationDrag(m_dragPrevious, position,
            m_rotationCenter, m_rotationRadius, camera()->Direction(), camera()->Up());
        emit dragRotationRequested(QString::fromStdString(m_dragId), m_dragLocalPoint,
                                   delta * found->placement.GetRotation());
    }
    else
        emit dragPointRequested(QString::fromStdString(m_dragId), m_dragLocalPoint,
                                cursorPointAtDepth(position, m_dragPoint),
                                camera()->Direction(), m_dragTolerance, m_dragRotationDeadband);
    m_dragPrevious = position;
}
void AssemblyViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_navigation.stop();
        if (QWidget::mouseGrabber() == this)
            releaseMouse();
        return;
    }
    if (event->button() == m_dragButton && !m_dragId.empty())
    {
        const bool moved = m_dragMoved;
        if (moved && event->position() != m_dragPrevious)
            updateComponentDrag(event->position());
        m_dragId.clear();
        m_dragButton = Qt::NoButton;
        m_dragMoved = false;
        if (QWidget::mouseGrabber() == this)
            releaseMouse();
        if (moved)
            emit dragFinished(true);
    }
}
void AssemblyViewport::wheelEvent(QWheelEvent* event)
{
    if (!m_dragId.empty()) { event->accept(); return; }
    if (!isReady())
        return;
    if (m_touchpad.wheel(event,m_graphics.view(),size(),devicePixelRatioF(),
        [this,event]{return navigationPivot(event->position());},
        [this](QPointF point,double factor){touchpadZoom(point,factor);}))
    { update();return; }
    if (!event->angleDelta().y()) return;
    const double factor = std::pow(1.15, -event->angleDelta().y() / 120.0);
    if (factor > 1)
        viewport::CameraNavigation::zoom(m_graphics.view(), native(event->position()), factor,
                                         cursorPoint(event->position()));
    else
        viewport::CameraNavigation::zoomCentered(m_graphics.view(), factor);
    update();
    event->accept();
}
gp_Pnt AssemblyViewport::navigationPivot(const QPointF& position)
{
    (void)hit(position);
    const auto selector = m_graphics.context()->MainSelector();
    return selector->NbPicked() ? selector->PickedPoint(1) : camera()->Center();
}
void AssemblyViewport::touchpadZoom(QPointF position,double factor)
{
    if (factor>1) viewport::CameraNavigation::zoom(m_graphics.view(),native(position),factor,cursorPoint(position));
    else viewport::CameraNavigation::zoomCentered(m_graphics.view(),factor);
}
bool AssemblyViewport::event(QEvent* event)
{
    if (event->type()==QEvent::NativeGesture && isReady() && m_dragId.empty() &&
        m_navigation.mode()==viewport::CameraNavigation::Mode::None &&
        m_touchpad.gesture(static_cast<QNativeGestureEvent*>(event),m_graphics.view(),devicePixelRatioF(),
            [this](QPointF point,double factor){touchpadZoom(point,factor);}))
    {update();return true;}
    if (event->type()==QEvent::Leave || event->type()==QEvent::FocusOut ||
        event->type()==QEvent::WindowDeactivate || event->type()==QEvent::UngrabMouse ||
        event->type()==QEvent::DevicePixelRatioChange || event->type()==QEvent::ScreenChangeInternal ||
        event->type()==QEvent::KeyRelease || event->type()==QEvent::KeyPress)
        m_touchpad.reset();
    return QWidget::event(event);
}
void AssemblyViewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        if (!m_dragId.empty())
        {
            m_dragId.clear();
            m_dragButton = Qt::NoButton;
            m_dragMoved = false;
            if (QWidget::mouseGrabber() == this)
                releaseMouse();
            emit dragFinished(false);
        }
        else
            emit canceled();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
} // namespace thecad::ui
