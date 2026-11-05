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
        const int maximumSamples = driver->InquireLimit(Graphic3d_TypeOfLimit_MaxMsaa);
        int samples = 0;
        for (int candidate = 2; candidate <= std::min(4, maximumSamples); candidate *= 2) samples = candidate;
        m_view->ChangeRenderingParams().NbMsaaSamples = samples;
        m_view->ChangeRenderingParams().IsAntialiasingEnabled = Standard_True;
        m_view->ChangeRenderingParams().TransparencyMethod = Graphic3d_RTM_BLEND_OIT;
        if (!window->IsMapped()) {
            window->Map();
        }
        m_view->SetBgGradientColors(
            Quantity_Color(0.90, 0.92, 0.95, Quantity_TOC_RGB),
            Quantity_Color(0.99, 0.99, 0.99, Quantity_TOC_RGB), Aspect_GFM_VER);
        m_view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GRAY30, 0.08, V3d_ZBUFFER);

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
    for(auto& lines:m_planeCenterlines) lines.Nullify();
    m_globalOrigin.Nullify();
    for (auto& axis : m_sketchAxes) { axis.Nullify(); }
    m_sketchOrigin.Nullify();
    for(auto& axis:m_globalAxes) axis.Nullify();
    m_pointPresentations.clear();
    m_constraintLabels.clear();m_dimensionGuides.clear();m_relationMarkers.clear();m_relationHits.clear();
    m_interactionLabel.Nullify();
    m_inferenceGuides.clear();
    m_selectedEntityPresentation.Nullify();
    m_hoveredEntityPresentation.Nullify();
    m_previewPresentation.Nullify();
    m_sketchPresentation.Nullify();m_constructionPresentation.Nullify();
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
        updateConstraintPresentation();
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
        m_context->Deactivate(candidate);
        m_context->Activate(candidate,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
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
    updateGlobalAxes();
    if (!isReady()) { return; }
    for (auto& plane : m_referencePlanes) {
        if (!plane.IsNull()) { m_context->Remove(plane, Standard_False); plane.Nullify(); }
    }
    for (auto& label : m_planeLabels) {
        if (!label.IsNull()) { m_context->Remove(label, Standard_False); label.Nullify(); }
    }
    for(auto& lines:m_planeCenterlines) {if(!lines.IsNull())m_context->Remove(lines,Standard_False);lines.Nullify();}
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
            m_planeCenterlines[i]->Attributes()->SetWireAspect(new Prs3d_LineAspect(Quantity_Color(Quantity_NOC_GRAY20),Aspect_TOL_DASH,devicePixelRatioF()));
            m_planeCenterlines[i]->SetInfiniteState(Standard_True);
            m_context->Display(m_planeCenterlines[i],AIS_WireFrame,-1,Standard_False);
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
        const Quantity_Color color = (selected || hovered) ? Quantity_Color(1.0,0.42,0.02,Quantity_TOC_RGB)
            : (i==1 ? Quantity_Color(0.25,0.42,1.0,Quantity_TOC_RGB) : Quantity_Color(0.09,0.18,0.42,Quantity_TOC_RGB));
        plane->SetColor(Quantity_Color(0.72,0.78,0.90,Quantity_TOC_RGB));
        plane->SetTransparency(0.88);
        plane->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(color,Aspect_TOL_SOLID,(selected||hovered?1.5:1.0)*devicePixelRatioF()));
        m_context->Redisplay(plane, Standard_False);
        if (!m_planeLabels[std::size_t(i)].IsNull()) {
            m_planeLabels[std::size_t(i)]->SetColor(i==1 ? Quantity_Color(0.25,0.42,1.0,Quantity_TOC_RGB) : Quantity_Color(0.09,0.18,0.42,Quantity_TOC_RGB));
            m_context->Redisplay(m_planeLabels[std::size_t(i)], Standard_False);
        }
    }
    update();
}

