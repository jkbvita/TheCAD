#include "presentation/SelectionAppearance.h"
#include "presentation/SolidPresentation.h"
#include "ui/Theme.h"
#include <QToolTip>
#include "modeling/RelationCommands.h"
#include "modeling/SketchTools.h"
#include "modeling/DimensionAnnotation.h"
#include <QFontMetricsF>
#include "modeling/ConstraintSolver.h"
#include <Geom_Axis2Placement.hxx>
#include <Image_PixMap.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <SelectMgr_IndexedMapOfOwner.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include "ui/CadViewport.h"
#include "modeling/SketchSnapping.h"

#include <AIS_Shape.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Prs3d_PointAspect.hxx>
#include <QUuid>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_GradientFillMethod.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_TypeOfLimit.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Vec.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Bnd_Box.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TCollection_ExtendedString.hxx>
#include <QApplication>
#include <QLineF>
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
#include <set>
#include <map>
#include <tuple>

namespace thecad::ui {
using namespace presentation;
namespace {
// Flat antialiased sprite, with a fine dark rim like the reference endpoints.
// The texture dimensions are physical pixels; model geometry is unchanged.
Handle(Prs3d_PointAspect) filledPoint(const Quantity_Color& color,double diameter)
{
    using Key=std::tuple<double,double,double,double>;
    static std::map<Key,Handle(Prs3d_PointAspect)> cache;
    const Key key{color.Red(),color.Green(),color.Blue(),diameter};
    if(const auto found=cache.find(key);found!=cache.end())return found->second;
    const int size=std::max(5,int(std::ceil(diameter+2)));
    Handle(Image_PixMap) pixels=new Image_PixMap;
    pixels->InitZero(Image_Format_RGBA,size,size);
    const double center=(size-1)*0.5,radius=diameter*0.5;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        const double distance=std::hypot(x-center,y-center);
        const float alpha=float(std::clamp(radius+0.5-distance,0.0,1.0));
        const double fill=std::clamp(radius-0.7-distance,0.0,1.0);
        const Quantity_Color rim(0.025,0.025,0.035,Quantity_TOC_RGB);
        const Quantity_Color pixel(rim.Red()+(color.Red()-rim.Red())*fill,
            rim.Green()+(color.Green()-rim.Green())*fill,rim.Blue()+(color.Blue()-rim.Blue())*fill,Quantity_TOC_RGB);
        pixels->SetPixelColor(x,y,Quantity_ColorRGBA(pixel,alpha));
    }
    Handle(Prs3d_PointAspect) aspect=new Prs3d_PointAspect(new Graphic3d_AspectMarker3d(pixels));
    cache.emplace(key,aspect);return aspect;
}
Handle(AIS_Trihedron) datumArrow(const gp_Pnt& origin,const gp_Dir& direction,double length,const Quantity_Color& color)
{
    const gp_Vec fallback=std::abs(direction.Z())<0.9?gp_Vec(0,0,1):gp_Vec(0,1,0);
    const gp_Dir normal(gp_Vec(direction).Crossed(fallback));
    Handle(AIS_Trihedron) arrow=new AIS_Trihedron(new Geom_Axis2Placement(gp_Ax2(origin,normal,direction)));
    arrow->SetSize(length);arrow->SetDatumDisplayMode(Prs3d_DM_Shaded);
    arrow->Attributes()->DatumAspect()->SetDrawDatumAxes(Prs3d_DatumAxes_XAxis);
    arrow->Attributes()->DatumAspect()->SetDrawLabels(Standard_False);
    arrow->SetDrawArrows(Standard_True);
    arrow->SetDatumPartColor(Prs3d_DatumParts_XAxis,color);
    arrow->SetArrowColor(Prs3d_DatumParts_XAxis,color);
    arrow->SetDatumPartColor(Prs3d_DatumParts_Origin,Quantity_Color(Quantity_NOC_RED));
    // Native datum presentations support shaded arrows and infinite bounds
    // together, preserving Fit All without AIS_Shape's wireframe fallback.
    arrow->SetInfiniteState(Standard_True);
    return arrow;
}
}


CadViewport::CadViewport(const TopoDS_Shape& shape, QWidget* parent)
    : QWidget(parent), m_shape(shape)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    qRegisterMetaType<modeling::SketchEntity>();
    qRegisterMetaType<modeling::Sketch>();
    qRegisterMetaType<modeling::SketchPlane>();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setObjectName("cadViewport");
    setAccessibleName("3D CAD viewport");
    connect(&ThemeManager::instance(),&ThemeManager::changed,this,&CadViewport::applyTheme);
}

void CadViewport::applyTheme(){
    if(!isReady())return;
    const bool dark=ThemeManager::instance().isDark();
    m_graphics.view()->SetBgGradientColors(dark?Quantity_Color(0.12549,0.15686,0.20,Quantity_TOC_sRGB):Quantity_Color(0.90,0.92,0.95,Quantity_TOC_RGB),dark?Quantity_Color(0.20784,0.26275,0.32549,Quantity_TOC_sRGB):Quantity_Color(0.99,0.99,0.99,Quantity_TOC_RGB),Aspect_GFM_VER);
    m_graphics.view()->TriedronDisplay(Aspect_TOTP_LEFT_LOWER,dark?Quantity_NOC_GRAY80:Quantity_NOC_GRAY30,0.08,V3d_ZBUFFER);
    updateSketchPresentation();updateReferencePlanes();setDatums(m_datums);
    if(!m_referenceCandidates.empty()){const auto candidates=m_referenceCandidates;setReferenceCandidates(candidates,m_referenceCandidateKind);}update();
}

CadViewport::~CadViewport()
{
    releaseViewer();
}

bool CadViewport::isReady() const
{
    return !m_graphics.view().IsNull();
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
        m_graphics.initialize(reinterpret_cast<Aspect_Handle>(winId()));
        applyTheme();
        m_graphics.view()->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GRAY30, 0.08, V3d_ZBUFFER);

        m_graphics.context()->SetDisplayMode(AIS_Shaded, Standard_False);
        if (!setShape(m_shape)) {
            Standard_Failure::Raise(m_error.toUtf8().constData());
        }
        updateSketchPresentation();
        updateReferencePlanes();
        setDatums(m_datums);
        if (m_sketchEditing) { orientSketch(); updateSketchAxes(); }
        else if (!m_shape.IsNull() || m_planesVisible || !m_datums.empty()) { resetView(); }
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
    for(auto& lines:m_planeCenterlines) lines.Nullify();
    m_globalOrigin.Nullify();
    for (auto& axis : m_sketchAxes) { axis.Nullify(); }
    m_sketchOrigin.Nullify();
    for(auto& axis:m_globalAxes) axis.Nullify();
    m_pointPresentations.clear();
    m_datumPresentations.clear();m_referenceCandidatePresentations.clear();m_referenceHover.Nullify();m_hoveredReferenceCandidate=-1;m_featureReferenceShapes.clear();
    m_dimensions.clear();m_relationMarkers.clear();m_relationHits.clear();
    m_interactionLabel.Nullify();
    m_measureLabel.Nullify();
    m_measureSpan.Nullify();
    m_inferenceGuides.clear();
    m_selectedEntityPresentation.Nullify();
    m_hoveredEntityPresentation.Nullify();
    m_previewPresentation.Nullify();
    m_materialPreviewPresentations.clear();
    m_selectedContourPresentations.clear();
    m_sketchPresentation.Nullify();m_constructionPresentation.Nullify();
    m_sketchBatches.clear();m_selectedSolidFace=0;
    m_presentation.Nullify();
    m_graphics.reset();
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
        updateCameraAnnotations();
        m_graphics.view()->Redraw();
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
    if(m_graphics.synchronizeSurface())updateConstraintPresentation();
}

