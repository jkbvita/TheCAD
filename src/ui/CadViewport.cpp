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
    m_interactionLabel.Nullify();
    m_inferenceGuides.clear();
    m_selectedEntityPresentation.Nullify();
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
    cancelPointDrag(); resetSketchInference();
    m_selectingPlane = false;
    showReferencePlanes(false);
    clearPreview();
    m_regionSelecting = false;
    m_sketchEditing = true;
    m_sketchTool.reset();
    m_firstPoint.reset();
    m_firstReference.reset();
    m_hoveredPoint.reset();
    m_selectedPoint.reset();
    m_selectedEntity.clear();
    setSketch(sketch);
    orientSketch();
    updateSketchAxes();
    setFocus(Qt::OtherFocusReason);
}

void CadViewport::endSketch()
{
    cancelPointDrag(); resetSketchInference();
    m_sketchEditing = false;
    updateSketchAxes();
    m_sketchTool.reset();
    m_firstPoint.reset();
    m_firstReference.reset();
    m_hoveredPoint.reset();
    m_selectedPoint.reset();
    m_selectedEntity.clear();
    m_sketch.reset();
    updateSketchPresentation();
    unsetCursor();
    resetView();
}

void CadViewport::beginContourSelection(const modeling::Sketch& sketch)
{
    cancelPointDrag();resetSketchInference();
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
void CadViewport::setSketchTool(modeling::EntityKind kind)
{
    cancelPointDrag(); resetSketchInference();
    m_sketchTool = kind;
    m_firstPoint.reset();
    m_firstReference.reset();
    if (m_sketchEditing) { setCursor(Qt::CrossCursor); }
    updateSketchPresentation();
}

void CadViewport::selectSketchTool()
{
    cancelPointDrag(); resetSketchInference();
    m_sketchTool.reset(); m_firstPoint.reset(); m_firstReference.reset();
    unsetCursor(); updateSketchPresentation();
}

void CadViewport::selectSketchEntity(const QString& id)
{
    m_selectedEntity = id.toStdString();
    m_selectedPoint.reset();
    updatePointPresentation();
}

void CadViewport::setSketch(const modeling::Sketch& sketch)
{
    cancelPointDrag();
    m_sketch = sketch;
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
    if(std::none_of(sketch.entities.begin(),sketch.entities.end(),[&](const auto& entity){return entity.id==m_selectedEntity;}))
        m_selectedEntity.clear();
    updateSketchPresentation();
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
                m_sketchPresentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
                m_sketchPresentation->SetColor(Quantity_Color(0.10, 0.22, 0.60, Quantity_TOC_RGB));
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

std::optional<CadViewport::SnapHit> CadViewport::pointAt(const QPointF& position) const
{
    if (!isReady() || !m_sketch) return {};
    std::vector<modeling::ProjectedSketchPoint> projected;
    const double dpr = devicePixelRatioF();
    for (const auto& entry : modeling::sketchPoints(*m_sketch)) {
        const auto pixel = screenPoint(entry.second);
        projected.push_back({entry.first, entry.second, pixel.x()*dpr, pixel.y()*dpr});
    }
    // Nine Qt logical pixels become native pixels exactly once, like mouse input.
    const auto hit = modeling::nearestSketchPoint(projected, position.x()*dpr, position.y()*dpr, 9.0*dpr);
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
    double best = 7.0; std::optional<std::string> result;
    for (const auto& entity : m_sketch->entities) {
        double distance = 1e100;
        if (entity.kind == modeling::EntityKind::Line) distance = distanceToSegment(entity.first,entity.second);
        else if (entity.kind == modeling::EntityKind::Rectangle) {
            const std::array<modeling::Point2,4> corners{entity.first,{entity.second.x,entity.first.y},entity.second,{entity.first.x,entity.second.y}};
            for(int n=0;n<4;++n) distance=std::min(distance,distanceToSegment(corners[n],corners[(n+1)%4]));
        } else {
            // Body hit testing is visual only; snapping always uses exact model references.
            constexpr int count=96;
            for(int n=0;n<count;++n) {
                const double a=6.283185307179586*n/count,b=6.283185307179586*(n+1)/count;
                distance=std::min(distance,distanceToSegment(
                    {entity.first.x+entity.radius*std::cos(a),entity.first.y+entity.radius*std::sin(a)},
                    {entity.first.x+entity.radius*std::cos(b),entity.first.y+entity.radius*std::sin(b)}));
            }
        }
        if(distance<best) { best=distance; result=entity.id; }
    }
    return result;
}

modeling::Point2 CadViewport::drawingPoint(const QPointF& position, std::optional<modeling::SketchPointRef>& reference)
{
    reference.reset();
    const auto previous=m_inference;
    m_inference={};
    auto hit=pointAt(position);
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
    if(m_sketchEditing && m_sketch) {
        for(const auto& entry:modeling::sketchPoints(*m_sketch)) {
            const bool origin=entry.first.role==modeling::PointRole::Origin;
            const bool hovered=m_hoveredPoint && sameReference(*m_hoveredPoint,entry.first);
            const bool selected=m_selectedPoint && sameReference(*m_selectedPoint,entry.first);
            const bool lineEndpoint=std::any_of(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& entity) {
                return entity.id==entry.first.entityId && entity.kind==modeling::EntityKind::Line;
            });
            if(!origin && !hovered && !selected && !lineEndpoint && entry.first.entityId!=m_selectedEntity) continue;
            Handle(AIS_Point) marker=new AIS_Point(new Geom_CartesianPoint(modeling::sketchPoint(*m_sketch,entry.second)));
            const Quantity_Color color(hovered ? Quantity_NOC_ORANGE : origin ? Quantity_NOC_RED3 : Quantity_NOC_BLUE);
            marker->SetZLayer(Graphic3d_ZLayerId_Topmost);
            marker->Attributes()->SetPointAspect(filledPoint(color,(hovered ? 9.0 : selected ? 9.0 : origin ? 4.0 : 7.0)*devicePixelRatioF()));
            marker->SetInfiniteState(Standard_True);
            m_context->Display(marker,0,-1,Standard_False);
            m_context->SetDisplayPriority(marker,Graphic3d_DisplayPriority_Topmost);
            m_pointPresentations.push_back(marker);
        }
        for(const auto& entity:m_sketch->entities) if(entity.id==m_selectedEntity) {
            modeling::Sketch only; only.plane=m_sketch->plane; only.attachment=m_sketch->attachment; only.entities.push_back(entity);
            m_selectedEntityPresentation=new AIS_Shape(modeling::sketchShape(only));
            m_selectedEntityPresentation->SetZLayer(Graphic3d_ZLayerId_Topmost);
            m_selectedEntityPresentation->SetColor(Quantity_NOC_GREEN4); m_selectedEntityPresentation->SetWidth(2.5 * devicePixelRatioF());
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
        else if(m_pointDrag->target) {text="Coincident";point=modeling::sketchPointValue(*m_sketch,*m_pointDrag->target);}
    } else if(m_sketchEditing && m_sketchTool && m_firstPoint) {
        for(const auto& line:m_inference.guides) {
            Handle(AIS_Shape) guide=new AIS_Shape(BRepBuilderAPI_MakeEdge(modeling::sketchPoint(*m_sketch,line.first),modeling::sketchPoint(*m_sketch,line.second)).Shape());
            const Quantity_Color color(0.65,0.48,0.06,Quantity_TOC_RGB);
            const Handle(Prs3d_LineAspect) aspect=new Prs3d_LineAspect(color,Aspect_TOL_DASH,devicePixelRatioF());
            guide->Attributes()->SetWireAspect(aspect);guide->Attributes()->SetLineAspect(aspect);
            guide->SetZLayer(Graphic3d_ZLayerId_Topmost);guide->SetInfiniteState(Standard_True);
            m_context->Display(guide,AIS_WireFrame,-1,Standard_False);m_inferenceGuides.push_back(guide);
        }
        if(m_inference.target) text="Coincident";
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
    const auto target=modeling::nearestSketchPoint(targets,position.x()*dpr,position.y()*dpr,9.0*dpr);
    drag.target=target ? std::optional<modeling::SketchPointRef>(target->reference) : std::nullopt;
    m_hoveredPoint=drag.target; drag.error.clear();
    try {
        drag.candidate=modeling::moveSketchPoint(drag.original,drag.reference,*raw,drag.target);
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
    if(drag.moved && drag.candidate) { m_sketch=*drag.candidate; emit sketchChanged(*m_sketch); }
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
        if(b) relation(modeling::RelationKind::Coincident,a,*b);
    };
    modeling::SketchEntity entity; entity.id=sketchId(); entity.kind=*m_sketchTool;
    entity.first=first; entity.second=point; entity.radius=std::hypot(point.x-first.x,point.y-first.y);
    if(entity.kind==modeling::EntityKind::Rectangle) {
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
        for(const auto& old:candidate.entities) if(old.kind==modeling::EntityKind::Line &&
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
    if(m_pointDrag) {event->accept();return;}
    if (!isReady() || m_navigation != Navigation::None) {
        return;
    }
    if (event->button() == Qt::LeftButton && m_regionSelecting) {
        m_regionClickCandidate=true;
        m_regionPressPosition=event->position();
    }
    if (event->button() == Qt::LeftButton && m_sketchEditing) {
        setFocus(Qt::MouseFocusReason);
        std::optional<modeling::SketchPointRef> reference;
        const auto raw = sketchPosition(event->position());
        if (m_sketchTool && raw) {
            const auto point = drawingPoint(event->position(), reference);
            if (!m_firstPoint) { m_firstPoint = point; m_firstReference = reference; }
            else { commitDrawing(point, reference); }
        } else {
            const auto hit = pointAt(event->position());
            m_selectedPoint = hit ? std::optional<modeling::SketchPointRef>(hit->reference) : std::nullopt;
            const auto body = hit ? std::optional<std::string>(hit->reference.entityId) : entityAt(event->position());
            m_selectedEntity = body.value_or("");
            emit sketchEntitySelected(QString::fromStdString(m_selectedEntity));
            m_selectedPoint = hit ? std::optional<modeling::SketchPointRef>(hit->reference) : std::nullopt;
            if (hit) { emit sketchPointSelected(QString::fromStdString(hit->reference.entityId), int(hit->reference.role)); }
            if(hit && hit->reference.role!=modeling::PointRole::Origin && hit->reference.role!=modeling::PointRole::Center) {
                const auto entity=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==hit->reference.entityId;});
                if(entity!=m_sketch->entities.end() && entity->kind==modeling::EntityKind::Line) {
                    m_pointDrag=PointDrag{*m_sketch,hit->reference,event->position(),false,*m_sketch,{},{}};
                    std::set<std::pair<std::string,int>> excluded;
                    for(const auto& ref:modeling::coincidentSketchPoints(*m_sketch,hit->reference)) excluded.insert({ref.entityId,int(ref.role)});
                    for(const auto& entry:modeling::sketchPoints(*m_sketch))
                        if(!excluded.count({entry.first.entityId,int(entry.first.role)})) m_pointDrag->targets.push_back(entry);
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
        const auto hit = pointAt(event->position());
        m_hoveredPoint = hit ? std::optional<modeling::SketchPointRef>(hit->reference) : std::nullopt;
        if (m_sketchTool && m_firstPoint) {
            std::optional<modeling::SketchPointRef> reference;
            const auto point = drawingPoint(event->position(), reference);
            modeling::SketchEntity preview;
            preview.kind = *m_sketchTool;
            preview.first = *m_firstPoint; preview.second = point;
            preview.radius = std::hypot(point.x - m_firstPoint->x, point.y - m_firstPoint->y);
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
    update();
    event->accept();
}

void CadViewport::mouseReleaseEvent(QMouseEvent* event)
{
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
        cancelPointDrag();
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
    update();
    event->accept();
}

void CadViewport::keyPressEvent(QKeyEvent* event)
{
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