int CadViewport::faceAt(const QPointF& position)
{
    if(!isReady() || m_presentation.IsNull()) return 0;
    const auto pixel=nativePosition(position);
    m_context->MoveTo(pixel.x(),pixel.y(),m_view,Standard_False);
    update();
    if(!m_context->HasDetected() || m_context->DetectedInteractive()!=m_presentation)return 0;
    const auto owner=Handle(StdSelect_BRepOwner)::DownCast(m_context->DetectedOwner());
    if(owner.IsNull() || !owner->HasShape())return 0;
    const auto face=owner->Shape();if(face.ShapeType()!=TopAbs_FACE)return 0;
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(m_shape,TopAbs_FACE,faces);return faces.FindIndex(face);
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

void CadViewport::updateGlobalAxes()
{
    if(!isReady()) return;
    for(auto& axis:m_globalAxes) {if(!axis.IsNull()) m_context->Remove(axis,Standard_False);axis.Nullify();}
    if(!m_globalOrigin.IsNull()){m_context->Remove(m_globalOrigin,Standard_False);m_globalOrigin.Nullify();}
    if(m_sketchEditing) return;
    m_globalOrigin=new AIS_Point(new Geom_CartesianPoint(gp_Pnt(0,0,0)));
    m_globalOrigin->Attributes()->SetPointAspect(filledPoint(Quantity_Color(Quantity_NOC_RED),4.0*devicePixelRatioF()));
    m_globalOrigin->SetInfiniteState(Standard_True);m_context->Display(m_globalOrigin,0,-1,Standard_False);
    const double length=std::max(5.0,m_planeHalfExtent*0.25);
    for(int i=0;i<3;++i) {
        const gp_Vec direction(i==0?1:0,i==1?1:0,i==2?1:0);
        const Quantity_Color color(i==0?Quantity_NOC_RED:i==1?Quantity_NOC_GREEN:Quantity_NOC_BLUE);
        m_globalAxes[i]=datumArrow(gp_Pnt(0,0,0),gp_Dir(direction),length,color);
        m_context->Display(m_globalAxes[i],0,-1,Standard_False);
    }
}

void CadViewport::updateSketchAxes()
{
    if (!isReady()) { return; }
    updateGlobalAxes();
    for (auto& axis : m_sketchAxes) {
        if (!axis.IsNull()) { m_context->Remove(axis, Standard_False); axis.Nullify(); }
    }
    if (!m_sketchOrigin.IsNull()) { m_context->Remove(m_sketchOrigin, Standard_False); m_sketchOrigin.Nullify(); }
    if (m_sketchEditing && m_sketch) {
        const auto axes = modeling::sketchAxes(*m_sketch);
        const double span = std::max(m_view->Camera()->Scale(), 1.0)*0.04;
        for (int i = 0; i < 2; ++i) {
            const gp_Vec direction(i == 0 ? axes.XDirection() : axes.YDirection());
            auto& axis=m_sketchAxes[std::size_t(i)];
            axis=datumArrow(axes.Location(),gp_Dir(direction),span,Quantity_Color(Quantity_NOC_RED));
            axis->SetZLayer(Graphic3d_ZLayerId_Topmost);
            m_context->Display(axis,0,-1,Standard_False);
        }
        m_sketchOrigin = new AIS_Point(new Geom_CartesianPoint(axes.Location()));
        m_sketchOrigin->SetZLayer(Graphic3d_ZLayerId_Topmost);
        m_sketchOrigin->Attributes()->SetPointAspect(filledPoint(Quantity_Color(Quantity_NOC_RED3),4.0*devicePixelRatioF()));
        m_sketchOrigin->SetInfiniteState(Standard_True);
        m_context->Display(m_sketchOrigin,0,-1,Standard_False);
    }
    update();
}
void CadViewport::beginSketch(const modeling::Sketch& sketch)
{
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_selectingPlane = false;
    showReferencePlanes(false);
    clearPreview();
    m_regionSelecting = false;
    m_sketchEditing = true;
    m_shiftSelectionChanged=false;
    m_sketchTool.reset();m_constructionTool=false;m_drawingPreview.reset();
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
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_sketchEditing = false;
    updateSketchAxes();
    m_sketchTool.reset();
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
    resetView();
}

void CadViewport::beginContourSelection(const modeling::Sketch& sketch)
{
    cancelDimensionDrag();cancelPointDrag();resetSketchInference();
    m_sketchTool.reset();m_firstPoint.reset();m_firstReference.reset();
    showReferencePlanes(false);
    m_sketchEditing = false;
    m_regionSelecting = true;
    updateSketchAxes();
    m_firstPoint.reset();
    setSketch(sketch);
    orientSketch();
}

void CadViewport::endContourSelection()
{
    m_navigation=Navigation::None;m_navigationButton=Qt::NoButton;m_regionClickCandidate=false;
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

QPoint CadViewport::dimensionScreenPoint(modeling::Point2 point) const {return screenPoint(point).toPoint();}
QString CadViewport::dimensionText(const modeling::SketchDimension& d) const {
    const QString prefix=d.kind==modeling::DimensionKind::CircleRadius?"R":d.kind==modeling::DimensionKind::CircleDiameter?QString::fromUtf8("Ø"):"";
    const double value=d.driving?d.value:modeling::dimensionMeasurement(*m_sketch,d);
    const QString text=prefix+QString::number(value,'g',6)+(d.kind==modeling::DimensionKind::LineAngle?QString::fromUtf8("°"):" mm");
    return d.driving?text:"("+text+")";
}
void CadViewport::setPickingTolerance(double pixels) {
    if(std::isfinite(pixels) && pixels>0)m_pickingTolerance=pixels;
}
std::optional<std::string> CadViewport::dimensionAt(const QPointF& position) const {
    if(!isReady() || !m_sketch)return {};
    double best=m_pickingTolerance;std::optional<std::string> result;
    QFont textFont("Arial");textFont.setPixelSize(14);const QFontMetricsF metrics(textFont);
    for(const auto& d:m_sketch->dimensions) {
        const auto graphics=modeling::dimensionGraphics(*m_sketch,d,m_view->Camera()->Scale()/std::max(1,height()));
        const auto anchor=screenPoint(graphics.text);
        // AIS text uses a left baseline anchor; use the actual string extent.
        const double width=metrics.horizontalAdvance(dimensionText(d));
        const double dx=std::max({anchor.x()-position.x(),0.0,position.x()-(anchor.x()+width)});
        const double dy=std::max({anchor.y()-metrics.ascent()-position.y(),0.0,position.y()-(anchor.y()+metrics.descent())});
        double distance=std::hypot(dx,dy);
        for(const auto& segment:graphics.segments) {
            const auto a=screenPoint(segment.first),v=screenPoint(segment.second)-a,delta=position-a;
            const double n=QPointF::dotProduct(v,v);
            const double t=n>0?std::clamp(QPointF::dotProduct(delta,v)/n,0.0,1.0):0;
            const auto offset=delta-v*t;distance=std::min(distance,std::hypot(offset.x(),offset.y()));
        }
        if(distance<best){best=distance;result=d.id;}
    }
    return result;
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
    const bool midpointTool=m_sketchTool==modeling::EntityKind::Line || m_sketchTool==modeling::EntityKind::Point;
    const auto hit=m_sketchTool?pointAt(position,midpointTool):selectionPointAt(position);
    m_hoveredPoint=hit?std::optional<modeling::SketchPointRef>(hit->reference):std::nullopt;
    if(m_hoveredPoint && m_hoveredPoint->role==modeling::PointRole::Midpoint){if(!oldPoint || oldPoint->entityId!=m_hoveredPoint->entityId || oldPoint->role!=m_hoveredPoint->role)QToolTip::showText(mapToGlobal(position.toPoint()),"Midpoint",this);}
    else if(oldPoint && oldPoint->role==modeling::PointRole::Midpoint)QToolTip::hideText();
    m_hoveredDimension=(!hit && !m_sketchTool)?dimensionAt(position).value_or(""):"";
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
    selectSketchTool();m_smartDimensionPicking=enabled;m_constraintPicking=enabled;emit sketchToolChanged();
}
void CadViewport::setDimensionPreview(const std::optional<modeling::SketchDimension>& dimension){m_dimensionPreview=dimension;updateConstraintPresentation();update();}
void CadViewport::setDimensionSelection(const std::vector<std::string>& entities,const std::vector<modeling::SketchPointRef>& points){m_dimensionEntities=entities;m_dimensionPoints=points;updatePointPresentation();update();}
void CadViewport::setConstraintErrors(const std::vector<std::string>& ids){m_constraintErrors=ids;updateConstraintPresentation();update();}
void CadViewport::setConstraintPicking(bool enabled) {selectSketchTool();m_constraintPicking=enabled;emit sketchToolChanged();}
void CadViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    if(m_sketchEditing && m_sketch && isReady() && event->button()==Qt::LeftButton) {
        cancelPointDrag();
        cancelDimensionDrag();
        if(const auto id=dimensionAt(event->position())) {
            emit dimensionEditRequested(QString::fromStdString(*id));event->accept();return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}
void CadViewport::updateConstraintPresentation() {
    if(!isReady())return;
    for(auto& label:m_constraintLabels)m_context->Remove(label,Standard_False);m_constraintLabels.clear();
    for(auto& guide:m_dimensionGuides)m_context->Remove(guide,Standard_False);m_dimensionGuides.clear();
    updateRelationSymbols();
    if(!m_sketch)return;
    const double unit=m_view->Camera()->Scale()/std::max(1,height());
    const auto label=[&](modeling::Point2 position,const QString& text,const Quantity_Color& color,double size) {
        Handle(AIS_TextLabel) item=new AIS_TextLabel;item->SetText(TCollection_ExtendedString(text.toStdWString().c_str()));
        item->SetFont("Arial");item->SetHJustification(Graphic3d_HTA_LEFT);item->SetVJustification(Graphic3d_VTA_BOTTOM);
        item->SetPosition(modeling::sketchPoint(*m_sketch,position));item->SetColor(color);item->SetHeight(size*devicePixelRatioF());item->SetZLayer(Graphic3d_ZLayerId_Topmost);item->SetInfiniteState(Standard_True);
        m_context->Display(item,0,-1,Standard_False);m_constraintLabels.push_back(item);
    };
    const auto guide=[&](modeling::Point2 a,modeling::Point2 b,const Quantity_Color& color) {
        if(std::hypot(a.x-b.x,a.y-b.y)<1e-7)return;
        Handle(AIS_Shape) item=new AIS_Shape(BRepBuilderAPI_MakeEdge(modeling::sketchPoint(*m_sketch,a),modeling::sketchPoint(*m_sketch,b)).Shape());
        item->SetColor(color);item->SetWidth(devicePixelRatioF());item->SetZLayer(Graphic3d_ZLayerId_Topmost);item->SetInfiniteState(Standard_True);m_context->Display(item,AIS_WireFrame,-1,Standard_False);m_dimensionGuides.push_back(item);
    };
    auto dimensions=m_sketch->dimensions;if(m_dimensionPreview)dimensions.push_back(*m_dimensionPreview);
    for(const auto& d:dimensions) {
        const bool selected=d.first.entityId==m_selectedEntity || d.second.entityId==m_selectedEntity;
        const bool error=std::find(m_constraintErrors.begin(),m_constraintErrors.end(),d.id)!=m_constraintErrors.end();
        const bool selectedAnnotation=std::find(m_selectedDimensions.begin(),m_selectedDimensions.end(),d.id)!=m_selectedDimensions.end();
        Quantity_Color color(error?Quantity_NOC_RED:d.id==m_hoveredDimension?Quantity_NOC_ORANGE:d.id=="pending-dimension"?Quantity_NOC_BLUE:Quantity_NOC_BLACK);
        if(!error && (selectedAnnotation || selected))color=Quantity_Color(0.25,0.72,1.0,Quantity_TOC_RGB);
        const auto graphics=modeling::dimensionGraphics(*m_sketch,d,unit);
        label(graphics.text,dimensionText(d),color,14);
        for(const auto& segment:graphics.segments)guide(segment.first,segment.second,color);
    }

}

void CadViewport::setSketchTool(modeling::EntityKind kind,bool construction)
{
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_selectedRelation.clear();
    m_sketchTool = kind;m_constructionTool=construction;m_drawingPreview.reset();
    m_smartDimensionPicking=false;m_constraintPicking=false;
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
    if(reference){const bool midpoint=reference->role==modeling::PointRole::Midpoint;candidate.relations.push_back({relationId,midpoint?modeling::RelationKind::Midpoint:modeling::RelationKind::Coincident,{entity.id,modeling::PointRole::First},midpoint?modeling::SketchPointRef{reference->entityId,modeling::PointRole::First}:*reference});}
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
    try{modeling::validateSketchRelations(candidate);(void)modeling::sketchShape(candidate);m_sketch=candidate;emit sketchChanged(candidate);emit sketchEntityCreated(entity);updateSketchPresentation();}
    catch(const std::exception& e){emit sketchInteractionError(QString::fromUtf8(e.what()));}
}

void CadViewport::selectSketchTool()
{
    cancelDimensionDrag();cancelPointDrag(); resetSketchInference();
    m_sketchTool.reset(); m_firstPoint.reset(); m_firstReference.reset();m_constructionTool=false;m_drawingPreview.reset();
    m_hoveredPoint.reset();QToolTip::hideText();
    m_smartDimensionPicking=false;m_constraintPicking=false;
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
    m_view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    // Changing the target also changes OCCT's view direction. Center first,
    // then set the final normal/up frame, including when a body is off-origin.
    m_view->SetAt(axes.Location().X(),axes.Location().Y(),axes.Location().Z());
    m_view->SetProj(normal.X(), normal.Y(), normal.Z());
    m_view->SetUp(up.X(), up.Y(), up.Z());
    double span = 75.0;
    for (const auto& entry : modeling::sketchPoints(*m_sketch)) {
        span = std::max({span, std::abs(entry.second.x) * 1.25, std::abs(entry.second.y) * 1.25});
    }
    for (const auto& entity : m_sketch->entities) {
        if (entity.kind == modeling::EntityKind::Circle)
            span = std::max({span, (std::abs(entity.first.x)+entity.radius)*1.25,
                                  (std::abs(entity.first.y)+entity.radius)*1.25});
    }
    m_view->Camera()->SetScale(span * 2.0);
    updateConstraintPresentation();
    update();
}

void CadViewport::updateSketchPresentation(const std::optional<modeling::SketchEntity>& preview)
{
    if (!isReady()) { return; }
    if (!m_sketchPresentation.IsNull()) {
        m_context->Remove(m_sketchPresentation, Standard_False);
        m_sketchPresentation.Nullify();
    }
    if(!m_constructionPresentation.IsNull()){m_context->Remove(m_constructionPresentation,Standard_False);m_constructionPresentation.Nullify();}
    if(m_sketch)m_sketchFullyDefined=modeling::solvePlanarSketch(*m_sketch).diagnostics.state==modeling::SolveState::FullyConstrained;
    if (m_sketch) {
        try {
            auto visible = *m_sketch;
            if(preview){
                if(preview->kind==modeling::EntityKind::Polygon && preview->radius>1e-6)
                    visible=modeling::addRegularPolygon(visible,"preview-polygon",{preview->first,preview->radius,std::atan2(preview->second.y-preview->first.y,preview->second.x-preview->first.x),6});
                else if(preview->kind!=modeling::EntityKind::Polygon)visible.entities.push_back(*preview);
            }
            auto construction=visible;
            construction.entities.erase(std::remove_if(construction.entities.begin(),construction.entities.end(),[](const auto& e){return !e.construction || e.kind==modeling::EntityKind::Point;}),construction.entities.end());
            construction.relations.clear();construction.dimensions.clear();construction.polygons.clear();
            if(!construction.entities.empty()){
                m_constructionPresentation=new AIS_Shape(modeling::sketchShape(construction));
                const Quantity_Color color(0.30,0.40,0.62,Quantity_TOC_RGB);
                const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(color,Aspect_TOL_DOTDASH,1.5*devicePixelRatioF());
                m_constructionPresentation->Attributes()->SetWireAspect(aspect);m_constructionPresentation->Attributes()->SetLineAspect(aspect);
                m_constructionPresentation->SetZLayer(Graphic3d_ZLayerId_Topmost);m_context->Display(m_constructionPresentation,AIS_WireFrame,-1,Standard_False);
            }
            visible.entities.erase(std::remove_if(visible.entities.begin(),visible.entities.end(),[](const auto& e){return e.construction || e.kind==modeling::EntityKind::Point;}),visible.entities.end());
            visible.relations.clear();visible.dimensions.clear();visible.polygons.clear();
            const auto shape = modeling::sketchShape(visible);
            if (!shape.IsNull()) {
                m_sketchPresentation = new AIS_Shape(shape);
                m_sketchPresentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
                m_sketchPresentation->SetColor(m_sketchFullyDefined?Quantity_Color(Quantity_NOC_BLACK):Quantity_Color(0.10, 0.22, 0.60, Quantity_TOC_RGB));
                m_sketchPresentation->SetWidth(2.0 * devicePixelRatioF());
                m_sketchPresentation->Attributes()->SetDeviationCoefficient(1e-5);
                m_sketchPresentation->Attributes()->SetDeviationAngle(0.015);
                m_context->Display(m_sketchPresentation, AIS_WireFrame, -1, Standard_False);
                m_context->SetDisplayPriority(m_sketchPresentation, Graphic3d_DisplayPriority_Topmost);
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
    int x, y; m_view->Convert(world.X(), world.Y(), world.Z(), x, y);
    return {x / devicePixelRatioF(), y / devicePixelRatioF()};
}

std::optional<CadViewport::SnapHit> CadViewport::pointAt(const QPointF& position,bool includeMidpoints) const
{
    if (!isReady() || !m_sketch) return {};
    std::vector<modeling::ProjectedSketchPoint> projected;
    const double dpr = devicePixelRatioF();
    for (const auto& entry : modeling::sketchPoints(*m_sketch)) {
        const auto pixel = screenPoint(entry.second);
        projected.push_back({entry.first, entry.second, pixel.x()*dpr, pixel.y()*dpr});
    }
    if(includeMidpoints)for(const auto& e:m_sketch->entities)if(e.kind==modeling::EntityKind::Line){
        const modeling::SketchPointRef ref{e.id,modeling::PointRole::Midpoint};const auto point=modeling::sketchPointValue(*m_sketch,ref);const auto pixel=screenPoint(point);
        projected.push_back({ref,point,pixel.x()*dpr,pixel.y()*dpr});
    }
    // Logical pixels become native pixels exactly once, like mouse input.
    const auto hit = modeling::nearestSketchPoint(projected, position.x()*dpr, position.y()*dpr, m_pickingTolerance*dpr);
    if (hit) return SnapHit{hit->reference, hit->model};
    return {};
}

std::optional<std::string> CadViewport::entityAt(const QPointF& position) const
{
    if (!m_sketch || !isReady()) return {};
    const auto distanceToSegment = [&](modeling::Point2 a, modeling::Point2 b) {
        const auto first = screenPoint(a), second = screenPoint(b);
        const auto d = second-first; const auto offset = position-first;
        const double length2 = QPointF::dotProduct(d,d);
        const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(offset,d)/length2,0.0,1.0) : 0.0;
        const auto nearest = first+d*t;
        return std::hypot(nearest.x()-position.x(),nearest.y()-position.y());
    };
    double best = m_pickingTolerance; std::optional<std::string> result;
    for (const auto& entity : m_sketch->entities) {
        double distance = 1e100;
        if (entity.kind == modeling::EntityKind::Line) distance = distanceToSegment(entity.first,entity.second);
        else if(entity.kind==modeling::EntityKind::Point)distance=QLineF(position,screenPoint(entity.first)).length();
        else if (entity.kind == modeling::EntityKind::Rectangle) {
            const std::array<modeling::Point2,4> corners{entity.first,{entity.second.x,entity.first.y},entity.second,{entity.first.x,entity.second.y}};
            for(int n=0;n<4;++n) distance=std::min(distance,distanceToSegment(corners[n],corners[(n+1)%4]));
        } else {
            const auto center=screenPoint(entity.first);
            const auto edge=screenPoint({entity.first.x+entity.radius,entity.first.y});
            const double radius=std::hypot(edge.x()-center.x(),edge.y()-center.y());
            distance=std::abs(std::hypot(position.x()-center.x(),position.y()-center.y())-radius);
        }
        if(distance<best) { best=distance; result=entity.id; }
    }
    return result;
}

std::optional<CadViewport::SnapHit> CadViewport::selectionPointAt(const QPointF& position) const
{
    auto hit=pointAt(position);
    if(hit && hit->reference.role==modeling::PointRole::Origin){
        for(const auto& e:m_sketch->entities)if(e.kind==modeling::EntityKind::Point && QLineF(position,screenPoint(e.first)).length()<=QLineF(position,screenPoint(hit->point)).length()+0.1)
            return SnapHit{{e.id,modeling::PointRole::First},e.first};
    }
    // A small circle's center tolerance must not swallow its circumference.
    // Exact center picks and the existing drawing/snapping policy are retained.
    if(hit && hit->reference.role==modeling::PointRole::Center) {
        const auto found=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==hit->reference.entityId;});
        if(found!=m_sketch->entities.end() && found->kind==modeling::EntityKind::Circle) {
            const auto center=screenPoint(found->first),edge=screenPoint({found->first.x+found->radius,found->first.y});
            const double centerDistance=QLineF(position,center).length(),radius=QLineF(center,edge).length();
            if(std::abs(centerDistance-radius)<centerDistance)return {};
        }
    }
    return hit;
}

modeling::Point2 CadViewport::drawingPoint(const QPointF& position, std::optional<modeling::SketchPointRef>& reference)
{
    reference.reset();
    const auto previous=m_inference;
    m_inference={};
    const bool midpointTool=m_sketchTool==modeling::EntityKind::Line || m_sketchTool==modeling::EntityKind::Point;
    auto hit=pointAt(position,midpointTool);
    if(previous.target && !(hit && hit->reference.role==modeling::PointRole::Origin)) {
        const auto value=modeling::sketchPointValue(*m_sketch,*previous.target);
        if(QLineF(position,screenPoint(value)).length()<=12.0) hit=SnapHit{*previous.target,value};
    }
    if(hit && !(m_firstPoint && m_sketchTool==modeling::EntityKind::Circle)) {
        reference=hit->reference; m_inference.target=reference; m_inference.symbolPosition=hit->point;
        m_hoveredPoint=reference; return hit->point;
    }
    auto point=sketchPosition(position).value_or(modeling::Point2{});
    if(m_firstPoint && m_sketchTool==modeling::EntityKind::Line) {
        const auto firstPixel=screenPoint(*m_firstPoint);
        const auto horizontal=screenPoint({point.x,m_firstPoint->y});
        const auto vertical=screenPoint({m_firstPoint->x,point.y});
        const auto inference=modeling::inferLineDirection(*m_firstPoint,point,
            QLineF(position,horizontal).length(),QLineF(position,vertical).length(),QLineF(position,firstPixel).length(),previous.axis);
        point=inference.point;m_inference.axis=inference.relation;
        const double span=std::max(1.0,m_view->Camera()->Scale())*std::max(1.0,m_view->Camera()->Aspect())*2;
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
    for(auto& point:m_pointPresentations) m_context->Remove(point,Standard_False);
    m_pointPresentations.clear();
    if(!m_selectedEntityPresentation.IsNull()) {
        m_context->Remove(m_selectedEntityPresentation,Standard_False); m_selectedEntityPresentation.Nullify();
    }
    if(!m_hoveredEntityPresentation.IsNull()) {
        m_context->Remove(m_hoveredEntityPresentation,Standard_False);m_hoveredEntityPresentation.Nullify();
    }
    if(m_sketchEditing && m_sketch) {
        if(!m_hoveredEntity.empty()) {
            auto only=*m_sketch;only.entities.clear();only.relations.clear();only.dimensions.clear();only.polygons.clear();
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
                m_context->Display(m_hoveredEntityPresentation,AIS_WireFrame,-1,Standard_False);
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
            Quantity_Color color(hovered ? Quantity_NOC_ORANGE : origin ? Quantity_NOC_RED3 : m_sketchFullyDefined ? Quantity_NOC_BLACK : Quantity_NOC_BLUE);
            if(selected && !origin)color=Quantity_Color(0.25,0.72,1.0,Quantity_TOC_RGB);
            marker->SetZLayer(Graphic3d_ZLayerId_Topmost);
            marker->Attributes()->SetPointAspect(filledPoint(color,(hovered ? 9.0 : selected ? 9.0 : origin ? 4.0 : 7.0)*devicePixelRatioF()));
            marker->SetInfiniteState(Standard_True);
            m_context->Display(marker,0,-1,Standard_False);
            m_context->SetDisplayPriority(marker,Graphic3d_DisplayPriority_Topmost);
            m_pointPresentations.push_back(marker);
        }
        if(m_hoveredPoint && m_hoveredPoint->role==modeling::PointRole::Midpoint && (m_pointDrag || m_sketchTool==modeling::EntityKind::Line || m_sketchTool==modeling::EntityKind::Point)){
            Handle(AIS_Point) marker=new AIS_Point(new Geom_CartesianPoint(modeling::sketchPoint(*m_sketch,modeling::sketchPointValue(*m_sketch,*m_hoveredPoint))));
            marker->Attributes()->SetPointAspect(filledPoint(Quantity_NOC_ORANGE,9*devicePixelRatioF()));marker->SetZLayer(Graphic3d_ZLayerId_Topmost);marker->SetInfiniteState(Standard_True);
            m_context->Display(marker,0,-1,Standard_False);m_context->SetDisplayPriority(marker,Graphic3d_DisplayPriority_Topmost);m_pointPresentations.push_back(marker);
        }
        modeling::Sketch only;only.plane=m_sketch->plane;only.attachment=m_sketch->attachment;
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
            m_context->Display(m_selectedEntityPresentation,AIS_WireFrame,-1,Standard_False);
        }
    }
    update();
}

void CadViewport::clearInteractionPresentation()
{
    if(isReady() && !m_interactionLabel.IsNull()) m_context->Remove(m_interactionLabel,Standard_False);
    m_interactionLabel.Nullify();
    if(isReady()) for(auto& guide:m_inferenceGuides) m_context->Remove(guide,Standard_False);
    m_inferenceGuides.clear();update();
}

void CadViewport::resetSketchInference()
{
    m_inference={}; clearInteractionPresentation();
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
    } else if(m_sketchEditing && m_sketchTool && m_firstPoint) {
        for(const auto& line:m_inference.guides) {
            Handle(AIS_Shape) guide=new AIS_Shape(BRepBuilderAPI_MakeEdge(modeling::sketchPoint(*m_sketch,line.first),modeling::sketchPoint(*m_sketch,line.second)).Shape());
            const Quantity_Color color(0.65,0.48,0.06,Quantity_TOC_RGB);
            const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(color,Aspect_TOL_DASH,devicePixelRatioF());
            guide->Attributes()->SetWireAspect(aspect);guide->Attributes()->SetLineAspect(aspect);
            guide->SetZLayer(Graphic3d_ZLayerId_Topmost);guide->SetInfiniteState(Standard_True);
            m_context->Display(guide,AIS_WireFrame,-1,Standard_False);m_inferenceGuides.push_back(guide);
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
    m_context->Display(m_interactionLabel,0,-1,Standard_False); update();
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
        const bool changed=m_sketch->relations.size()!=drag.original.relations.size() || !std::equal(m_sketch->entities.begin(),m_sketch->entities.end(),drag.original.entities.begin(),[](const auto& a,const auto& b){return std::hypot(a.first.x-b.first.x,a.first.y-b.first.y)<1e-9 && std::hypot(a.second.x-b.second.x,a.second.y-b.second.y)<1e-9 && std::abs(a.radius-b.radius)<1e-9;});
        if(changed)emit sketchChanged(*m_sketch);
    }
    else { m_sketch=drag.original; if(drag.moved && !drag.error.isEmpty()) emit sketchInteractionError(drag.error); }
    updateSketchPresentation();
}

void CadViewport::commitDrawing(modeling::Point2 point, const std::optional<modeling::SketchPointRef>& reference)
{
    if(!m_sketch || !m_firstPoint || !m_sketchTool) return;
    const auto first=*m_firstPoint;
    if(samePoint(first,point)) return;
    auto candidate=*m_sketch;
    const auto relation=[&](modeling::RelationKind kind,modeling::SketchPointRef a,modeling::SketchPointRef b) {
        candidate.relations.push_back({sketchId(),kind,a,b});
    };
    const auto attach=[&](modeling::SketchPointRef a,const std::optional<modeling::SketchPointRef>& b) {
        if(b){const bool midpoint=b->role==modeling::PointRole::Midpoint;relation(midpoint?modeling::RelationKind::Midpoint:modeling::RelationKind::Coincident,a,midpoint?modeling::SketchPointRef{b->entityId,modeling::PointRole::First}:*b);}
    };
    modeling::SketchEntity entity; entity.id=sketchId(); entity.kind=*m_sketchTool;
    entity.first=first; entity.second=point;entity.construction=m_constructionTool; entity.radius=std::hypot(point.x-first.x,point.y-first.y);
    if(entity.kind==modeling::EntityKind::Polygon) {
        try {
            candidate=modeling::addRegularPolygon(candidate,entity.id,{first,entity.radius,std::atan2(point.y-first.y,point.x-first.x),6});
            const auto& group=candidate.polygons.back();
            attach({group.centerId,modeling::PointRole::First},m_firstReference);
            attach({group.edges.front(),modeling::PointRole::First},reference);
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
        attach({ids[0],modeling::PointRole::First},m_firstReference);
        attach({ids[1],modeling::PointRole::Second},reference);
    } else if(entity.kind==modeling::EntityKind::Line) {
        for(const auto& old:candidate.entities) if(old.kind==modeling::EntityKind::Line && old.construction==entity.construction &&
            ((samePoint(old.first,first)&&samePoint(old.second,point)) ||
             (samePoint(old.first,point)&&samePoint(old.second,first)))) return;
        candidate.entities.push_back(entity);
        attach({entity.id,modeling::PointRole::First},m_firstReference);
        attach({entity.id,modeling::PointRole::Second},reference);
        if(first.y==point.y) relation(modeling::RelationKind::Horizontal,{entity.id,modeling::PointRole::First},{entity.id,modeling::PointRole::Second});
        if(first.x==point.x) relation(modeling::RelationKind::Vertical,{entity.id,modeling::PointRole::First},{entity.id,modeling::PointRole::Second});
    } else {
        for(const auto& old:candidate.entities) if(old.kind==modeling::EntityKind::Circle && samePoint(old.first,first) && std::abs(old.radius-entity.radius)<1e-7) return;
        candidate.entities.push_back(entity);
        attach({entity.id,modeling::PointRole::Center},m_firstReference);
    }
    try {
        modeling::validateSketchRelations(candidate);
        modeling::sketchShape(candidate); // Validate sizes, limits and kernel geometry before replacing the draft.
    } catch (const std::exception& failure) {
        emit sketchInteractionError(QString::fromUtf8(failure.what())); return;
    } catch (const Standard_Failure& failure) {
        emit sketchInteractionError(QString::fromUtf8(failure.GetMessageString())); return;
    }
    m_sketch=candidate;
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
    m_view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const auto axes = modeling::sketchAxes(*m_sketch);
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
    m_deleteSequence.invalidate();
    if(m_pointDrag || m_dimensionDrag) {event->accept();return;}
    if (!isReady() || m_navigation != Navigation::None) {
        return;
    }
    if(event->button()==Qt::LeftButton && m_sketchEditing && !event->modifiers().testFlag(Qt::ShiftModifier)){
        if(const auto id=relationAt(event->position())){setFocus(Qt::MouseFocusReason);selectSketchRelation(*id);event->accept();return;}
        m_selectedRelation.clear();
    }
    if(event->button()==Qt::LeftButton && m_sketchEditing && event->modifiers().testFlag(Qt::ShiftModifier)){
        if(m_constraintPicking)emit constraintPickingCanceled();
        if(m_sketchTool)selectSketchTool();
        setFocus(Qt::MouseFocusReason);m_shiftSelectionChanged=true;m_selectionCursor=event->position().toPoint();
        const auto point=selectionPointAt(event->position());const auto body=point?std::optional<std::string>(point->reference.entityId):entityAt(event->position());
        m_selectedDimensions.clear();m_selectedPoint.reset();
        if(body && !body->empty()){
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
    if(event->button()==Qt::LeftButton && m_sketchEditing && !m_sketchTool && !selectionPointAt(event->position())
        && (!m_constraintPicking || m_smartDimensionPicking) && !m_dimensionPreview) {
        if(const auto id=dimensionAt(event->position())) {
            setFocus(Qt::MouseFocusReason);
            const bool toggle=event->modifiers().testFlag(Qt::ControlModifier);selectDimension(*id,toggle);
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
    if(event->button()==Qt::LeftButton && m_constraintPicking && m_sketchEditing) {
        setFocus(Qt::MouseFocusReason);const auto hit=selectionPointAt(event->position());
        const auto body=hit?std::optional<std::string>(hit->reference.entityId):entityAt(event->position());
        if(m_smartDimensionPicking) {
            if(hit)emit dimensionReferencePicked(QString::fromStdString(hit->reference.entityId),int(hit->reference.role));
            else if(body && !body->empty())emit dimensionReferencePicked(QString::fromStdString(*body),-1);
            else {const auto position=sketchPosition(event->position());if(position)emit dimensionPlacementRequested(*position);}
            event->accept();return;
        }
        if(body && !body->empty()){m_selectedEntity=*body;m_selectedEntities={*body};m_selectedDimensions.clear();emit sketchEntitySelected(QString::fromStdString(*body));}
        if(hit){m_selectedPoint=hit->reference;emit sketchPointSelected(QString::fromStdString(hit->reference.entityId),int(hit->reference.role));}
        else if(!body){const auto position=sketchPosition(event->position());if(position)emit dimensionPlacementRequested(*position);}
        updatePointPresentation();event->accept();return;
    }
    if (event->button() == Qt::LeftButton && m_sketchEditing) {
        setFocus(Qt::MouseFocusReason);
        std::optional<modeling::SketchPointRef> reference;
        const auto raw = sketchPosition(event->position());
        if(m_sketchTool==modeling::EntityKind::Point && raw){placeSketchPoint(event->position());event->accept();return;}
        if (m_sketchTool && raw) {
            const auto point = drawingPoint(event->position(), reference);
            if (!m_firstPoint) { m_firstPoint = point; m_firstReference = reference; }
            else { commitDrawing(point, reference); }
        } else {
            const auto hit = selectionPointAt(event->position());
            m_selectedPoint = hit ? std::optional<modeling::SketchPointRef>(hit->reference) : std::nullopt;
            const auto body = hit ? std::optional<std::string>(hit->reference.entityId) : entityAt(event->position());
            const bool toggle=event->modifiers().testFlag(Qt::ControlModifier);
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
            if(dragRef && !toggle && dragRef->role!=modeling::PointRole::Origin) {
                const auto entity=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==dragRef->entityId;});
                if(entity!=m_sketch->entities.end() && (entity->kind==modeling::EntityKind::Line || entity->kind==modeling::EntityKind::Circle || entity->kind==modeling::EntityKind::Point)) {
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
    if (event->button() == Qt::LeftButton) {
        m_planeClickCandidate = !m_sketchEditing && !m_regionSelecting;
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
    resetSketchInference();
    updateSketchPresentation();
    m_navigationButton = event->button();
    m_previousPosition = nativePosition(event->position());
    setCursor(m_navigation == Navigation::Pan ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
    event->accept();
}

void CadViewport::mouseMoveEvent(QMouseEvent* event)
{
    if(m_shiftSelectionChanged)m_selectionCursor=event->position().toPoint();
    if(m_dimensionDrag){updateDimensionDrag(event->position());event->accept();return;}
    if(m_smartDimensionPicking && m_navigation==Navigation::None){
        updateSketchHover(event->position());
        const auto point=sketchPosition(event->position());if(point)emit dimensionPointerMoved(*point);event->accept();return;
    }
    if(m_pointDrag) { updatePointDrag(event->position()); event->accept(); return; }
    if(!m_sketchEditing && !m_regionSelecting && m_navigation==Navigation::None) {
        const int face=faceAt(event->position());
        const auto hit = face ? std::optional<modeling::ReferencePlaneHit>{} : planeAt(event->position());
        const std::optional<modeling::SketchPlane> hovered = hit ? std::optional<modeling::SketchPlane>(hit->plane) : std::nullopt;
        if (hovered != m_hoveredPlane) {
            m_hoveredPlane = hovered;
            updatePlaneStyles();
        }
    }
    if(m_regionClickCandidate && (event->position()-m_regionPressPosition).manhattanLength()>QApplication::startDragDistance())
        m_regionClickCandidate=false;
    if (m_planeClickCandidate
        && (event->position() - m_planePressPosition).manhattanLength() > QApplication::startDragDistance()) {
        m_planeClickCandidate = false;
    }
    if (isReady() && m_sketchEditing && m_navigation == Navigation::None) {
        updateSketchHover(event->position());
        if (m_sketchTool && m_firstPoint) {
            std::optional<modeling::SketchPointRef> reference;
            const auto point = drawingPoint(event->position(), reference);
            modeling::SketchEntity preview;
            preview.kind = *m_sketchTool;
            preview.first = *m_firstPoint; preview.second = point;
            preview.radius = std::hypot(point.x - m_firstPoint->x, point.y - m_firstPoint->y);preview.construction=m_constructionTool;m_drawingPreview=preview;
            updateSketchPresentation(preview);
            updateInteractionPresentation();
        } else { updatePointPresentation(); }
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
    updateRelationSymbols();update();
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
    if (event->button() == m_navigationButton) {
        const bool regionClick=m_regionClickCandidate && event->button()==Qt::LeftButton
            && (event->position()-m_regionPressPosition).manhattanLength()<=QApplication::startDragDistance();
        m_regionClickCandidate=false;
        const bool click = m_planeClickCandidate && event->button() == Qt::LeftButton
            && (event->position() - m_planePressPosition).manhattanLength() <= QApplication::startDragDistance();
        m_navigation = Navigation::None;
        m_navigationButton = Qt::NoButton;
        m_planeClickCandidate = false;
        unsetCursor();
        if(regionClick && m_regionSelecting) {
            const auto point=sketchPosition(event->position());if(point) emit sketchRegionClicked(*point);
        } else if (click) {
            const int face=faceAt(event->position());
            if(face) {m_context->SelectDetected();emit solidFaceSelected(face);update();event->accept();return;}
            m_context->ClearSelected(Standard_False);
            emit solidFaceSelected(0);
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
    if(event->type()==QEvent::FocusOut)m_deleteSequence.invalidate();
    if(event->type()==QEvent::Leave){m_hoveredRelation.clear();m_deleteSequence.invalidate();m_hoveredEntity.clear();m_hoveredDimension.clear();updatePointPresentation();updateConstraintPresentation();}
    if(event->type()==QEvent::Leave && !m_pointDrag) resetSketchInference();
    if (event->type() == QEvent::Leave && m_hoveredPoint) { m_hoveredPoint.reset(); updatePointPresentation(); }
    if (event->type() == QEvent::Leave && m_hoveredPlane) {
        m_hoveredPlane.reset();
        updatePlaneStyles();
    }
    if (event->type() == QEvent::DevicePixelRatioChange || event->type() == QEvent::ScreenChangeInternal) {
        m_renderSize = {};
        update();
    }
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
        m_deleteSequence.invalidate();cancelPointDrag();cancelDimensionDrag();
        m_navigation = Navigation::None;
        m_navigationButton = Qt::NoButton;
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
    const double factor = std::pow(1.15, std::clamp(steps, -10.0, 10.0));
    m_view->SetZoom(factor);
    updateConstraintPresentation();
    update();
    event->accept();
}

void CadViewport::keyPressEvent(QKeyEvent* event)
{
    if(event->key()==Qt::Key_Delete && !m_selectedRelation.empty()){emit sketchRelationDeleteRequested(QString::fromStdString(m_selectedRelation));event->accept();return;}
    const bool selected=!m_selectedEntities.empty() || !m_selectedDimensions.empty();
    const bool deletionMode=m_sketchEditing && !m_sketchTool && !m_pointDrag && !m_dimensionDrag
        && (!m_constraintPicking || m_smartDimensionPicking) && !m_dimensionPreview;
    if(deletionMode && event->key()==Qt::Key_Delete && event->modifiers()==Qt::NoModifier){
        m_deleteSequence.invalidate();if(selected)emit sketchDeleteRequested();event->accept();return;
    }
    if(deletionMode && selected && (!m_smartDimensionPicking || !m_selectedDimensions.empty())
        && event->key()==Qt::Key_D && event->modifiers()==Qt::NoModifier){
        if(event->isAutoRepeat()){m_deleteSequence.invalidate();event->accept();return;}
        if(m_deleteSequence.isValid() && m_deleteSequence.elapsed()<=750){
            m_deleteSequence.invalidate();emit sketchDeleteRequested();
        }else m_deleteSequence.start();
        event->accept();return;
    }
    m_deleteSequence.invalidate();
    if(event->key()==Qt::Key_Escape && m_dimensionDrag){cancelDimensionDrag();event->accept();return;}
    if(m_smartDimensionPicking && (event->key()==Qt::Key_R || event->key()==Qt::Key_D)){emit dimensionVariantRequested(event->key()==Qt::Key_R);event->accept();return;}
    if(event->key()==Qt::Key_Escape && m_constraintPicking){emit constraintPickingCanceled();event->accept();return;}
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
    if(event->key()==Qt::Key_Shift && !event->isAutoRepeat() && m_shiftSelectionChanged){
        m_shiftSelectionChanged=false;
        if(m_sketchEditing && !m_selectedEntities.empty())emit relationsContextRequested(m_selectionCursor);
        event->accept();return;
    }
    QWidget::keyReleaseEvent(event);
}

void CadViewport::fitAll()
{
    if (isReady()) {
        synchronizeSurface();
        m_view->FitAll(0.1, Standard_False);
        m_view->ZFitAll();
        updateConstraintPresentation();
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