bool CadViewport::setShape(const TopoDS_Shape& shape)
{
    m_treeBodySelected=false;invalidateBodySelection();
    setSelectedSolidFace(0);
    if (shape.IsNull()) {
        if (isReady() && !m_presentation.IsNull()) {
            m_graphics.context()->Remove(m_presentation, Standard_False);
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
    Handle(AIS_Shape) candidate;
    try {
        candidate = makeSolidPresentation(shape);
        m_graphics.context()->Display(candidate, Standard_False);
        m_graphics.context()->Deactivate(candidate);
        for(const auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX,TopAbs_SOLID})
            m_graphics.context()->Activate(candidate,AIS_Shape::SelectionMode(type),Standard_False);
        if (!m_presentation.IsNull()) {
            m_graphics.context()->Erase(m_presentation, Standard_False);
        }
        // Live sketch edits regenerate the support body too. Redisplay it
        // without discarding the user's current sketch zoom and pan.
        // Redisplay and selection preserve the camera.
    } catch (const Standard_Failure& failure) {
        if (!candidate.IsNull()) m_graphics.context()->Remove(candidate, Standard_False);
        if (!m_presentation.IsNull()) {
            m_graphics.context()->Display(m_presentation, Standard_False);
        }
        m_error = QString::fromUtf8(failure.GetMessageString());
        update();
        return false;
    }
    if (!m_presentation.IsNull()) {
        m_graphics.context()->Remove(m_presentation, Standard_False);
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
    m_planesVisible = visible;
    if (!visible) { m_hoveredPlane.reset(); }
    updateReferencePlanes();
    // Reference visibility preserves the camera.
}

void CadViewport::setSelectedReferencePlane(std::optional<modeling::SketchPlane> plane)
{
    if(plane)setSelectedSolidFace(0);
    m_selectedPlane = plane;
    updatePlaneStyles();
}

void CadViewport::setSelectedSolidFace(int index){
    if(index==m_selectedSolidFace)return;
    m_selectedSolidFace=0;if(!isReady())return;
    m_graphics.context()->ClearSelected(Standard_False);
    if(index>0 && !m_presentation.IsNull()){
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(m_shape,TopAbs_FACE,faces);
        if(index<=faces.Extent()){
            Handle(SelectMgr_IndexedMapOfOwner) owners;m_graphics.context()->EntityOwners(owners,m_presentation,AIS_Shape::SelectionMode(TopAbs_FACE));
            if(!owners.IsNull())for(int i=1;i<=owners->Extent();++i){
                const auto owner=Handle(StdSelect_BRepOwner)::DownCast(owners->FindKey(i));
                if(!owner.IsNull() && owner->HasShape() && owner->Shape().IsSame(faces(index))){m_graphics.context()->SetSelected(owner,Standard_False);m_selectedSolidFace=index;break;}
            }
        }
    }
    refreshNativeSelection();update();
}

void CadViewport::updateReferencePlanes()
{
    updateGlobalAxes();
    if (!isReady()) { return; }
    for (auto& plane : m_referencePlanes) {
        if (!plane.IsNull()) { m_graphics.context()->Remove(plane, Standard_False); plane.Nullify(); }
    }
    for (auto& label : m_planeLabels) {
        if (!label.IsNull()) { m_graphics.context()->Remove(label, Standard_False); label.Nullify(); }
    }
    for(auto& lines:m_planeCenterlines) {if(!lines.IsNull())m_graphics.context()->Remove(lines,Standard_False);lines.Nullify();}
    if (m_planesVisible) {
        m_planeHalfExtent = 50.0; // Model-space default for an empty part, not screen pixels.
        if (!m_shape.IsNull()) {
            Bnd_Box bounds;
            BRepBndLib::Add(m_shape, bounds);
            if (!bounds.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                bounds.Get(x0, y0, z0, x1, y1, z1);
                m_planeHalfExtent=std::clamp(std::hypot(std::hypot(x1-x0,y1-y0),z1-z0)*0.65,25.0,500.0);
            }
        }
        for (int i = 0; i < 3; ++i) {
            const auto type = static_cast<modeling::SketchPlane>(i);
            auto& plane = m_referencePlanes[std::size_t(i)];
            plane = new AIS_Shape(modeling::referencePlaneFace(type, m_planeHalfExtent));
            styleReferencePlane(plane,false,false,i,devicePixelRatioF());
            m_graphics.context()->Display(plane, AIS_Shaded, -1, Standard_False);
            auto& label = m_planeLabels[std::size_t(i)];
            label = new AIS_TextLabel;
            label->SetText(TCollection_ExtendedString(modeling::referencePlaneName(type)));
            const modeling::Point2 corner = i == 1 ? modeling::Point2{-m_planeHalfExtent, m_planeHalfExtent}
                                                   : modeling::Point2{m_planeHalfExtent, m_planeHalfExtent};
            label->SetPosition(modeling::sketchPoint(type, corner));
            const auto frame=modeling::sketchAxes(type);
            label->SetOrientation3D(gp_Ax2(modeling::sketchPoint(type,corner),frame.Direction(),frame.XDirection()));
            label->SetFlipping(Standard_True);
            label->SetHJustification(Graphic3d_HTA_RIGHT);
            label->SetVJustification(Graphic3d_VTA_TOP);
            label->SetFontAspect(Font_FA_Italic);
            label->SetHeight(15.0 * devicePixelRatioF());
            BRep_Builder builder;TopoDS_Compound lines;builder.MakeCompound(lines);
            for(const bool horizontal:{false,true}) {
                const auto a=horizontal?modeling::Point2{-m_planeHalfExtent,0}:modeling::Point2{0,-m_planeHalfExtent};
                const auto b=horizontal?modeling::Point2{m_planeHalfExtent,0}:modeling::Point2{0,m_planeHalfExtent};
                builder.Add(lines,BRepBuilderAPI_MakeEdge(modeling::sketchPoint(type,a),modeling::sketchPoint(type,b)).Shape());
            }
            m_planeCenterlines[i]=new AIS_Shape(lines);
            m_planeCenterlines[i]->Attributes()->SetWireAspect(new Prs3d_LineAspect(viewportColor(ThemeColor::Construction),Aspect_TOL_DASH,devicePixelRatioF()));
            m_planeCenterlines[i]->SetInfiniteState(Standard_True);
            m_graphics.context()->Display(m_planeCenterlines[i],AIS_WireFrame,-1,Standard_False);
            label->SetInfiniteState(Standard_True);
            m_graphics.context()->Display(label, 0, -1, Standard_False); // Labels are never pick targets.
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
        const bool selected = (m_selectedPlane && *m_selectedPlane == type) || std::any_of(m_selection.items().begin(),m_selection.items().end(),[&](const auto& r){return r.kind==modeling::SelectionKind::OriginPlane&&r.index==i;});
        const bool hovered = m_hoveredPlane && *m_hoveredPlane == type;
        styleReferencePlane(plane,selected,hovered,i,devicePixelRatioF());
        m_graphics.context()->Redisplay(plane, Standard_False);
        if (!m_planeLabels[std::size_t(i)].IsNull()) {
            m_planeLabels[std::size_t(i)]->SetColor(viewportColor(ThemeColor::Reference));
            m_graphics.context()->Redisplay(m_planeLabels[std::size_t(i)], Standard_False);
        }
    }
    update();
}

int CadViewport::faceAt(const QPointF& position)
{
    if(!isReady() || m_presentation.IsNull())return 0;
    for(const auto& candidate:selectionCandidates(position))if(candidate.value.reference.kind==modeling::SelectionKind::Face)return candidate.value.reference.index;
    return 0;
}

std::optional<modeling::ReferencePlaneHit> CadViewport::planeAt(const QPointF& position)
{
    if (!isReady() || !m_planesVisible) { return {}; }
    synchronizeSurface();
    const auto pixel = nativePosition(position);
    double x, y, z, dx, dy, dz;
    m_graphics.view()->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Pnt projected(x, y, z);
    const gp_Dir direction(dx, dy, dz);
    // ConvertWithProj can put the origin on the target plane. Move it onto the
    // eye plane so depth comparisons are forward and correct for orthographic
    // and perspective cameras alike, even after orbiting and panning.
    const double depth = gp_Vec(m_graphics.view()->Camera()->Eye(), projected).Dot(gp_Vec(direction));
    const auto origin = projected.Translated(-gp_Vec(direction) * depth);
    return modeling::pickReferencePlane(gp_Lin(origin, direction), m_planeHalfExtent);
}

void CadViewport::updateGlobalAxes()
{
    if(!isReady()) return;
    for(auto& axis:m_globalAxes) {if(!axis.IsNull()) m_graphics.context()->Remove(axis,Standard_False);axis.Nullify();}
    if(!m_globalOrigin.IsNull()){m_graphics.context()->Remove(m_globalOrigin,Standard_False);m_globalOrigin.Nullify();}
    if(m_sketchEditing) return;
    m_globalOrigin=new AIS_Point(new Geom_CartesianPoint(gp_Pnt(0,0,0)));
    m_globalOrigin->Attributes()->SetPointAspect(filledPoint(Quantity_Color(Quantity_NOC_RED),4.0*devicePixelRatioF()));
    m_globalOrigin->SetInfiniteState(Standard_True);m_graphics.context()->Display(m_globalOrigin,0,-1,Standard_False);
    const double length=std::max(5.0,m_planeHalfExtent*0.25);
    for(int i=0;i<3;++i) {
        const gp_Vec direction(i==0?1:0,i==1?1:0,i==2?1:0);
        const Quantity_Color color(i==0?Quantity_NOC_RED:i==1?Quantity_NOC_GREEN:Quantity_NOC_BLUE);
        m_globalAxes[i]=datumArrow(gp_Pnt(0,0,0),gp_Dir(direction),length,color);
        m_graphics.context()->Display(m_globalAxes[i],0,-1,Standard_False);
    }
}

void CadViewport::updateSketchAxes()
{
    if (!isReady()) { return; }
    updateGlobalAxes();
    for (auto& axis : m_sketchAxes) {
        if (!axis.IsNull()) { m_graphics.context()->Remove(axis, Standard_False); axis.Nullify(); }
    }
    if (!m_sketchOrigin.IsNull()) { m_graphics.context()->Remove(m_sketchOrigin, Standard_False); m_sketchOrigin.Nullify(); }
    if (m_sketchEditing && m_sketch) {
        const auto axes = modeling::sketchAxes(*m_sketch);
        const double span = std::max(m_graphics.view()->Camera()->Scale(), 1.0)*0.04;
        for (int i = 0; i < 2; ++i) {
            const gp_Vec direction(i == 0 ? axes.XDirection() : axes.YDirection());
            auto& axis=m_sketchAxes[std::size_t(i)];
            axis=datumArrow(axes.Location(),gp_Dir(direction),span,Quantity_Color(Quantity_NOC_RED));
            axis->SetZLayer(Graphic3d_ZLayerId_Topmost);
            m_graphics.context()->Display(axis,0,-1,Standard_False);
        }
        m_sketchOrigin = new AIS_Point(new Geom_CartesianPoint(axes.Location()));
        m_sketchOrigin->SetZLayer(Graphic3d_ZLayerId_Topmost);
        m_sketchOrigin->Attributes()->SetPointAspect(filledPoint(Quantity_Color(Quantity_NOC_RED3),4.0*devicePixelRatioF()));
        m_sketchOrigin->SetInfiniteState(Standard_True);
        m_graphics.context()->Display(m_sketchOrigin,0,-1,Standard_False);
    }
    update();
}
void CadViewport::beginSketch(const modeling::Sketch& sketch)
{
    clearSelection();
    setSelectedContourFaces({});
    setSelectedSolidFace(0);
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_selectingPlane = false;
    showReferencePlanes(false);
    clearPreview();
    m_regionSelecting = false;
    m_sketchEditing = true;
    if(isReady())for(auto& datum:m_datumPresentations)m_graphics.context()->Erase(datum,Standard_False);
    m_shiftSelectionChanged=false;
    clearArcOrSlot();
    m_tools.clearDrawing();m_tools.resetConstruction();m_drawingPreview.reset();
    m_firstPoint.reset();
    m_firstReference.reset();
    m_hoveredPoint.reset();
    m_hoveredEntity.clear();m_hoveredDimension.clear();
    m_selectedPoint.reset();
    m_selectedRelation.clear();m_hoveredRelation.clear();
    m_selectedEntity.clear();m_selectedEntities.clear();m_selectedDimensions.clear();m_deleteSequence.invalidate();
    setSketch(sketch);
    orientSketch();
    updateSketchAxes();
    setFocus(Qt::OtherFocusReason);
}

void CadViewport::endSketch()
{
    cancelSketchMultiSelection();
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_sketchEditing = false;
    setSketchModifierPicking(false);
    setSketchBoundaryReferences({});
    setSketchModifierPreview(nullptr);
    setDatums(m_datums);
    updateSketchAxes();
    clearArcOrSlot();
    m_tools.clearDrawing();
    m_firstPoint.reset();
    m_firstReference.reset();
    m_hoveredPoint.reset();
    m_hoveredEntity.clear();m_hoveredDimension.clear();
    m_selectedPoint.reset();
    m_selectedRelation.clear();m_hoveredRelation.clear();
    m_selectedEntity.clear();m_selectedEntities.clear();m_selectedDimensions.clear();m_deleteSequence.invalidate();
    m_sketch.reset();
    updateSketchPresentation();
    unsetCursor();
    update();
}

void CadViewport::beginContourSelection(const modeling::Sketch& sketch)
{
    clearSelection();
    setSelectedContourFaces({});
    cancelDimensionDrag();cancelPointDrag();resetSketchInference();
    clearArcOrSlot();
    m_tools.clearDrawing();m_firstPoint.reset();m_firstReference.reset();
    showReferencePlanes(false);
    m_sketchEditing = false;
    m_regionSelecting = true;
    updateSketchAxes();
    m_firstPoint.reset();
    setSketch(sketch);
    // Contour selection and feature editing preserve the current camera.
}

void CadViewport::endContourSelection()
{
    setSelectedContourFaces({});
    m_camera.stop();m_regionClickCandidate=false;
    m_regionSelecting = false;
    m_sketch.reset();
    updateSketchPresentation();
}

void CadViewport::previewShape(const TopoDS_Shape& shape,const modeling::MaterialChanges& changes)
{
    if (!isReady() || shape.IsNull()) { return; }
    clearPreview();
    m_previewPresentation = makeSolidPresentation(shape);
    if (!m_presentation.IsNull()) { m_graphics.context()->Erase(m_presentation, Standard_False); }
    m_graphics.context()->Display(m_previewPresentation,AIS_Shaded,-1,Standard_False);
    const auto displayChange=[&](const TopoDS_Shape& material,const Quantity_Color& color){
        if(material.IsNull())return;
        auto presentation=makeSolidPresentation(material);presentation->SetColor(color);presentation->SetTransparency(0.25);
        presentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
        m_graphics.context()->Display(presentation,AIS_Shaded,-1,Standard_False);m_materialPreviewPresentations.push_back(presentation);
    };
    displayChange(changes.added,Quantity_Color(1.0,0.74,0.20,Quantity_TOC_RGB));
    displayChange(changes.removed,removedMaterialYellow());
    update();
}

void CadViewport::setSelectedContourFaces(const std::vector<TopoDS_Face>& faces){
    if(!isReady())return;
    for(auto& face:m_selectedContourPresentations)m_graphics.context()->Remove(face,Standard_False);
    m_selectedContourPresentations.clear();
    for(const auto& face:faces){
        Handle(AIS_Shape) presentation=new AIS_Shape(face);
        presentation->SetColor(Quantity_Color(0.25,0.65,1.0,Quantity_TOC_RGB));presentation->SetTransparency(0.35);
        presentation->SetZLayer(Graphic3d_ZLayerId_Topmost);presentation->Attributes()->SetFaceBoundaryDraw(Standard_True);
        m_graphics.context()->Display(presentation,AIS_Shaded,-1,Standard_False);m_selectedContourPresentations.push_back(presentation);
    }
    update();
}

QPointF CadViewport::projectWorldPoint(const gp_Pnt& point) const
{
    int x, y;
    m_graphics.view()->Convert(point.X(), point.Y(), point.Z(), x, y);
    return {x / devicePixelRatioF(), y / devicePixelRatioF()};
}

void CadViewport::setExtrusionDepthGrips(const std::vector<gp_Ax1>& grips)
{
    if (!isReady()) return;
    for (const auto& item : m_extrusionDepthGrips) m_graphics.context()->Remove(item, false);
    m_extrusionDepthGrips.clear();
    const double length = m_graphics.view()->Camera()->Scale() * 0.04;
    for (const auto& grip : grips) {
        const auto origin = grip.Location().Translated(-gp_Vec(grip.Direction()) * length);
        auto arrow = datumArrow(origin, grip.Direction(), length, selectionBlue());
        arrow->SetZLayer(Graphic3d_ZLayerId_Topmost);
        m_graphics.context()->Display(arrow, 1, -1, false);
        m_extrusionDepthGrips.push_back(arrow);
    }
    update();
}

void CadViewport::clearPreview()
{
    if (!isReady()) { return; }
    for(auto& presentation:m_materialPreviewPresentations)m_graphics.context()->Remove(presentation,Standard_False);
    m_materialPreviewPresentations.clear();
    if (!m_previewPresentation.IsNull()) {
        m_graphics.context()->Remove(m_previewPresentation, Standard_False);
        m_previewPresentation.Nullify();
        if (!m_presentation.IsNull()) { m_graphics.context()->Display(m_presentation, Standard_False); }
        update();
    }
}

viewport::SketchPicker CadViewport::sketchPicker()const{
    return {*m_sketch,m_selectionPolicy,m_sketchFeatureId,devicePixelRatioF(),m_pickingTolerance,m_graphics.view()->Camera()->Scale()/std::max(1,height()),[this](modeling::Point2 p){return screenPoint(p);}};
}
QPoint CadViewport::dimensionScreenPoint(modeling::Point2 point) const {return screenPoint(point).toPoint();}
QString CadViewport::dimensionText(const modeling::SketchDimension& d) const {return viewport::DimensionPresenter::text(*m_sketch,d);}
void CadViewport::setPickingTolerance(double pixels) {
    if(std::isfinite(pixels) && pixels>0)m_pickingTolerance=pixels;
}
std::vector<modeling::SelectionCandidate> CadViewport::dimensionCandidates(const QPointF& position) const {
    if(!isReady()||!m_sketch)return {};
    return sketchPicker().dimensionCandidates(position);
}
std::optional<std::string> CadViewport::dimensionAt(const QPointF& position) const {
    const auto candidates=m_selectionPolicy.order(dimensionCandidates(position));
    return candidates.empty()?std::optional<std::string>{}:candidates.front().reference.entityId;
}
void CadViewport::updateSketchHover(const QPointF& position) {
    const auto symbol=relationAt(position);const auto previous=m_hoveredRelation;m_hoveredRelation=symbol.value_or("");
    if(symbol){
        m_hoveredPoint.reset();m_hoveredEntity.clear();m_hoveredDimension.clear();
        if(previous!=m_hoveredRelation){const auto r=std::find_if(m_sketch->relations.begin(),m_sketch->relations.end(),[&](const auto& r){return r.id==*symbol;});if(r!=m_sketch->relations.end())QToolTip::showText(mapToGlobal(position.toPoint()),modeling::relationName(r->kind),this);}
        updatePointPresentation();updateRelationSymbols();update();return;
    }
    if(!previous.empty())QToolTip::hideText();
    const auto oldPoint=m_hoveredPoint;
    const bool midpointTool=m_tools.drawing()==modeling::EntityKind::Line || m_tools.drawing()==modeling::EntityKind::Point || drawingArcOrSlot();
    const auto hit=m_tools.drawing()?pointAt(position,midpointTool):selectionPointAt(position);
    m_hoveredPoint=hit?std::optional<modeling::SketchPointRef>(hit->reference):std::nullopt;
    if(m_hoveredPoint && m_hoveredPoint->role==modeling::PointRole::Midpoint){if(!oldPoint || oldPoint->entityId!=m_hoveredPoint->entityId || oldPoint->role!=m_hoveredPoint->role)QToolTip::showText(mapToGlobal(position.toPoint()),"Midpoint",this);}
    else if(oldPoint && oldPoint->role==modeling::PointRole::Midpoint)QToolTip::hideText();
    m_hoveredDimension=(!hit && !m_tools.drawing())?dimensionAt(position).value_or(""):"";
    m_hoveredEntity=(!hit && m_hoveredDimension.empty())?entityAt(position).value_or(""):"";
    updatePointPresentation();updateConstraintPresentation();update();
}
void CadViewport::updateDimensionDrag(const QPointF& position) {
    if(!m_dimensionDrag || !m_sketch)return;
    auto& drag=*m_dimensionDrag;
    if(!drag.moved && (position-drag.press).manhattanLength()<QApplication::startDragDistance())return;
    const auto raw=sketchPosition(position);if(!raw)return;
    auto d=std::find_if(m_sketch->dimensions.begin(),m_sketch->dimensions.end(),[&](const auto& v){return v.id==drag.id;});
    if(d==m_sketch->dimensions.end())return;
    drag.moved=true;modeling::placeDimensionAnnotation(*m_sketch,*d,{raw->x+drag.offset.x,raw->y+drag.offset.y});
    updateConstraintPresentation();update();
}
void CadViewport::cancelDimensionDrag() {
    if(!m_dimensionDrag)return;
    m_sketch=m_dimensionDrag->original;m_dimensionDrag.reset();
    if(QWidget::mouseGrabber()==this)releaseMouse();
    updateConstraintPresentation();update();
}
void CadViewport::setSmartDimensionPicking(bool enabled) {
    selectSketchTool();m_tools.dimension(enabled);emit sketchToolChanged();
}
void CadViewport::setDimensionPreview(const std::optional<modeling::SketchDimension>& dimension){m_dimensionPreview=dimension;updateConstraintPresentation();update();}
void CadViewport::setDimensionSelection(const std::vector<std::string>& entities,const std::vector<modeling::SketchPointRef>& points){m_dimensionEntities=entities;m_dimensionPoints=points;updatePointPresentation();update();}
void CadViewport::setConstraintErrors(const std::vector<std::string>& ids){m_constraintErrors=ids;updateConstraintPresentation();update();}
void CadViewport::setConstraintPicking(bool enabled) {selectSketchTool();m_tools.relations(enabled);emit sketchToolChanged();}
void CadViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    if(m_sketchLineEditPicking && event->button()==Qt::LeftButton){event->accept();return;}
    if(m_sketchEditing && m_sketch && isReady() && event->button()==Qt::LeftButton) {
        cancelPointDrag();
        cancelDimensionDrag();
        if(const auto id=dimensionAt(event->position())) {
            emit dimensionEditRequested(QString::fromStdString(*id));event->accept();return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}
void CadViewport::updateConstraintPresentation(bool layoutRelations){
    if(!isReady())return;
    m_dimensions.clear(m_graphics.context());m_cameraAnnotationsDirty=false;
    if(layoutRelations)updateRelationSymbols();
    if(!m_sketch)return;
    const viewport::DimensionVisualState state{m_dimensionPreview,m_selectedEntity,m_hoveredDimension,m_selectedDimensions,m_constraintErrors,
        viewportColor(ThemeColor::Defined),viewportColor(ThemeColor::Underdefined),viewportColor(ThemeColor::Error),selectionBlue(),hoverOrange()};
    m_dimensions.rebuild(m_graphics.context(),*m_sketch,m_graphics.view()->Camera()->Scale()/std::max(1,height()),devicePixelRatioF(),state);
}

void CadViewport::setSketchTool(modeling::EntityKind kind,bool construction)
{
    cancelSketchMultiSelection();
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_selectedRelation.clear();
    clearArcOrSlot();
    m_tools.draw(kind,construction);m_drawingPreview.reset();
    m_firstPoint.reset();
    m_firstReference.reset();
    m_hoveredPoint.reset();QToolTip::hideText();
    if (m_sketchEditing) { setCursor(Qt::CrossCursor); }
    emit sketchToolChanged();
    updateSketchPresentation();
}

void CadViewport::placeSketchPoint(const QPointF& position) {
    if(!m_sketch)return;
    std::optional<modeling::SketchPointRef> reference;auto point=drawingPoint(position,reference);auto candidate=*m_sketch;
    modeling::SketchEntity entity{modeling::EntityKind::Point,point,{},1,QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()};
    const auto relationId=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    try {
    if(reference){const bool midpoint=reference->role==modeling::PointRole::Midpoint;candidate.relations.push_back({relationId,midpoint?modeling::RelationKind::Midpoint:modeling::RelationKind::Coincident,{entity.id,modeling::PointRole::First},midpoint?modeling::SketchPointRef{reference->entityId,modeling::PointRole::First}:*reference});}
    else if(m_boundarySnap) attachBoundarySnap(candidate,{entity.id,modeling::PointRole::First},*m_boundarySnap);
    else if(const auto id=entityAt(position)) {
        const auto e=std::find_if(candidate.entities.begin(),candidate.entities.end(),[&](const auto& v){return v.id==*id;});
        if(e!=candidate.entities.end() && e->kind==modeling::EntityKind::Line){
            const double dx=e->second.x-e->first.x,dy=e->second.y-e->first.y,t=std::clamp(((point.x-e->first.x)*dx+(point.y-e->first.y)*dy)/(dx*dx+dy*dy),0.0,1.0);
            entity.first={e->first.x+t*dx,e->first.y+t*dy};candidate.relations.push_back({relationId,modeling::RelationKind::PointOnLine,{entity.id,modeling::PointRole::First},{e->id,modeling::PointRole::First}});
        }else if(e!=candidate.entities.end() && e->kind==modeling::EntityKind::Circle){
            const double angle=std::atan2(point.y-e->first.y,point.x-e->first.x);entity.first={e->first.x+e->radius*std::cos(angle),e->first.y+e->radius*std::sin(angle)};
            candidate.relations.push_back({relationId,modeling::RelationKind::PointOnCircle,{entity.id,modeling::PointRole::First},{e->id,modeling::PointRole::Center}});
        }
    }
    candidate.entities.push_back(entity);
    modeling::validateSketchRelations(candidate);(void)modeling::sketchShape(candidate);m_sketch=candidate;emit sketchChanged(candidate);emit sketchEntityCreated(entity);updateSketchPresentation();}
    catch(const std::exception& e){emit sketchInteractionError(QString::fromUtf8(e.what()));}
}

void CadViewport::selectSketchTool()
{
    cancelSketchMultiSelection();
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    clearArcOrSlot();
    m_tools.clearDrawing(); m_firstPoint.reset(); m_firstReference.reset();m_tools.resetConstruction();m_drawingPreview.reset();
    m_hoveredPoint.reset();QToolTip::hideText();
    m_tools.select();
    unsetCursor(); updateSketchPresentation();emit sketchToolChanged();
}

void CadViewport::selectSketchEntity(const QString& id)
{
    m_selectedRelation.clear();
    m_selectedEntity=id.toStdString();m_selectedEntities.clear();m_selectedDimensions.clear();m_selectedPoint.reset();
    if(!m_selectedEntity.empty())m_selectedEntities.push_back(m_selectedEntity);
    m_deleteSequence.invalidate();updatePointPresentation();updateConstraintPresentation();emit sketchSelectionChanged();
}
void CadViewport::selectDimension(const std::string& id,bool toggle)
{
    m_selectedRelation.clear();
    if(!toggle){m_selectedEntities.clear();m_selectedDimensions.clear();}
    const auto found=std::find(m_selectedDimensions.begin(),m_selectedDimensions.end(),id);
    if(toggle && found!=m_selectedDimensions.end())m_selectedDimensions.erase(found);else m_selectedDimensions.push_back(id);
    m_selectedEntity=m_selectedEntities.empty()?"":m_selectedEntities.back();m_selectedPoint.reset();
    emit sketchEntitySelected(QString::fromStdString(m_selectedEntity));
    updatePointPresentation();updateConstraintPresentation();emit sketchSelectionChanged();
}

void CadViewport::setSketch(const modeling::Sketch& sketch)
{
    cancelDimensionDrag();cancelPointDrag();
    m_sketch = sketch;
    modeling::attachDimensionAnnotations(*m_sketch);
    if(std::none_of(sketch.relations.begin(),sketch.relations.end(),[&](const auto& r){return r.id==m_selectedRelation;}))m_selectedRelation.clear();
    // Numeric edits may move or remove the point where a continuous line resumes.
    if(m_firstReference) {
        try { m_firstPoint = modeling::sketchPointValue(sketch,*m_firstReference); }
        catch(const std::exception&) { m_firstPoint.reset(); m_firstReference.reset(); }
    }
    const auto validReference=[&](std::optional<modeling::SketchPointRef>& reference) {
        if(!reference) return;
        try { (void)modeling::sketchPointValue(sketch,*reference); }
        catch(const std::exception&) { reference.reset(); }
    };
    for (auto& pick : m_curvedPicks)
    {
        validReference(pick.reference);
        if (pick.reference)
            pick.position = modeling::sketchPointValue(sketch, *pick.reference);
    }
    validReference(m_hoveredPoint); validReference(m_selectedPoint);
    validReference(m_inference.target);validReference(m_inference.alignX);validReference(m_inference.alignY);
    if(std::none_of(sketch.entities.begin(),sketch.entities.end(),[&](const auto& entity){return entity.id==m_selectedEntity;}))
        m_selectedEntity.clear();
    m_selectedEntities.erase(std::remove_if(m_selectedEntities.begin(),m_selectedEntities.end(),[&](const auto& id){
        return std::none_of(sketch.entities.begin(),sketch.entities.end(),[&](const auto& e){return e.id==id;});
    }),m_selectedEntities.end());
    m_selectedDimensions.erase(std::remove_if(m_selectedDimensions.begin(),m_selectedDimensions.end(),[&](const auto& id){
        return std::none_of(sketch.dimensions.begin(),sketch.dimensions.end(),[&](const auto& d){return d.id==id;});
    }),m_selectedDimensions.end());
    updateSketchPresentation();emit sketchSelectionChanged();
}

void CadViewport::orientSketch()
{
    if (!isReady() || !m_sketch) { return; }
    const auto axes = modeling::sketchAxes(*m_sketch);
    const auto normal = axes.Direction();
    const auto up = axes.YDirection();
    m_graphics.view()->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    // Changing the target also changes OCCT's view direction. Center first,
    // then set the final normal/up frame, including when a body is off-origin.
    m_graphics.view()->SetAt(axes.Location().X(),axes.Location().Y(),axes.Location().Z());
    m_graphics.view()->SetProj(normal.X(), normal.Y(), normal.Z());
    m_graphics.view()->SetUp(up.X(), up.Y(), up.Z());
    double span = 75.0;
    for (const auto& entry : modeling::sketchPoints(*m_sketch)) {
        span = std::max({span, std::abs(entry.second.x) * 1.25, std::abs(entry.second.y) * 1.25});
    }
    for (const auto& entity : m_sketch->entities) {
        if (entity.kind == modeling::EntityKind::Circle)
            span = std::max({span, (std::abs(entity.first.x)+entity.radius)*1.25,
                                  (std::abs(entity.first.y)+entity.radius)*1.25});
    }
    m_graphics.view()->Camera()->SetScale(span * 2.0);
    updateConstraintPresentation();
    update();
}

void CadViewport::updateSketchPresentation(const std::optional<modeling::SketchEntity>& preview)
{
    if (!isReady()) { return; }
    for(auto& batch:m_sketchBatches)m_graphics.context()->Remove(batch,Standard_False);m_sketchBatches.clear();m_sketchPresentation.Nullify();m_constructionPresentation.Nullify();
    m_definition=m_sketch?modeling::analyzeSketchDefinition(*m_sketch):modeling::SketchDefinition{};
    m_sketchFullyDefined=m_definition.valid && m_definition.degreesOfFreedom==0;
    if (m_sketch) {
        try {
            auto visible = *m_sketch;
            visible.entities.insert(visible.entities.end(), m_curvedPreview.begin(), m_curvedPreview.end());
            if(preview){
                if(preview->kind==modeling::EntityKind::Polygon && preview->radius>1e-6)
                    visible=modeling::addRegularPolygon(visible,"preview-polygon",{preview->first,preview->radius,std::atan2(preview->second.y-preview->first.y,preview->second.x-preview->first.x),6});
                else if(preview->kind!=modeling::EntityKind::Polygon)visible.entities.push_back(*preview);
            }
            for(const bool construction:{false,true})for(const bool defined:{false,true}){
                auto group=visible;group.relations.clear();group.dimensions.clear();group.polygons.clear();
                group.projections.clear();group.replications.clear();group.offsets.clear();
                group.entities.erase(std::remove_if(group.entities.begin(),group.entities.end(),[&](const auto& e){const bool fixed=std::find(m_definition.definedCurves.begin(),m_definition.definedCurves.end(),e.id)!=m_definition.definedCurves.end();return e.kind==modeling::EntityKind::Point || e.construction!=construction || fixed!=defined;}),group.entities.end());
                if(group.entities.empty())continue;
                Handle(AIS_Shape) batch=new AIS_Shape(modeling::sketchShape(group));
                const Quantity_Color color=defined?viewportColor(ThemeColor::Defined):construction?viewportColor(ThemeColor::Construction):viewportColor(ThemeColor::Underdefined);
                batch->SetColor(color);batch->SetWidth((construction?1.5:2.0)*devicePixelRatioF());
                if(construction){const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(color,Aspect_TOL_DOTDASH,1.5*devicePixelRatioF());batch->Attributes()->SetWireAspect(aspect);batch->Attributes()->SetLineAspect(aspect);}
                batch->Attributes()->SetDeviationCoefficient(1e-5);batch->Attributes()->SetDeviationAngle(0.015);batch->SetZLayer(Graphic3d_ZLayerId_Topmost);
                m_graphics.context()->Display(batch,AIS_WireFrame,-1,Standard_False);m_graphics.context()->SetDisplayPriority(batch,Graphic3d_DisplayPriority_Topmost);m_sketchBatches.push_back(batch);
                if(construction && m_constructionPresentation.IsNull())m_constructionPresentation=batch;
                if(!construction && m_sketchPresentation.IsNull())m_sketchPresentation=batch;
            }
        } catch (const std::exception&) {
            // A zero-size pointer preview is transient, never stored as model data.
        } catch (const Standard_Failure&) {
        }
    }
    updatePointPresentation();
    updateConstraintPresentation();
    update();
}

namespace {
bool sameReference(const modeling::SketchPointRef& a, const modeling::SketchPointRef& b)
{ return a.entityId == b.entityId && a.role == b.role; }
bool samePoint(modeling::Point2 a, modeling::Point2 b)
{ return std::hypot(a.x-b.x, a.y-b.y) < 1e-7; }
std::string sketchId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
}

QPointF CadViewport::screenPoint(modeling::Point2 point) const
{
    const auto world = modeling::sketchPoint(*m_sketch, point);
    int x, y; m_graphics.view()->Convert(world.X(), world.Y(), world.Z(), x, y);
    return {x / devicePixelRatioF(), y / devicePixelRatioF()};
}

std::optional<CadViewport::SnapHit> CadViewport::pointAt(const QPointF& position,bool includeMidpoints) const
{
    if(!isReady()||!m_sketch)return {};
    return sketchPicker().pointAt(position,includeMidpoints);
}

std::vector<modeling::SelectionCandidate> CadViewport::sketchEntityCandidates(const QPointF& position) const
{
    if(!isReady()||!m_sketch)return {};
    return sketchPicker().sketchEntityCandidates(position);
}

std::optional<std::string> CadViewport::entityAt(const QPointF& position) const {
    const auto candidates=m_selectionPolicy.order(sketchEntityCandidates(position));
    return candidates.empty()?std::optional<std::string>{}:candidates.front().reference.entityId;
}

std::optional<CadViewport::SnapHit> CadViewport::selectionPointAt(const QPointF& position) const
{
    if(!isReady()||!m_sketch)return {};
    return sketchPicker().selectionPointAt(position);
}

modeling::Point2 CadViewport::drawingPoint(const QPointF& position, std::optional<modeling::SketchPointRef>& reference)
{
    reference.reset();
    m_boundarySnap.reset();
    const auto previous=m_inference;
    m_inference={};
    const bool midpointTool=m_tools.drawing()==modeling::EntityKind::Line || m_tools.drawing()==modeling::EntityKind::Point || drawingArcOrSlot();
    auto hit=pointAt(position,midpointTool);
    if(previous.target && !(hit && hit->reference.role==modeling::PointRole::Origin)) {
        const auto value=modeling::sketchPointValue(*m_sketch,*previous.target);
        if(QLineF(position,screenPoint(value)).length()<=12.0) hit=SnapHit{*previous.target,value};
    }
    if(hit && !(m_firstPoint && m_tools.drawing()==modeling::EntityKind::Circle)) {
        reference=hit->reference; m_inference.target=reference; m_inference.symbolPosition=hit->point;
        m_hoveredPoint=reference; return hit->point;
    }
    auto point=sketchPosition(position).value_or(modeling::Point2{});
    if (!(m_firstPoint && m_tools.drawing() == modeling::EntityKind::Circle))
        if (auto snap = boundarySnapAt(position, point))
        {
            m_boundarySnap = snap;
            m_inference.symbolPosition = snap->point;
            return snap->point;
        }
    if(m_firstPoint && m_tools.drawing()==modeling::EntityKind::Line) {
        const auto firstPixel=screenPoint(*m_firstPoint);
        const auto horizontal=screenPoint({point.x,m_firstPoint->y});
        const auto vertical=screenPoint({m_firstPoint->x,point.y});
        const auto inference=modeling::inferLineDirection(*m_firstPoint,point,
            QLineF(position,horizontal).length(),QLineF(position,vertical).length(),QLineF(position,firstPixel).length(),previous.axis);
        point=inference.point;m_inference.axis=inference.relation;
        const double span=std::max(1.0,m_graphics.view()->Camera()->Scale())*std::max(1.0,m_graphics.view()->Camera()->Aspect())*2;
        const auto guide=[&](modeling::Point2 anchor,bool horizontalGuide) {
            if(horizontalGuide) m_inference.guides.push_back({{point.x-span,anchor.y},{point.x+span,anchor.y}});
            else m_inference.guides.push_back({{anchor.x,point.y-span},{anchor.x,point.y+span}});
        };
        if(m_inference.axis) guide(*m_firstPoint,*m_inference.axis==modeling::RelationKind::Horizontal);
        const auto align=[&](bool xCoordinate,const std::optional<modeling::SketchPointRef>& preferred) -> std::optional<modeling::SketchPointRef> {
            double best=5.0; std::optional<modeling::SketchPointRef> result;
            for(const auto& entry:modeling::sketchPoints(*m_sketch)) {
                if(samePoint(entry.second,*m_firstPoint)) continue;
                auto aligned=point;if(xCoordinate) aligned.x=entry.second.x;else aligned.y=entry.second.y;
                const double distance=QLineF(position,screenPoint(aligned)).length();
                if(preferred && sameReference(*preferred,entry.first) && distance<=9.0) return entry.first;
                if(distance<best){best=distance;result=entry.first;}
            }
            return result;
        };
        if(m_inference.axis!=modeling::RelationKind::Vertical) m_inference.alignX=align(true,previous.alignX);
        if(m_inference.axis!=modeling::RelationKind::Horizontal) m_inference.alignY=align(false,previous.alignY);
        if(m_inference.alignX) { const auto anchor=modeling::sketchPointValue(*m_sketch,*m_inference.alignX); point.x=anchor.x;guide(anchor,false); }
        if(m_inference.alignY) { const auto anchor=modeling::sketchPointValue(*m_sketch,*m_inference.alignY); point.y=anchor.y;guide(anchor,true); }
        m_inference.symbolPosition={(m_firstPoint->x+point.x)/2,(m_firstPoint->y+point.y)/2};
    }
    return point;
}

void CadViewport::updatePointPresentation()
{
    if (!isReady()) return;
    for(auto& point:m_pointPresentations) m_graphics.context()->Remove(point,Standard_False);
    m_pointPresentations.clear();
    if(!m_selectedEntityPresentation.IsNull()) {
        m_graphics.context()->Remove(m_selectedEntityPresentation,Standard_False); m_selectedEntityPresentation.Nullify();
    }
    if(!m_hoveredEntityPresentation.IsNull()) {
        m_graphics.context()->Remove(m_hoveredEntityPresentation,Standard_False);m_hoveredEntityPresentation.Nullify();
    }
    if(m_sketchEditing && m_sketch) {
        if(!m_hoveredEntity.empty()) {
            auto only=*m_sketch;only.entities.clear();only.relations.clear();only.dimensions.clear();only.polygons.clear();
            only.projections.clear();only.replications.clear();only.offsets.clear();
            for(const auto& e:m_sketch->entities)if(e.id==m_hoveredEntity)only.entities.push_back(e);
            if(!only.entities.empty()) {
                m_hoveredEntityPresentation=new AIS_Shape(modeling::sketchShape(only));
                m_hoveredEntityPresentation->SetColor(Quantity_NOC_ORANGE);
                m_hoveredEntityPresentation->SetWidth(3*devicePixelRatioF());
                if(only.entities.front().construction){
                    const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(Quantity_NOC_ORANGE,Aspect_TOL_DOTDASH,3*devicePixelRatioF());
                    m_hoveredEntityPresentation->Attributes()->SetWireAspect(aspect);m_hoveredEntityPresentation->Attributes()->SetLineAspect(aspect);
                }
                m_hoveredEntityPresentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
                m_graphics.context()->Display(m_hoveredEntityPresentation,AIS_WireFrame,-1,Standard_False);
            }
        }
        for(const auto& entry:modeling::sketchPoints(*m_sketch)) {
            const bool origin=entry.first.role==modeling::PointRole::Origin;
            const bool hovered=m_hoveredPoint && sameReference(*m_hoveredPoint,entry.first);
            const bool selected=(m_selectedPoint && sameReference(*m_selectedPoint,entry.first)) || std::any_of(m_dimensionPoints.begin(),m_dimensionPoints.end(),[&](const auto& p){return sameReference(p,entry.first);});
            const bool lineEndpoint=std::any_of(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& entity) {
                return entity.id==entry.first.entityId && entity.kind==modeling::EntityKind::Line;
            });
            const bool standalone=std::any_of(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==entry.first.entityId && e.kind==modeling::EntityKind::Point;});
            const bool circleCenter=entry.first.role==modeling::PointRole::Center;
            if(!origin && !hovered && !selected && !lineEndpoint && !standalone && !circleCenter && entry.first.entityId!=m_selectedEntity) continue;
            Handle(AIS_Point) marker=new AIS_Point(new Geom_CartesianPoint(modeling::sketchPoint(*m_sketch,entry.second)));
            const bool fixed=std::any_of(m_definition.fixedPoints.begin(),m_definition.fixedPoints.end(),[&](const auto& p){return sameReference(p,entry.first);});
            Quantity_Color color=hovered?Quantity_Color(Quantity_NOC_ORANGE):origin?Quantity_Color(Quantity_NOC_RED3):fixed?viewportColor(ThemeColor::Defined):ThemeManager::instance().isDark()?viewportColor(ThemeColor::Underdefined):Quantity_Color(Quantity_NOC_BLUE);
            if(selected && !origin)color=Quantity_Color(0.25,0.72,1.0,Quantity_TOC_RGB);
            marker->SetZLayer(Graphic3d_ZLayerId_Topmost);
            marker->Attributes()->SetPointAspect(filledPoint(color,(hovered ? 9.0 : selected ? 9.0 : origin ? 4.0 : 7.0)*devicePixelRatioF()));
            marker->SetInfiniteState(Standard_True);
            m_graphics.context()->Display(marker,0,-1,Standard_False);
            m_graphics.context()->SetDisplayPriority(marker,Graphic3d_DisplayPriority_Topmost);
            m_pointPresentations.push_back(marker);
        }
        if(m_hoveredPoint && m_hoveredPoint->role==modeling::PointRole::Midpoint && (m_pointDrag || m_tools.drawing()==modeling::EntityKind::Line || m_tools.drawing()==modeling::EntityKind::Point || drawingArcOrSlot())){
            Handle(AIS_Point) marker=new AIS_Point(new Geom_CartesianPoint(modeling::sketchPoint(*m_sketch,modeling::sketchPointValue(*m_sketch,*m_hoveredPoint))));
            marker->Attributes()->SetPointAspect(filledPoint(Quantity_NOC_ORANGE,9*devicePixelRatioF()));marker->SetZLayer(Graphic3d_ZLayerId_Topmost);marker->SetInfiniteState(Standard_True);
            m_graphics.context()->Display(marker,0,-1,Standard_False);m_graphics.context()->SetDisplayPriority(marker,Graphic3d_DisplayPriority_Topmost);m_pointPresentations.push_back(marker);
        }
        modeling::Sketch only;only.plane=m_sketch->plane;only.attachment=m_sketch->attachment;only.referencePlaneId=m_sketch->referencePlaneId;only.referencePlaneFrame=m_sketch->referencePlaneFrame;
        for(const auto& entity:m_sketch->entities)if(entity.id==m_selectedEntity || std::find(m_selectedEntities.begin(),m_selectedEntities.end(),entity.id)!=m_selectedEntities.end() || std::find(m_dimensionEntities.begin(),m_dimensionEntities.end(),entity.id)!=m_dimensionEntities.end())only.entities.push_back(entity);
        if(!only.entities.empty()) {
            m_selectedEntityPresentation=new AIS_Shape(modeling::sketchShape(only));
            m_selectedEntityPresentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
            m_selectedEntityPresentation->SetColor(Quantity_Color(0.25,0.72,1.0,Quantity_TOC_RGB)); m_selectedEntityPresentation->SetWidth(2.5 * devicePixelRatioF());
            if(std::all_of(only.entities.begin(),only.entities.end(),[](const auto& e){return e.construction;})){
                const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(Quantity_Color(0.25,0.72,1.0,Quantity_TOC_RGB),Aspect_TOL_DOTDASH,2.5*devicePixelRatioF());
                m_selectedEntityPresentation->Attributes()->SetWireAspect(aspect);m_selectedEntityPresentation->Attributes()->SetLineAspect(aspect);
            }
            m_selectedEntityPresentation->Attributes()->SetDeviationCoefficient(1e-5);
            m_selectedEntityPresentation->Attributes()->SetDeviationAngle(0.015);
            m_graphics.context()->Display(m_selectedEntityPresentation,AIS_WireFrame,-1,Standard_False);
        }
    }
    update();
}

void CadViewport::clearInteractionPresentation()
{
    if(isReady() && !m_interactionLabel.IsNull()) m_graphics.context()->Remove(m_interactionLabel,Standard_False);
    m_interactionLabel.Nullify();
    if(isReady()) for(auto& guide:m_inferenceGuides) m_graphics.context()->Remove(guide,Standard_False);
    m_inferenceGuides.clear();update();
}

void CadViewport::resetSketchInference()
{
    m_inference={}; m_boundarySnap.reset(); clearInteractionPresentation();
}

void CadViewport::updateInteractionPresentation()
{
    clearInteractionPresentation();
    if(!isReady() || !m_sketch) return;
    QString text;
    modeling::Point2 point=m_inference.symbolPosition;
    bool invalid=false;
    if(m_pointDrag && m_pointDrag->moved) {
        point=modeling::sketchPointValue(*m_sketch,m_pointDrag->reference);
        if(!m_pointDrag->error.isEmpty()) {text="Cannot move";invalid=true;}
        else if(m_pointDrag->target) {text=m_pointDrag->target->role==modeling::PointRole::Midpoint?"Midpoint":"Coincident";point=modeling::sketchPointValue(*m_sketch,*m_pointDrag->target);}
    } else if (m_boundarySnap && m_sketchEditing && m_tools.drawing()) {
        text = m_boundarySnap->role == modeling::PointRole::Midpoint ? "Model midpoint" :
               m_boundarySnap->role ? "Model point" : "Model edge";
    } else if(m_sketchEditing && m_tools.drawing() && m_firstPoint) {
        for(const auto& line:m_inference.guides) {
            Handle(AIS_Shape) guide=new AIS_Shape(BRepBuilderAPI_MakeEdge(modeling::sketchPoint(*m_sketch,line.first),modeling::sketchPoint(*m_sketch,line.second)).Shape());
            const Quantity_Color color(0.65,0.48,0.06,Quantity_TOC_RGB);
            const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(color,Aspect_TOL_DASH,devicePixelRatioF());
            guide->Attributes()->SetWireAspect(aspect);guide->Attributes()->SetLineAspect(aspect);
            guide->SetZLayer(Graphic3d_ZLayerId_Topmost);guide->SetInfiniteState(Standard_True);
            m_graphics.context()->Display(guide,AIS_WireFrame,-1,Standard_False);m_inferenceGuides.push_back(guide);
        }
        if(m_inference.target) text=m_inference.target->role==modeling::PointRole::Midpoint?"Midpoint":"Coincident";
        else if(m_inference.axis==modeling::RelationKind::Horizontal) text="H";
        else if(m_inference.axis==modeling::RelationKind::Vertical) text="V";
    }
    if(text.isEmpty()) return;
    const auto labelPoint=sketchPosition(screenPoint(point)+QPointF(12,-16)).value_or(point);
    m_interactionLabel=new AIS_TextLabel;
    m_interactionLabel->SetText(TCollection_ExtendedString(text.toUtf8().constData(),Standard_True));
    m_interactionLabel->SetPosition(modeling::sketchPoint(*m_sketch,labelPoint));
    m_interactionLabel->SetHeight(12.0*devicePixelRatioF());
    m_interactionLabel->SetColor(invalid ? Quantity_NOC_RED3 : Quantity_NOC_DARKGOLDENROD);
    m_interactionLabel->SetZLayer(Graphic3d_ZLayerId_Topmost);
    m_interactionLabel->SetInfiniteState(Standard_True);
    m_graphics.context()->Display(m_interactionLabel,0,-1,Standard_False); update();
}

void CadViewport::cancelPointDrag()
{
    if(!m_pointDrag) return;
    m_sketch=m_pointDrag->original; m_pointDrag.reset();
    if(QWidget::mouseGrabber()==this) releaseMouse();
    m_hoveredPoint.reset(); clearInteractionPresentation(); updateSketchPresentation();
}

void CadViewport::updatePointDrag(const QPointF& position)
{
    if(!m_pointDrag || !m_sketch) return;
    auto& drag=*m_pointDrag;
    if(!drag.moved && (position-drag.pressPosition).manhattanLength()<QApplication::startDragDistance()) return;
    drag.moved=true;
    const auto raw=sketchPosition(position); if(!raw) return;
    std::vector<modeling::ProjectedSketchPoint> targets;
    const double dpr=devicePixelRatioF();
    for(const auto& entry:drag.targets) {
        const auto pixel=screenPoint(entry.second);
        targets.push_back({entry.first,entry.second,pixel.x()*dpr,pixel.y()*dpr});
    }
    const modeling::Point2 destination{raw->x+drag.grabOffset.x,raw->y+drag.grabOffset.y};
    const auto targetPixel=screenPoint(destination);
    const auto target=modeling::nearestSketchPoint(targets,targetPixel.x()*dpr,targetPixel.y()*dpr,m_pickingTolerance*dpr);
    drag.target=target ? std::optional<modeling::SketchPointRef>(target->reference) : std::nullopt;
    m_hoveredPoint=drag.target; drag.error.clear();
    try {
        auto warm=drag.original;if(drag.candidate)warm.entities=drag.candidate->entities;
        drag.candidate=modeling::moveSketchPoint(warm,drag.reference,destination,drag.target,drag.circleBody);
        m_sketch=*drag.candidate;
    } catch(const std::exception& error) {
        drag.error=QString::fromUtf8(error.what()); drag.candidate.reset(); m_sketch=drag.original;
    } catch(const Standard_Failure& error) {
        drag.error=QString::fromUtf8(error.GetMessageString()); drag.candidate.reset(); m_sketch=drag.original;
    }
    updateSketchPresentation(); updateInteractionPresentation();
}

void CadViewport::finishPointDrag()
{
    if(!m_pointDrag) return;
    const auto drag=*m_pointDrag; m_pointDrag.reset();
    if(QWidget::mouseGrabber()==this) releaseMouse();
    clearInteractionPresentation();
    if(drag.moved && drag.candidate) {
        m_sketch=*drag.candidate;
        const bool changed=m_sketch->relations.size()!=drag.original.relations.size() || !std::equal(m_sketch->entities.begin(),m_sketch->entities.end(),drag.original.entities.begin(),[](const auto& a,const auto& b){return std::hypot(a.first.x-b.first.x,a.first.y-b.first.y)<1e-9 && std::hypot(a.second.x-b.second.x,a.second.y-b.second.y)<1e-9 && std::abs(a.radius-b.radius)<1e-9 && std::hypot(a.center.x-b.center.x,a.center.y-b.center.y)<1e-9;});
        if(changed)emit sketchChanged(*m_sketch);
    }
    else { m_sketch=drag.original; if(drag.moved && !drag.error.isEmpty()) emit sketchInteractionError(drag.error); }
    updateSketchPresentation();
}

void CadViewport::commitDrawing(modeling::Point2 point, const std::optional<modeling::SketchPointRef>& reference)
{
    if(!m_sketch || !m_firstPoint || !m_tools.drawing()) return;
    const auto first=*m_firstPoint;
    if(samePoint(first,point)) return;
    auto candidate=*m_sketch;
    const auto relation=[&](modeling::RelationKind kind,modeling::SketchPointRef a,modeling::SketchPointRef b) {
        candidate.relations.push_back({sketchId(),kind,a,b});
    };
    const auto attach=[&](modeling::SketchPointRef a,const std::optional<modeling::SketchPointRef>& b,
                          const std::optional<BoundarySnap>& snap) {
        if(b){const bool midpoint=b->role==modeling::PointRole::Midpoint;relation(midpoint?modeling::RelationKind::Midpoint:modeling::RelationKind::Coincident,a,midpoint?modeling::SketchPointRef{b->entityId,modeling::PointRole::First}:*b);}
        else if(snap) attachBoundarySnap(candidate,a,*snap);
    };
    modeling::SketchEntity entity; entity.id=sketchId(); entity.kind=*m_tools.drawing();
    entity.first=first; entity.second=point;entity.construction=m_tools.construction(); entity.radius=std::hypot(point.x-first.x,point.y-first.y);
    try {
    if(entity.kind==modeling::EntityKind::Polygon) {
        try {
            candidate=modeling::addRegularPolygon(candidate,entity.id,{first,entity.radius,std::atan2(point.y-first.y,point.x-first.x),6});
            const auto& group=candidate.polygons.back();
            attach({group.centerId,modeling::PointRole::First},m_firstReference,m_firstBoundarySnap);
            attach({group.edges.front(),modeling::PointRole::First},reference,m_boundarySnap);
        }catch(const std::exception& e){emit sketchInteractionError(QString::fromUtf8(e.what()));return;}
    } else if(entity.kind==modeling::EntityKind::Rectangle) {
        if(std::abs(first.x-point.x)<1e-7 || std::abs(first.y-point.y)<1e-7) return;
        const std::array<modeling::Point2,4> corners{first,{point.x,first.y},point,{first.x,point.y}};
        std::array<std::string,4> ids;
        for(int n=0;n<4;++n) {
            modeling::SketchEntity edge; edge.id=sketchId(); ids[n]=edge.id; edge.kind=modeling::EntityKind::Line;
            edge.first=corners[n]; edge.second=corners[(n+1)%4];
            for(const auto& old:candidate.entities) if(old.kind==modeling::EntityKind::Line &&
                ((samePoint(old.first,edge.first)&&samePoint(old.second,edge.second)) ||
                 (samePoint(old.first,edge.second)&&samePoint(old.second,edge.first)))) return;
            candidate.entities.push_back(edge);
            relation(n%2==0?modeling::RelationKind::Horizontal:modeling::RelationKind::Vertical,
                {edge.id,modeling::PointRole::First},{edge.id,modeling::PointRole::Second});
        }
        for(int n=0;n<4;++n) relation(modeling::RelationKind::Coincident,
            {ids[n],modeling::PointRole::Second},{ids[(n+1)%4],modeling::PointRole::First});
        attach({ids[0],modeling::PointRole::First},m_firstReference,m_firstBoundarySnap);
        attach({ids[1],modeling::PointRole::Second},reference,m_boundarySnap);
    } else if(entity.kind==modeling::EntityKind::Line) {
        for(const auto& old:candidate.entities) if(old.kind==modeling::EntityKind::Line && old.construction==entity.construction &&
            ((samePoint(old.first,first)&&samePoint(old.second,point)) ||
             (samePoint(old.first,point)&&samePoint(old.second,first)))) return;
        candidate.entities.push_back(entity);
        attach({entity.id,modeling::PointRole::First},m_firstReference,m_firstBoundarySnap);
        attach({entity.id,modeling::PointRole::Second},reference,m_boundarySnap);
        const auto anchored = [&](const auto& ref, const auto& snap) {
            return (snap && snap->role) || (ref && (ref->role == modeling::PointRole::Origin ||
                   modeling::sketchProjection(candidate, ref->entityId)));
        };
        if (!(anchored(m_firstReference,m_firstBoundarySnap) && anchored(reference,m_boundarySnap))) {
            if(first.y==point.y) relation(modeling::RelationKind::Horizontal,{entity.id,modeling::PointRole::First},{entity.id,modeling::PointRole::Second});
            if(first.x==point.x) relation(modeling::RelationKind::Vertical,{entity.id,modeling::PointRole::First},{entity.id,modeling::PointRole::Second});
        }
    } else {
        for(const auto& old:candidate.entities) if(old.kind==modeling::EntityKind::Circle && samePoint(old.first,first) && std::abs(old.radius-entity.radius)<1e-7) return;
        candidate.entities.push_back(entity);
        attach({entity.id,modeling::PointRole::Center},m_firstReference,m_firstBoundarySnap);
    }
        modeling::validateSketchRelations(candidate);
        modeling::sketchShape(candidate); // Validate sizes, limits and kernel geometry before replacing the draft.
    } catch (const std::exception& failure) {
        emit sketchInteractionError(QString::fromUtf8(failure.what())); return;
    } catch (const Standard_Failure& failure) {
        emit sketchInteractionError(QString::fromUtf8(failure.GetMessageString())); return;
    }
    m_sketch=candidate;
    m_firstBoundarySnap.reset();
    if(entity.kind==modeling::EntityKind::Line) {
        m_firstPoint=point; m_firstReference=modeling::SketchPointRef{entity.id,modeling::PointRole::Second};
    } else { m_firstPoint.reset(); m_firstReference.reset(); }
    resetSketchInference();
    emit sketchChanged(candidate);
    emit sketchEntityCreated(entity); // Compatibility notification; full sketchChanged owns insertion.
    updateSketchPresentation();
}

std::optional<modeling::Point2> CadViewport::sketchPosition(const QPointF& position)
{
    if (!isReady() || !m_sketch) { return {}; }
    synchronizeSurface();
    const QPoint pixel = nativePosition(position);
    double x, y, z, dx, dy, dz;
    m_graphics.view()->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const auto axes = modeling::sketchAxes(*m_sketch);
    const gp_Vec normal(axes.Direction());
    const gp_Vec ray(dx, dy, dz);
    const double divisor = ray.Dot(normal);
    if (std::abs(divisor) < 1e-4) { return {}; }
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
    if(event->button()==Qt::MiddleButton&&isReady()){beginNavigation(event);return;}
    m_contextClick=event->button()==Qt::RightButton;m_contextPress=event->position();
    m_deleteSequence.invalidate();
    if(m_pointDrag || m_dimensionDrag) {event->accept();return;}
    if (!isReady() || m_camera.mode() != Navigation::None) {
        return;
    }
    if (event->button() == Qt::LeftButton && m_sketchLineEditPicking) {
        setFocus(Qt::MouseFocusReason);
        pointAtSketchLineEdit(event->position(), true);
        event->accept();
        return;
    }
    if(event->button()==Qt::LeftButton && m_sketchEditing && !event->modifiers().testFlag(Qt::ShiftModifier)){
        if(const auto id=relationAt(event->position())){setFocus(Qt::MouseFocusReason);selectSketchRelation(*id);event->accept();return;}
        m_selectedRelation.clear();
    }
    if(event->button()==Qt::LeftButton && m_sketchEditing && event->modifiers().testFlag(Qt::ShiftModifier)){
        if(m_tools.constraintPicking())emit constraintPickingCanceled();
        if(m_tools.drawing())selectSketchTool();
        setFocus(Qt::MouseFocusReason);m_selectionCursor=event->position().toPoint();m_selectedRelation.clear();
        if(!selectionPointAt(event->position()))if(const auto dimension=dimensionAt(event->position())){selectDimension(*dimension,true);event->accept();return;}
        const auto point=selectionPointAt(event->position());const auto body=point?std::optional<std::string>(point->reference.entityId):entityAt(event->position());
        m_selectedDimensions.clear();m_selectedPoint.reset();
        if(body && !body->empty()){
            m_shiftSelectionChanged=true;
            const auto found=std::find(m_selectedEntities.begin(),m_selectedEntities.end(),*body);
            if(found==m_selectedEntities.end())m_selectedEntities.push_back(*body);else m_selectedEntities.erase(found);
        }
        m_selectedEntity=m_selectedEntities.empty()?"":m_selectedEntities.back();
        emit sketchEntitySelected(QString::fromStdString(m_selectedEntity));emit sketchSelectionChanged();
        updatePointPresentation();updateConstraintPresentation();event->accept();return;
    }
    if (event->button() == Qt::LeftButton && m_regionSelecting) {
        m_regionClickCandidate=true;
        m_regionPressPosition=event->position();
    }
    if(event->button()==Qt::LeftButton && m_sketchEditing && !m_sketchModifierPicking &&
        !m_tools.drawing() && !selectionPointAt(event->position())
        && (!m_tools.constraintPicking() || (m_tools.dimensionPicking() && m_dimensionEntities.empty() && m_dimensionPoints.empty())) && !m_dimensionPreview) {
        if(const auto id=dimensionAt(event->position())) {
            setFocus(Qt::MouseFocusReason);
            const bool toggle=event->modifiers().testFlag(Qt::ControlModifier)||event->modifiers().testFlag(Qt::ShiftModifier);selectDimension(*id,toggle);
            if(toggle){event->accept();return;}
            const auto d=std::find_if(m_sketch->dimensions.begin(),m_sketch->dimensions.end(),[&](const auto& v){return v.id==*id;});
            const auto raw=sketchPosition(event->position());
            if(raw && d!=m_sketch->dimensions.end()) {
                const auto text=modeling::dimensionAnnotationPosition(*m_sketch,*d);
                m_dimensionDrag=DimensionDrag{*m_sketch,*id,event->position(),{text.x-raw->x,text.y-raw->y},false};
                grabMouse();event->accept();return;
            }
        }
    }
    if(event->button()==Qt::LeftButton && m_tools.constraintPicking() && m_sketchEditing) {
        setFocus(Qt::MouseFocusReason);const auto hit=selectionPointAt(event->position());
        const auto body=hit?std::optional<std::string>(hit->reference.entityId):entityAt(event->position());
        if(m_tools.dimensionPicking()) {
            if(hit)emit dimensionReferencePicked(QString::fromStdString(hit->reference.entityId),int(hit->reference.role));
            else if(body && !body->empty())emit dimensionReferencePicked(QString::fromStdString(*body),-1);
            else if (const auto position = sketchPosition(event->position())) {
                if (const auto snap = boundarySnapAt(event->position(), *position))
                    emit dimensionBodyReferencePicked(m_sketchBoundaryReferences.at(snap->index),
                        snap->role && snap->role != modeling::PointRole::Midpoint ? int(*snap->role) : -1);
                else
                    emit dimensionPlacementRequested(*position);
            }
            event->accept();return;
        }
        if(body && !body->empty()){m_selectedEntity=*body;m_selectedEntities={*body};m_selectedDimensions.clear();emit sketchEntitySelected(QString::fromStdString(*body));}
        if(hit){m_selectedPoint=hit->reference;emit sketchPointSelected(QString::fromStdString(hit->reference.entityId),int(hit->reference.role));}
        else if(!body){const auto position=sketchPosition(event->position());if(position)emit dimensionPlacementRequested(*position);}
        updatePointPresentation();event->accept();return;
    }
    if (event->button() == Qt::LeftButton && m_sketchEditing) {
        setFocus(Qt::MouseFocusReason);
        const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
        if (m_sketchConversionPicking ||
            (!m_tools.drawing() && !selectionPointAt(event->position()) && !entityAt(event->position())))
        {
            const auto candidates = selectionCandidates(event->position());
            const auto model = std::find_if(candidates.begin(), candidates.end(),
                [](const auto& item) { return modeling::isTopology(item.value.reference.kind); });
            if (model != candidates.end())
            {
                selectCandidate(*model, toggle);
                event->accept();
                return;
            }
            if (m_sketchConversionPicking)
            {
                if (!toggle) clearSelection();
                event->accept();
                return;
            }
        }
        if (!toggle && !m_selection.items().empty()) beginTreeSelection();
        std::optional<modeling::SketchPointRef> reference;
        const auto raw = sketchPosition(event->position());
        if(m_tools.drawing()==modeling::EntityKind::Point && raw){placeSketchPoint(event->position());event->accept();return;}
        if(m_tools.drawing()&&!raw){event->accept();return;}
        if (drawingArcOrSlot()) {
            pickArcOrSlot(event->position(), *raw);
            event->accept();return;
        }
        if (m_tools.drawing() && raw) {
            const auto point = drawingPoint(event->position(), reference);
            if (!m_firstPoint) { m_firstPoint = point; m_firstReference = reference; m_firstBoundarySnap = m_boundarySnap; }
            else { commitDrawing(point, reference); }
        } else {
            const auto hit = selectionPointAt(event->position());
            m_selectedPoint = hit ? std::optional<modeling::SketchPointRef>(hit->reference) : std::nullopt;
            const auto body = hit ? std::optional<std::string>(hit->reference.entityId) : entityAt(event->position());
            if(!toggle){m_selectedEntities.clear();m_selectedDimensions.clear();}
            if(body && !body->empty()) {
                const auto found=std::find(m_selectedEntities.begin(),m_selectedEntities.end(),*body);
                if(toggle && found!=m_selectedEntities.end())m_selectedEntities.erase(found);else m_selectedEntities.push_back(*body);
            }
            m_selectedEntity=m_selectedEntities.empty()?"":m_selectedEntities.back();
            emit sketchEntitySelected(QString::fromStdString(m_selectedEntity));
            m_selectedPoint = hit ? std::optional<modeling::SketchPointRef>(hit->reference) : std::nullopt;
            if (hit) { emit sketchPointSelected(QString::fromStdString(hit->reference.entityId), int(hit->reference.role)); }
            emit sketchSelectionChanged();
            std::optional<modeling::SketchPointRef> dragRef=hit?std::optional<modeling::SketchPointRef>(hit->reference):std::nullopt;
            if(!hit && body && raw){const auto e=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==*body;});if(e!=m_sketch->entities.end() && e->kind==modeling::EntityKind::Circle)dragRef=modeling::SketchPointRef{e->id,modeling::PointRole::Center};}
            if(dragRef && !toggle && !m_sketchModifierPicking && dragRef->role!=modeling::PointRole::Origin &&
               !modeling::sketchProjection(*m_sketch, dragRef->entityId)) {
                const auto entity=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==dragRef->entityId;});
                if(entity!=m_sketch->entities.end() && (entity->kind==modeling::EntityKind::Line || modeling::isCircular(entity->kind) || entity->kind==modeling::EntityKind::Point)) {
                    m_pointDrag=PointDrag{*m_sketch,*dragRef,event->position(),false,*m_sketch,{},{}};
                    if(!hit && raw){m_pointDrag->circleBody=true;m_pointDrag->grabOffset={entity->first.x-raw->x,entity->first.y-raw->y};}
                    std::set<std::pair<std::string,int>> excluded;
                    for(const auto& ref:modeling::coincidentSketchPoints(*m_sketch,*dragRef)) excluded.insert({ref.entityId,int(ref.role)});
                    for(const auto& entry:modeling::sketchPoints(*m_sketch))
                        if(!excluded.count({entry.first.entityId,int(entry.first.role)})) m_pointDrag->targets.push_back(entry);
                    if(!m_pointDrag->circleBody)for(const auto& e:m_sketch->entities)if(e.kind==modeling::EntityKind::Line && !excluded.count({e.id,int(modeling::PointRole::First)}) && !excluded.count({e.id,int(modeling::PointRole::Second)})){
                        const modeling::SketchPointRef ref{e.id,modeling::PointRole::Midpoint};m_pointDrag->targets.push_back({ref,modeling::sketchPointValue(*m_sketch,ref)});
                    }
                    grabMouse();
                }
            }
            updatePointPresentation();
        }
        event->accept();
        return;
    }
    if(event->button()==Qt::LeftButton){m_planeClickCandidate=!m_sketchEditing&&!m_regionSelecting;m_planePressPosition=event->position();setFocus(Qt::MouseFocusReason);event->accept();}

}

