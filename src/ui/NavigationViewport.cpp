#include "ui/CadViewport.h"
#include <Graphic3d_Camera.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <cmath>
#include <algorithm>
namespace thecad::ui {
gp_Pnt CadViewport::navigationPivot(const QPointF& position){
    const QScopedValueRollback<bool> editing(m_sketchEditing,false);
    const auto candidates=selectionCandidates(position);
    if(!candidates.empty()){
        const auto& picked=candidates.front();if(picked.hit)return *picked.hit;if(picked.shape.ShapeType()==TopAbs_VERTEX)return BRep_Tool::Pnt(TopoDS::Vertex(picked.shape));
        const auto selector=m_graphics.context()->MainSelector();for(int n=1;n<=selector->NbPicked();++n)if(!picked.owner.IsNull()&&selector->Picked(n)==picked.owner)return selector->PickedPoint(n);
    }
    return m_camera.anchor().value_or(m_graphics.view()->Camera()->Center());
}
void CadViewport::beginNavigation(QMouseEvent* event){
    if(m_pointDrag||m_dimensionDrag){event->accept();return;}
    const auto mode=viewport::CameraNavigation::modeFor(event->modifiers());
    const auto pivot=mode==Navigation::Orbit?navigationPivot(event->position()):gp_Pnt();
    const auto zoomDepth=mode==Navigation::Zoom&&!m_graphics.view()->Camera()->IsOrthographic()?navigationPivot(event->position()):m_graphics.view()->Camera()->Center();
    m_camera.begin(event->modifiers(),event->position(),nativePosition(event->position()),pivot,zoomDepth);
    m_regionClickCandidate=false;m_planeClickCandidate=false;m_contextClick=false;previewCandidate({});resetSketchInference();setFocus(Qt::MouseFocusReason);grabMouse();setCursor(mode==Navigation::Pan?Qt::ClosedHandCursor:Qt::SizeAllCursor);event->accept();
}
void CadViewport::moveNavigation(QMouseEvent* event){
    std::optional<gp_Pnt> depth;
    if(m_camera.mode()==Navigation::Zoom&&m_sketchEditing&&m_sketch){
        depth=m_graphics.view()->Camera()->Center();
        if(const auto point=sketchPosition(m_camera.position()))depth=modeling::sketchPoint(*m_sketch,*point);
    }
    m_camera.move(m_graphics.view(),event->position(),nativePosition(event->position()),size(),depth);
    m_cameraAnnotationsDirty=true;update();event->accept();
}
void CadViewport::zoomToward(const QPointF& position,double factor){
    if(!isReady())return;
    auto depth=m_graphics.view()->Camera()->Center();
    if(m_sketchEditing&&m_sketch){if(const auto point=sketchPosition(position))depth=modeling::sketchPoint(*m_sketch,*point);}
    else if(!m_graphics.view()->Camera()->IsOrthographic()){
        const auto candidates=selectionCandidates(position);if(!candidates.empty()&&candidates.front().hit)depth=*candidates.front().hit;
    }
    viewport::CameraNavigation::zoom(m_graphics.view(),nativePosition(position),factor,depth);
}
void CadViewport::updateCameraAnnotations(){
    if(!m_cameraAnnotationsDirty||!isReady())return;
    m_cameraAnnotationsDirty=false;
    if(!m_sketch)return;
    const double unit=m_graphics.view()->Camera()->Scale()/std::max(1,height());
    // Dimension text/geometry already lives on the sketch plane. Orbit/pan only
    // changes its projection; rebuild pixel-sized arrows when zoom changes.
    if(std::abs(unit-m_dimensions.unit())>std::abs(unit)*1e-10)
        updateConstraintPresentation(false);
    updateRelationPositions();
}
void CadViewport::normalTo(){
    if(!isReady())return;m_camera.resetAnchor();
    if(m_sketch&&(m_sketchEditing||m_regionSelecting)){orientSketch();update();return;}
    std::optional<gp_Ax3> frame;
    if(m_selectedPlane)frame=modeling::sketchAxes(*m_selectedPlane);
    for(const auto& d:m_datums)if(d.id==m_selectedDatum&&d.geometry.kind==modeling::DatumKind::Plane)frame=d.geometry.frame;
    if(m_selectedSolidFace>0){prepareBodyPicking();if(m_selectedSolidFace<=m_selectionFaces.Extent()){BRepAdaptor_Surface surface(TopoDS::Face(m_selectionFaces(m_selectedSolidFace)));if(surface.GetType()==GeomAbs_Plane)frame=modeling::planarFaceAxes(TopoDS::Face(m_selectionFaces(m_selectedSolidFace)));}}
    if(frame){const auto n=frame->Direction(),u=frame->YDirection();const auto p=frame->Location();m_graphics.view()->SetAt(p.X(),p.Y(),p.Z());m_graphics.view()->SetProj(n.X(),n.Y(),n.Z());m_graphics.view()->SetUp(u.X(),u.Y(),u.Z());m_graphics.view()->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);update();}
}
}
