#include "ui/CadViewport.h"

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Graphic3d_Camera.hxx>
#include <QScopedValueRollback>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
gp_Pnt CadViewport::navigationPivot(const QPointF& position)
{
    const QScopedValueRollback<bool> editing(m_sketchEditing, false);
    const auto candidates = selectionCandidates(position);
    if (!candidates.empty())
    {
        const auto& picked = candidates.front();
        if (picked.hit)
            return *picked.hit;
        if (picked.shape.ShapeType() == TopAbs_VERTEX)
            return BRep_Tool::Pnt(TopoDS::Vertex(picked.shape));
        const auto selector = m_graphics.context()->MainSelector();
        for (int n = 1; n <= selector->NbPicked(); ++n)
            if (!picked.owner.IsNull() && selector->Picked(n) == picked.owner)
                return selector->PickedPoint(n);
    }
    return m_camera.anchor().value_or(camera()->Center());
}

gp_Pnt CadViewport::zoomAnchor(const QPointF& position)
{
    if (m_sketchEditing && m_sketch)
        if (const auto point = sketchPosition(position))
            return modeling::sketchPoint(*m_sketch, *point);
    if (!camera()->IsOrthographic())
    {
        const auto candidates = selectionCandidates(position);
        if (!candidates.empty() && candidates.front().hit)
            return *candidates.front().hit;
    }
    return camera()->Center();
}

bool CadViewport::cameraNavigationAllowed() const
{
    return !m_pointDrag && !m_dimensionDrag;
}

void CadViewport::cameraViewChanged()
{
    m_cameraAnnotationsDirty = true;
}

void CadViewport::updateCameraAnnotations()
{
    if (!m_cameraAnnotationsDirty || !isReady())
        return;
    m_cameraAnnotationsDirty = false;
    if (!m_sketch)
        return;
    const double unit = m_graphics.view()->Camera()->Scale() / std::max(1, height());
    if (std::abs(unit - m_dimensions.unit()) > std::abs(unit) * 1e-10)
        updateConstraintPresentation(false);
    updateRelationPositions();
}

void CadViewport::prepareFeatureView(const std::optional<gp_Ax3>& profile)
{
    if (!isReady())
        return;
    const auto cameraObject = camera();
    const gp_Ax3 frame = profile.value_or(gp_Ax3());
    const auto direction = cameraObject->Direction();
    const double normalAlignment = std::abs(direction.Dot(frame.Direction()));
    const bool needsAngle = profile ? normalAlignment > 0.95 || normalAlignment < 0.2
                                    : std::max({normalAlignment,
                                                std::abs(direction.Dot(frame.XDirection())),
                                                std::abs(direction.Dot(frame.YDirection()))}) > 0.95;
    if (!needsAngle)
        return;
    m_camera.resetAnchor();
    const gp_Dir towardViewer(gp_Vec(frame.XDirection()) + gp_Vec(frame.YDirection()) +
                              gp_Vec(frame.Direction()));
    cameraObject->SetDirection(towardViewer.Reversed());
    cameraObject->SetUp(frame.YDirection());
    cameraObject->OrthogonalizeUp();
    m_graphics.view()->ZFitAll();
    cameraViewChanged();
    update();
}

void CadViewport::normalTo()
{
    if (!isReady())
        return;
    m_camera.resetAnchor();
    if (m_sketch && (m_sketchEditing || m_regionSelecting))
    {
        orientSketch();
        update();
        return;
    }
    std::optional<gp_Ax3> frame;
    if (m_selectedPlane)
        frame = modeling::sketchAxes(*m_selectedPlane);
    for (const auto& datum : m_datums)
        if (datum.id == m_selectedDatum && datum.geometry.kind == modeling::DatumKind::Plane)
            frame = datum.geometry.frame;
    if (m_selectedSolidFace > 0)
    {
        prepareBodyPicking();
        if (m_selectedSolidFace <= m_selectionFaces.Extent())
        {
            BRepAdaptor_Surface surface(TopoDS::Face(m_selectionFaces(m_selectedSolidFace)));
            if (surface.GetType() == GeomAbs_Plane)
                frame = modeling::planarFaceAxes(TopoDS::Face(m_selectionFaces(m_selectedSolidFace)));
        }
    }
    if (frame)
    {
        const auto normal = frame->Direction();
        const auto up = frame->YDirection();
        const auto location = frame->Location();
        m_graphics.view()->SetAt(location.X(), location.Y(), location.Z());
        m_graphics.view()->SetProj(normal.X(), normal.Y(), normal.Z());
        m_graphics.view()->SetUp(up.X(), up.Y(), up.Z());
        camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        cameraViewChanged();
        update();
    }
}
} // namespace thecad::ui