void CadViewport::mouseMoveEvent(QMouseEvent* event)
{
    if(m_camera.mode()!=Navigation::None){moveNavigation(event);return;}
    if((event->position()-m_contextPress).manhattanLength()>QApplication::startDragDistance())m_contextClick=false;
    if(m_shiftSelectionChanged)m_selectionCursor=event->position().toPoint();
    if(m_dimensionDrag){updateDimensionDrag(event->position());event->accept();return;}
    if(m_tools.dimensionPicking() && m_camera.mode()==Navigation::None){
        updateSketchHover(event->position());
        const auto point=sketchPosition(event->position());if(point)emit dimensionPointerMoved(*point);event->accept();return;
    }
    if(m_pointDrag) { updatePointDrag(event->position()); event->accept(); return; }
    if(!m_sketchEditing && m_camera.mode()==Navigation::None) {
        if(!m_referenceCandidates.empty())updateReferenceHover(event->position());
        else if(!m_regionSelecting)updateSelectionHover(event->position());
    }
    if(m_regionClickCandidate && (event->position()-m_regionPressPosition).manhattanLength()>QApplication::startDragDistance())
        m_regionClickCandidate=false;
    if (m_planeClickCandidate
        && (event->position() - m_planePressPosition).manhattanLength() > QApplication::startDragDistance()) {
        m_planeClickCandidate = false;
    }
    if (isReady() && m_sketchEditing && m_camera.mode() == Navigation::None) {
        if (m_sketchLineEditPicking) {
            pointAtSketchLineEdit(event->position(), false);
            return;
        }
        if (m_sketchConversionPicking) {
            updateSelectionHover(event->position());
            return;
        }
        updateSketchHover(event->position());
        if (drawingArcOrSlot()) {
            previewArcOrSlot(event->position());
            return;
        }
        if (m_tools.drawing() && m_firstPoint) {
            std::optional<modeling::SketchPointRef> reference;
            if(!sketchPosition(event->position())){m_drawingPreview.reset();updateSketchPresentation();return;}
            const auto point = drawingPoint(event->position(), reference);
            modeling::SketchEntity preview;
            preview.kind = *m_tools.drawing();
            preview.first = *m_firstPoint; preview.second = point;
            preview.radius = std::hypot(point.x - m_firstPoint->x, point.y - m_firstPoint->y);preview.construction=m_tools.construction();m_drawingPreview=preview;
            updateSketchPresentation(preview);
            updateInteractionPresentation();
        } else {
            if (m_tools.drawing()) {
                std::optional<modeling::SketchPointRef> reference;
                if (sketchPosition(event->position())) (void)drawingPoint(event->position(), reference);
                updateInteractionPresentation();
            }
            updatePointPresentation();
        }
        return;
    }
    event->accept();
}

void CadViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if(event->button()==Qt::LeftButton && m_dimensionDrag) {
        updateDimensionDrag(event->position());const bool moved=m_dimensionDrag->moved;m_dimensionDrag.reset();
        if(QWidget::mouseGrabber()==this)releaseMouse();
        if(moved)emit sketchChanged(*m_sketch);
        updateConstraintPresentation();update();event->accept();return;
    }
    if(event->button()==Qt::LeftButton && m_pointDrag) { updatePointDrag(event->position()); finishPointDrag(); event->accept(); return; }
    if(event->button()==m_camera.button()&&m_camera.mode()!=Navigation::None){m_camera.stop();updateConstraintPresentation();update();unsetCursor();if(QWidget::mouseGrabber()==this)releaseMouse();event->accept();return;}
    if (event->button() == Qt::LeftButton) {
        const bool regionClick=m_regionClickCandidate && event->button()==Qt::LeftButton
            && (event->position()-m_regionPressPosition).manhattanLength()<=QApplication::startDragDistance();
        m_regionClickCandidate=false;
        const bool click = m_planeClickCandidate && event->button() == Qt::LeftButton
            && (event->position() - m_planePressPosition).manhattanLength() <= QApplication::startDragDistance();
        m_camera.stop();

        m_planeClickCandidate = false;
        unsetCursor();
        if(regionClick && m_regionSelecting) {
            if (!m_referenceCandidates.empty()) {
                const int index = referenceCandidateAt(event->position());
                if (index >= 0) emit referenceGeometryPicked(index);
            } else {
                const auto point=sketchPosition(event->position());if(point) emit sketchRegionClicked(*point);
            }
        } else if (click) {
            if(!m_referenceCandidates.empty()){const int index=referenceCandidateAt(event->position());if(index>=0)emit referenceGeometryPicked(index);event->accept();return;}
            const auto candidates=selectionCandidates(event->position());
            if(candidates.empty())clearSelection();
            else selectCandidate(candidates.front(),event->modifiers().testFlag(Qt::ShiftModifier)||event->modifiers().testFlag(Qt::ControlModifier),true);
            event->accept();return;
        }
        event->accept();
    }
}

