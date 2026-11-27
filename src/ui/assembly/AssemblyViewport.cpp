#include "ui/assembly/AssemblyViewport.h"
#include "viewport/ComponentRotationDrag.h"
#include "ui/Theme.h"
#include "presentation/SolidPresentation.h"
#include "presentation/SelectionAppearance.h"
#include <AIS_Shape.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <QApplication>
#include <QKeyEvent>
#include <QLineF>
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

AssemblyViewport::AssemblyViewport(QWidget* parent) : ViewportWidget(parent)
{
    setObjectName("assemblyViewport");
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &AssemblyViewport::applyTheme);
}
AssemblyViewport::~AssemblyViewport() = default;
void AssemblyViewport::initialize()
{
    m_attempted = true;
    try
    {
        initializeGraphics();
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
void AssemblyViewport::showEvent(QShowEvent* event)
{
    if (!m_attempted)
        initialize();
    ViewportWidget::showEvent(event);
}
void AssemblyViewport::paintEvent(QPaintEvent*)
{
    if (!m_attempted)
        initialize();
    if (isReady())
    {
        synchronizeGraphics();
        redrawGraphics();
        emit framePresented();
    }
}
void AssemblyViewport::applyTheme()
{
    if (!isReady())
        return;
    applyCommonViewportTheme();
    update();
}
void AssemblyViewport::setComponents(const std::vector<AssemblyVisual>& visuals)
{
    const bool unchanged =
        visuals.size() == m_visuals.size() &&
        std::equal(visuals.begin(), visuals.end(), m_visuals.begin(),
                   [](const auto& a, const auto& b)
                   {
                       const bool sameShape =
                           (a.shape.IsNull() && b.shape.IsNull()) ||
                           (!a.shape.IsNull() && !b.shape.IsNull() && a.shape.IsSame(b.shape));
                       return a.id == b.id && sameShape && a.visible == b.visible &&
                              a.fixed == b.fixed && a.preview == b.preview &&
                              a.constrained == b.constrained && samePose(a.placement, b.placement);
                   });
    m_visuals = visuals;
    std::vector<viewport::PickableBRep> pickable;
    pickable.reserve(visuals.size());
    for (const auto& visual : visuals)
        pickable.push_back({visual.id, visual.shape, visual.placement, visual.visible});
    m_scenePicker.setScene(std::move(pickable));
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
        if (visual.shape.IsNull())
        {
            if (!item.IsNull())
            {
                context->Remove(item, false);
                item.Nullify();
            }
            continue;
        }
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
    const auto pixel = nativePosition(point);
    const auto selector = m_graphics.context()->MainSelector();
    const int tolerance = qMax(1, qRound(8 * devicePixelRatioF()));
    if (selector->CustomPixelTolerance() != tolerance)
    {
        selector->SetPixelTolerance(tolerance + 1);
        selector->SetPixelTolerance(tolerance);
    }
    // Keep the screen-space tolerance for usability, but let real depth decide
    // which overlapping component is in front. A pixel-based depth allowance
    // made rear edges compete with visible faces as if they were coplanar.
    selector->SetPickClosest(true);
    selector->SetDepthTolerance(SelectMgr_TypeOfDepthTolerance_SensitivityFactor, 0.0);
    selector->Pick(pixel.x(), pixel.y(), m_graphics.view());

    const auto view = m_graphics.view();
    const auto viewCamera = camera();
    const gp_Vec cameraDirection(viewCamera->Direction());
    const auto cursorSurface = m_scenePicker.surfaceAt(view, pixel);
    const double depthSlack = std::max(1e-6, viewCamera->Scale() / std::max(1, height()));
    for (int index = 1; index <= selector->NbPicked(); ++index)
    {
        const auto owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(index));
        if (owner.IsNull() || !owner->HasShape())
            continue;
        for (const auto& [id, item] : m_presentations)
            if (owner->Selectable() == item && m_graphics.context()->IsDisplayed(item))
            {
                const auto kind = owner->Shape().ShapeType();
                if (kind != TopAbs_VERTEX && kind != TopAbs_EDGE && kind != TopAbs_FACE)
                    continue;
                TopTools_IndexedMapOfShape subshapes;
                TopExp::MapShapes(item->Shape(), kind, subshapes);
                const int subshapeIndex = subshapes.FindIndex(owner->Shape());
                if (subshapeIndex <= 0)
                    continue;
                const auto visual = std::find_if(m_visuals.begin(), m_visuals.end(),
                                                 [&](const auto& value) {
                                                     return value.id == id && value.visible;
                                                 });
                if (visual == m_visuals.end())
                    continue;

                gp_Pnt entityPoint = selector->PickedPoint(index);
                double cursorDistance = 0.0;
                if (kind == TopAbs_VERTEX)
                    entityPoint = BRep_Tool::Pnt(TopoDS::Vertex(owner->Shape()))
                                      .Transformed(visual->placement);
                else if (kind == TopAbs_EDGE)
                {
                    const auto edgeHit = viewport::BRepScenePicker::closestDisplayedEdgePoint(
                        TopoDS::Edge(owner->Shape()), visual->placement, view, point,
                        devicePixelRatioF());
                    if (edgeHit)
                    {
                        entityPoint = edgeHit->point;
                        cursorDistance = edgeHit->distance;
                    }
                    else
                    {
                        int entityX, entityY;
                        view->Convert(entityPoint.X(), entityPoint.Y(), entityPoint.Z(), entityX,
                                      entityY);
                        cursorDistance = QLineF(
                            point, QPointF(entityX / devicePixelRatioF(),
                                           entityY / devicePixelRatioF())).length();
                    }
                }

                const double entityDepth =
                    gp_Vec(viewCamera->Eye(), entityPoint).Dot(cameraDirection);
                if (kind == TopAbs_FACE)
                {
                    const bool isCursorFace = cursorSurface && cursorSurface->id == id &&
                                              cursorSurface->faceIndex == subshapeIndex;
                    if (cursorSurface && !isCursorFace &&
                        entityDepth > cursorSurface->depth + depthSlack)
                        continue;
                    if (isCursorFace)
                        entityPoint = cursorSurface->point;
                }
                else
                {
                    if (cursorDistance > 8.0)
                        continue;
                    if (m_scenePicker.occludes(view, entityPoint, depthSlack))
                        continue;
                }

                m_hitPoint = entityPoint;
                return {id, subshapeIndex, kind};
            }
    }
    if (cursorSurface)
    {
        m_hitPoint = cursorSurface->point;
        return {cursorSurface->id, cursorSurface->faceIndex, TopAbs_FACE, cursorSurface->point};
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
    const auto pixel = nativePosition(point);
    double x, y, z;
    m_graphics.view()->Convert(pixel.x(), pixel.y(), x, y, z);
    return {x, y, z};
}
gp_Pnt AssemblyViewport::cursorPointAtDepth(const QPointF& point, const gp_Pnt& planePoint) const
{
    if (!isReady())
        return {};
    const auto pixel = nativePosition(point);
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
    fitScene(0.1);
}
void AssemblyViewport::standardView(int index)
{
    if (index < 0 || index > 6)
        return;
    setStandardView(ViewportWidget::StandardView(index));
}
void AssemblyViewport::mousePressEvent(QMouseEvent* event)
{
    m_touchpad.reset();
    if (!isReady())
        return;
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton && m_dragId.empty())
    {
        if (beginCameraNavigation(event))
            return;
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
    if (m_camera.mode() != viewport::CameraNavigation::Mode::None)
    {
        moveCameraNavigation(event);
        return;
    }
    if (handleTouchpadPointerMove(event, isActiveWindow() && m_dragId.empty()))
    {
        return;
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
    if (endCameraNavigation(event))
        return;
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
    handleWheelNavigation(event);
}
gp_Pnt AssemblyViewport::navigationPivot(const QPointF& position)
{
    (void)hit(position);
    return m_hitPoint.value_or(camera()->Center());
}
gp_Pnt AssemblyViewport::zoomAnchor(const QPointF& position)
{
    (void)hit(position);
    return m_hitPoint.value_or(camera()->Center());
}
bool AssemblyViewport::cameraNavigationAllowed() const
{
    return m_dragId.empty();
}
bool AssemblyViewport::event(QEvent* event)
{
    return ViewportWidget::event(event);
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
