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
        const auto selector=m_context->MainSelector();for(int n=1;n<=selector->NbPicked();++n)if(!picked.owner.IsNull()&&selector->Picked(n)==picked.owner)return selector->PickedPoint(n);
    }
    return m_rotationAnchor.value_or(m_view->Camera()->Center());
}
void CadViewport::beginNavigation(QMouseEvent* event){
    if(m_pointDrag||m_dimensionDrag){event->accept();return;}
    m_navigation=event->modifiers().testFlag(Qt::ControlModifier)?Navigation::Pan:event->modifiers().testFlag(Qt::ShiftModifier)?Navigation::Zoom:Navigation::Orbit;
    m_navigationButton=Qt::MiddleButton;m_navigationLogical=event->position();m_previousPosition=nativePosition(event->position());
    if(m_navigation==Navigation::Orbit){m_orbitPivot=navigationPivot(event->position());m_rotationAnchor=m_orbitPivot;}
    if(m_navigation==Navigation::Zoom&&!m_view->Camera()->IsOrthographic())m_zoomDepth=navigationPivot(event->position());
    m_regionClickCandidate=false;m_planeClickCandidate=false;m_contextClick=false;previewCandidate({});resetSketchInference();setFocus(Qt::MouseFocusReason);grabMouse();setCursor(m_navigation==Navigation::Pan?Qt::ClosedHandCursor:Qt::SizeAllCursor);event->accept();
}
void CadViewport::moveNavigation(QMouseEvent* event){
    const auto delta=event->position()-m_navigationLogical;const auto pixel=nativePosition(event->position());
    if(m_navigation==Navigation::Orbit){const auto camera=m_view->Camera();const auto up=camera->Up(),right=gp_Dir(gp_Vec(camera->Direction()).Crossed(gp_Vec(up)));const double speed=3.141592653589793/std::max(240,std::min(width(),height()));gp_Trsf yaw,pitch;yaw.SetRotation(gp_Ax1(m_orbitPivot,up),-delta.x()*speed);pitch.SetRotation(gp_Ax1(m_orbitPivot,right),-delta.y()*speed);camera->Transform(pitch*yaw);}
    else if(m_navigation==Navigation::Pan){const auto change=pixel-m_previousPosition;m_view->Pan(change.x(),-change.y());}
    else if(m_navigation==Navigation::Zoom)zoomToward(m_navigationLogical,std::exp(std::clamp(-delta.y()*0.012,-2.0,2.0)));
    m_previousPosition=pixel;m_navigationLogical=event->position();m_cameraAnnotationsDirty=true;update();event->accept();
}
void CadViewport::zoomToward(const QPointF& position,double factor){
    if(!isReady()||!std::isfinite(factor)||factor<=0)return;
    const auto camera=m_view->Camera();const auto oldScale=camera->Scale();const double next=std::clamp(oldScale/factor,1e-6,1e12);
    const auto pixel=nativePosition(position);const auto planePoint=[&](gp_Pnt planeOrigin){double x,y,z,dx,dy,dz;m_view->ConvertWithProj(pixel.x(),pixel.y(),x,y,z,dx,dy,dz);const gp_Vec ray(dx,dy,dz),normal(camera->Direction());const double dot=ray.Dot(normal);if(std::abs(dot)<1e-12)return planeOrigin;const gp_Pnt p(x,y,z);return p.Translated(ray*(gp_Vec(p,planeOrigin).Dot(normal)/dot));};
    gp_Pnt anchor=planePoint(camera->Center());
    if(m_sketchEditing&&m_sketch){if(const auto point=sketchPosition(position))anchor=modeling::sketchPoint(*m_sketch,*point);}
    else if(!camera->IsOrthographic()){
        // Orthographic cursor zoom is independent of model depth. In perspective,
        // pick depth once per drag instead of intersecting the BRep on every move.
        if(m_navigation==Navigation::Zoom)anchor=planePoint(m_zoomDepth);
        else{const auto candidates=selectionCandidates(position);if(!candidates.empty()&&candidates.front().hit)anchor=planePoint(*candidates.front().hit);}
    }
    camera->SetScale(next);const auto after=planePoint(anchor);gp_Trsf translation;translation.SetTranslation(gp_Vec(after,anchor));camera->Transform(translation);

}
void CadViewport::updateCameraAnnotations(){
    if(!m_cameraAnnotationsDirty||!isReady())return;
    m_cameraAnnotationsDirty=false;
    if(!m_sketch)return;
    const double unit=m_view->Camera()->Scale()/std::max(1,height());
    // Dimension text/geometry already lives on the sketch plane. Orbit/pan only
    // changes its projection; rebuild pixel-sized arrows when zoom changes.
    if(std::abs(unit-m_annotationUnit)>std::abs(unit)*1e-10)
        updateConstraintPresentation(false);
    updateRelationPositions();
}
void CadViewport::normalTo(){
    if(!isReady())return;m_rotationAnchor.reset();
    if(m_sketch&&(m_sketchEditing||m_regionSelecting)){orientSketch();update();return;}
    std::optional<gp_Ax3> frame;
    if(m_selectedPlane)frame=modeling::sketchAxes(*m_selectedPlane);
    for(const auto& d:m_datums)if(d.id==m_selectedDatum&&d.geometry.kind==modeling::DatumKind::Plane)frame=d.geometry.frame;
    if(m_selectedSolidFace>0){prepareBodyPicking();if(m_selectedSolidFace<=m_selectionFaces.Extent()){BRepAdaptor_Surface surface(TopoDS::Face(m_selectionFaces(m_selectedSolidFace)));if(surface.GetType()==GeomAbs_Plane)frame=modeling::planarFaceAxes(TopoDS::Face(m_selectionFaces(m_selectedSolidFace)));}}
    if(frame){const auto n=frame->Direction(),u=frame->YDirection();const auto p=frame->Location();m_view->SetAt(p.X(),p.Y(),p.Z());m_view->SetProj(n.X(),n.Y(),n.Z());m_view->SetUp(u.X(),u.Y(),u.Z());m_view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);update();}
}
}