bool CadViewport::event(QEvent* event)
{
    if (event->type() == QEvent::Leave && m_sketchLineEditPicking)
        emit sketchLineEditPointed({}, {}, false);
    if(event->type()==QEvent::Leave)previewCandidate({});
    if(event->type()==QEvent::Leave&&isReady()){
        m_hoveredDatum.clear();updateDatumStyles();
        m_hoveredReferenceCandidate = -1;
        if(!m_referenceHover.IsNull()){m_graphics.context()->Remove(m_referenceHover,Standard_False);m_referenceHover.Nullify();update();}
    }
    if(event->type()==QEvent::FocusOut)m_deleteSequence.invalidate();
    if(event->type()==QEvent::Leave){m_hoveredRelation.clear();m_deleteSequence.invalidate();m_hoveredEntity.clear();m_hoveredDimension.clear();updatePointPresentation();updateConstraintPresentation();}
    if(event->type()==QEvent::Leave && !m_pointDrag) resetSketchInference();
    if (event->type() == QEvent::Leave && m_hoveredPoint) { m_hoveredPoint.reset(); updatePointPresentation(); }
    if (event->type() == QEvent::Leave && m_hoveredPlane) {
        m_hoveredPlane.reset();
        updatePlaneStyles();
    }
    if (event->type() == QEvent::DevicePixelRatioChange || event->type() == QEvent::ScreenChangeInternal) {
        m_graphics.invalidateSize();
        update();
    }
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
        m_deleteSequence.invalidate();cancelPointDrag();cancelDimensionDrag();
        if(m_camera.mode()!=Navigation::None){updateConstraintPresentation();update();}
        m_camera.stop();

        m_planeClickCandidate = false;m_regionClickCandidate=false;
        unsetCursor();
    }
    return QWidget::event(event);
}

void CadViewport::wheelEvent(QWheelEvent* event)
{
    if (!isReady()) {
        return;
    }
    resetSketchInference();
    const double steps = !event->angleDelta().isNull()
        ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 40.0;
    if(steps==0){event->accept();return;}
    const double factor = std::pow(1.15, -std::clamp(steps, -10.0, 10.0));
    if(factor>1)zoomToward(event->position(),factor);
    else viewport::CameraNavigation::zoomCentered(m_graphics.view(),factor);
    m_cameraAnnotationsDirty=true;
    update();
    event->accept();
}

void CadViewport::keyPressEvent(QKeyEvent* event)
{
    if(event->key()==Qt::Key_Escape&&!m_sketchEditing&&!m_selectingPlane&&!m_selection.items().empty()){clearSelection();event->accept();return;}
    if(event->key()==Qt::Key_Escape&&!m_sketchEditing&&!m_selectingPlane&&!m_selectedDatum.empty()){setSelectedDatum({});emit datumSelected({});event->accept();return;}
    if(event->key()==Qt::Key_Escape && !m_sketchEditing && !m_selectingPlane && m_selectedSolidFace){setSelectedSolidFace(0);emit solidFaceSelected(0);event->accept();return;}
    if(event->key()==Qt::Key_Delete && !m_selectedRelation.empty()){emit sketchRelationDeleteRequested(QString::fromStdString(m_selectedRelation));event->accept();return;}
    const bool selected=!m_selectedEntities.empty() || !m_selectedDimensions.empty();
    const bool deletionMode=m_sketchEditing && !m_tools.drawing() && !m_pointDrag && !m_dimensionDrag
        && (!m_tools.constraintPicking() || m_tools.dimensionPicking()) && !m_dimensionPreview;
    if(deletionMode && event->key()==Qt::Key_Delete && event->modifiers()==Qt::NoModifier){
        m_deleteSequence.invalidate();if(selected)emit sketchDeleteRequested();event->accept();return;
    }
    if(deletionMode && selected && (!m_tools.dimensionPicking() || !m_selectedDimensions.empty())
        && event->key()==Qt::Key_D && event->modifiers()==Qt::NoModifier){
        if(event->isAutoRepeat()){m_deleteSequence.invalidate();event->accept();return;}
        if(m_deleteSequence.isValid() && m_deleteSequence.elapsed()<=750){
            m_deleteSequence.invalidate();emit sketchDeleteRequested();
        }else m_deleteSequence.start();
        event->accept();return;
    }
    m_deleteSequence.invalidate();
    if(event->key()==Qt::Key_Escape && m_dimensionDrag){cancelDimensionDrag();event->accept();return;}
    if(m_tools.dimensionPicking() && (event->key()==Qt::Key_R || event->key()==Qt::Key_D)){emit dimensionVariantRequested(event->key()==Qt::Key_R);event->accept();return;}
    if(event->key()==Qt::Key_Escape && m_tools.constraintPicking()){emit constraintPickingCanceled();event->accept();return;}
    if (event->key() == Qt::Key_Escape && m_selectingPlane) {
        cancelPlaneSelection();
    } else if (event->key() == Qt::Key_Escape && m_sketchEditing) {
        selectSketchTool();
    } else if (event->key() == Qt::Key_F) {
        fitAll();
    } else if (event->key() == Qt::Key_Home) {
        resetView();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void CadViewport::keyReleaseEvent(QKeyEvent* event)
{
    if(event->key()==Qt::Key_Shift && !event->isAutoRepeat() && finishSketchMultiSelection()){event->accept();return;}
    QWidget::keyReleaseEvent(event);
}

bool CadViewport::finishSketchMultiSelection(){
    if(!m_shiftSelectionChanged)return false;
    m_shiftSelectionChanged=false;
    if(m_sketchEditing && !m_tools.drawing() && !m_tools.constraintPicking() && !m_selectedEntities.empty())emit relationsContextRequested(m_selectionCursor);
    return true;
}

void CadViewport::cancelSketchMultiSelection(){m_shiftSelectionChanged=false;}

void CadViewport::fitAll()
{
    if (isReady()) {
        synchronizeSurface();
        m_graphics.view()->FitAll(0.1, Standard_False);
        m_graphics.view()->ZFitAll();
        updateConstraintPresentation();
        update();
    }
}

void CadViewport::resetView()
{
    if (isReady()) {
        if (m_sketchEditing) { orientSketch(); return; }
        m_graphics.view()->SetProj(V3d_XposYnegZpos);
        fitAll();
    }
}

} // namespace thecad::ui
