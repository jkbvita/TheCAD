#pragma once

#include "modeling/Sketch.h"
#include "modeling/SketchModifiers.h"
#include "viewport/GraphicsScene.h"
#include "viewport/CameraNavigation.h"
#include "viewport/DimensionPresenter.h"
#include "viewport/SketchPicker.h"
#include "application/SketchToolController.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/FeaturePreview.h"
#include "modeling/ReferencePlanes.h"
#include "modeling/ReferenceGeometry.h"
#include "modeling/Selection.h"
#include <QPointer>
#include <array>
#include <AIS_TextLabel.hxx>
#include <AIS_Point.hxx>
#include <AIS_Trihedron.hxx>
#include <optional>
#include <QMetaType>
#include <QPoint>
#include <QElapsedTimer>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <set>
class QMenu;
class QContextMenuEvent;

namespace thecad::modeling { struct Measurement; }

namespace thecad::ui {
struct DatumVisual {std::string id,name;modeling::DatumGeometry geometry;bool visible=true;};
struct ReferencePickItem {modeling::GeometryReference reference;modeling::DatumGeometry geometry;TopoDS_Shape shape;};

class CadViewportTest;

class CadViewport final : public QWidget {
    Q_OBJECT

public:
    explicit CadViewport(const TopoDS_Shape& shape, QWidget* parent = nullptr);
    ~CadViewport() override;

    bool isReady() const;
    QString initializationError() const;
    bool setShape(const TopoDS_Shape& shape);
    void releaseGraphics();
    void beginPlaneSelection();
    void cancelPlaneSelection();
    void showReferencePlanes(bool visible);
    bool referencePlanesVisible() const { return m_planesVisible; }
    void setSelectedReferencePlane(std::optional<modeling::SketchPlane> plane);
    void setSelectedSolidFace(int index);
    int selectedSolidFace() const {return m_selectedSolidFace;}
    void beginSketch(const modeling::Sketch& sketch);
    void endSketch();
    void setSketchTool(modeling::EntityKind kind,bool construction=false);
    bool constructionTool() const {return m_tools.construction();}
    void selectSketchTool();
    bool finishSketchMultiSelection();
    void cancelSketchMultiSelection();
    void selectSketchEntity(const QString& id);
    void setSketch(const modeling::Sketch& sketch);
    void setSketchBoundaryReferences(std::vector<modeling::SketchProjection> references);
    void setSketchModifierPicking(bool enabled, bool convert = false, bool lineEdit = false) {
        m_sketchModifierPicking = enabled;
        m_sketchConversionPicking = enabled && convert;
        m_sketchLineEditPicking = enabled && lineEdit;
    }
    void setSketchModifierPreview(const modeling::Sketch* candidate,
                                 const std::optional<modeling::SketchEntity>& removed = {},
                                 const std::optional<modeling::Point2>& split = {});
    void beginContourSelection(const modeling::Sketch& sketch);
    void endContourSelection();
    void setSelectedContourFaces(const std::vector<TopoDS_Face>& faces);
    void previewShape(const TopoDS_Shape& shape,const modeling::MaterialChanges& changes={});
    void clearPreview();
    void setConstraintPicking(bool enabled);
    void setSmartDimensionPicking(bool enabled);
    void setDimensionPreview(const std::optional<modeling::SketchDimension>& dimension);
    void setDimensionSelection(const std::vector<std::string>& entities,const std::vector<modeling::SketchPointRef>& points);
    QPoint dimensionScreenPoint(modeling::Point2 point) const;
    std::string selectedSketchEntity() const {return m_selectedEntity;}
    const std::vector<std::string>& selectedSketchEntities() const {return m_selectedEntities;}
    const std::vector<std::string>& selectedSketchDimensions() const {return m_selectedDimensions;}
    const TopoDS_Shape& displayedShape() const {return m_shape;}
    std::optional<modeling::SketchPointRef> selectedSketchPoint() const {return m_selectedPoint;}
    void setConstraintErrors(const std::vector<std::string>& ids);
    void showSketchRelations(bool visible);
    void selectSketchRelation(const std::string& id);
    std::string selectedSketchRelation() const {return m_selectedRelation;}
    void setPickingTolerance(double logicalPixels);
    double pickingTolerance() const { return m_pickingTolerance; }
    std::optional<modeling::EntityKind> sketchTool() const { return m_tools.drawing(); }
    bool smartDimensionActive() const { return m_tools.dimensionPicking(); }
    void fitAll();
    void resetView();
    void normalTo();
    void setTreeBodySelected(bool selected);
    void setTreeReferenceSelection(const std::vector<modeling::SelectionRef>&);
    void applyTheme();
    void setDatums(const std::vector<DatumVisual>& datums);
    void setSelectedDatum(const std::string& id);
    std::string datumAt(const QPointF& position,std::optional<modeling::DatumKind> kind={});
    void setReferenceCandidates(const std::vector<ReferencePickItem>& candidates,std::optional<modeling::DatumKind> kind);
    void setFeatureReferenceShapes(const std::vector<TopoDS_Shape>&);
    int referenceCandidateAt(const QPointF& position);
    std::vector<modeling::SelectionRef> selection() const;
    void setMeasurePicking(bool active) { m_measurePicking = active; }
    std::vector<TopoDS_Shape> measurementShapes();
    void showMeasurement(const QString& text = {}, const modeling::Measurement* result = nullptr);
    const modeling::SelectionPolicy& selectionPolicy() const {return m_selectionPolicy;}
    void setSelectionFilters(std::uint32_t filters,bool enabled=true);
    void showSelectOther(const QPoint& position);
    void clearSelection();
    void beginTreeSelection();
    void setSelectionFeatureIds(std::string body,std::string sketch){m_bodyFeatureId=std::move(body);m_sketchFeatureId=std::move(sketch);}

signals:
    void constructionContextRequested(QMenu* menu);
    void selectionChanged();
    void datumSelected(const QString& id);
    void referenceGeometryPicked(int option);
    void relationsContextRequested(QPoint position);
    void sketchRelationSelected(const QString& id);
    void sketchRelationDeleteRequested(const QString& id);
    void sketchSelectionChanged();
    void sketchLineEditPointed(const QString& id, thecad::modeling::Point2 point, bool apply);
    void sketchDeleteRequested();
    void sketchToolChanged();
    void dimensionReferencePicked(const QString& id,int role);
    void dimensionBodyReferencePicked(const thecad::modeling::SketchProjection& projection,int role);
    void dimensionPointerMoved(thecad::modeling::Point2 point);
    void dimensionVariantRequested(bool radius);
    void constraintPickingCanceled();
    void dimensionPlacementRequested(thecad::modeling::Point2 point);
    void dimensionEditRequested(const QString& id);
    void initializationFailed(const QString& message);
    void sketchInteractionError(const QString& message);
    void sketchChanged(const thecad::modeling::Sketch& sketch);
    void sketchEntitySelected(const QString& id);
    void sketchPointSelected(const QString& entityId, int role);
    void sketchEntityCreated(const thecad::modeling::SketchEntity& entity);
    void sketchRegionClicked(thecad::modeling::Point2 point);
    void referencePlaneSelected(thecad::modeling::SketchPlane plane, bool directClick = false);
    void planeSelectionCanceled();
    void solidFaceSelected(int index);

protected:
    QPaintEngine* paintEngine() const override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    friend class CadViewportTest;
    friend class SketchInteractionTest;
    friend class ModelingWorkflowTest;
    friend class GraphicalPlaneWorkflowTest;
    friend class SelectionWorkflowTest;
    friend class ReferenceManagerTest;
    friend class PlaneNavigationTest;
    friend class SolidFeaturesWorkflowTest;
    struct PickCandidate {modeling::SelectionCandidate value;TopoDS_Shape shape;Handle(SelectMgr_EntityOwner) owner;
        std::optional<gp_Pnt> hit;};
    std::vector<PickCandidate> selectionCandidates(const QPointF&,bool includeOccluded=false);
    void selectCandidate(const PickCandidate&,bool toggle=false,bool directClick=false);
    void updateSelectionHover(const QPointF&);
    void previewCandidate(const std::optional<PickCandidate>&);
    void refreshNativeSelection();
    void invalidateBodySelection();
    void prepareBodyPicking();
    std::uint64_t m_pickCacheGeneration=~std::uint64_t(0);
    TopTools_IndexedMapOfShape m_selectionFaces,m_selectionEdges,m_selectionVertices,m_selectionBodies;
    std::set<int> m_seamEdges,m_artificialVertices;
    std::vector<modeling::SelectionCandidate> sketchEntityCandidates(const QPointF&) const;
    modeling::SelectionPolicy m_selectionPolicy;
    modeling::SelectionModel m_selection;
    std::uint64_t m_shapeGeneration=0;
    std::string m_bodyFeatureId,m_sketchFeatureId;
    std::vector<PickCandidate> m_nativeSelections;
    Handle(AIS_Shape) m_selectionHover;
    bool m_measurePicking = false;
    Handle(AIS_Shape) m_measureSpan;
    Handle(AIS_TextLabel) m_measureLabel;
    std::vector<Handle(AIS_Shape)> m_featureReferenceShapes;
    std::vector<Handle(AIS_Shape)> m_selectionPresentations;
    bool m_treeBodySelected=false;
    QPointer<QMenu> m_selectOtherMenu;
    QPointF m_contextPress;
    bool m_contextClick=false;
    bool m_dispatchingSelection=false;
    using Navigation=viewport::CameraNavigation::Mode;
    void beginNavigation(QMouseEvent*);
    void moveNavigation(QMouseEvent*);
    void zoomToward(const QPointF&,double);
    gp_Pnt navigationPivot(const QPointF&);
    viewport::CameraNavigation m_camera;
    void initialize();
    void releaseViewer();
    void synchronizeSurface();
    void orientSketch();
    void updateReferencePlanes();
    void updatePlaneStyles();
    void updateConstraintPresentation(bool layoutRelations=true);
    void updateCameraAnnotations();
    bool m_cameraAnnotationsDirty=false;
    void updateRelationSymbols();
    void updateRelationPositions();
    std::optional<std::string> relationAt(const QPointF& position) const;
    struct RelationHit {std::string id;QRectF bounds;std::string entityId;modeling::Point2 anchor;QPointF offset;};
    std::vector<RelationHit> m_relationHits;
    std::vector<Handle(AIS_Point)> m_relationMarkers;
    bool m_showRelations=true;
    std::string m_hoveredRelation,m_selectedRelation;
    viewport::DimensionPresenter m_dimensions;
    std::optional<modeling::SketchDimension> m_dimensionPreview;
    std::vector<std::string> m_dimensionEntities;
    std::vector<modeling::SketchPointRef> m_dimensionPoints;
    bool m_sketchFullyDefined = false;
    modeling::SketchDefinition m_definition;
    std::vector<std::string> m_constraintErrors;
    void updateSketchAxes();
    void updateGlobalAxes();
    int faceAt(const QPointF& position);
    std::optional<modeling::ReferencePlaneHit> planeAt(const QPointF& position);
    void updateSketchPresentation(const std::optional<modeling::SketchEntity>& preview = {});
    std::optional<modeling::Point2> sketchPosition(const QPointF& position);
    using SnapHit=viewport::SketchPicker::SnapHit;
    viewport::SketchPicker sketchPicker()const;
    std::optional<SnapHit> pointAt(const QPointF& position,bool includeMidpoints=false) const;
    std::optional<SnapHit> selectionPointAt(const QPointF& position) const;
    std::optional<std::string> entityAt(const QPointF& position) const;
    std::optional<std::string> dimensionAt(const QPointF& position) const;
    std::vector<modeling::SelectionCandidate> dimensionCandidates(const QPointF& position) const;
    QString dimensionText(const modeling::SketchDimension& dimension) const;
    void updateSketchHover(const QPointF& position);
    void updateDimensionDrag(const QPointF& position);
    void cancelDimensionDrag();
    struct DimensionDrag {
        modeling::Sketch original;
        std::string id;
        QPointF press;
        modeling::Point2 offset;
        bool moved = false;
    };
    std::optional<DimensionDrag> m_dimensionDrag;
    double m_pickingTolerance = 8.0; // Qt logical pixels, independent of zoom/DPI.
    std::string m_hoveredEntity, m_hoveredDimension;
    Handle(AIS_Shape) m_hoveredEntityPresentation;
    QPointF screenPoint(modeling::Point2 point) const;
    modeling::Point2 drawingPoint(const QPointF& position, std::optional<modeling::SketchPointRef>& reference);
    void updatePointPresentation();
    void cancelPointDrag();
    void updatePointDrag(const QPointF& position);
    void finishPointDrag();
    void updateInteractionPresentation();
    void clearInteractionPresentation();
    void resetSketchInference();
    struct BoundarySnap {
        std::size_t index;
        std::optional<modeling::PointRole> role;
        modeling::Point2 point;
    };
    std::optional<BoundarySnap> boundarySnapAt(const QPointF& position, modeling::Point2 local) const;
    void attachBoundarySnap(modeling::Sketch& sketch, modeling::SketchPointRef point,
                            const BoundarySnap& snap);
    std::vector<modeling::SketchProjection> m_sketchBoundaryReferences;
    std::optional<BoundarySnap> m_boundarySnap, m_firstBoundarySnap;
    bool m_sketchModifierPicking = false, m_sketchConversionPicking = false, m_sketchLineEditPicking = false;
    Handle(AIS_Shape) m_sketchModifierPreview, m_sketchRemovalPreview;
    Handle(AIS_Point) m_sketchSplitPreview;
    void pointAtSketchLineEdit(const QPointF& position, bool apply);
    void commitDrawing(modeling::Point2 point, const std::optional<modeling::SketchPointRef>& reference);
    QPoint nativePosition(const QPointF& position) const;

    TopoDS_Shape m_shape;
    viewport::GraphicsScene m_graphics;
    Handle(AIS_Shape) m_presentation;
    Handle(AIS_Shape) m_sketchPresentation;
    Handle(AIS_Shape) m_constructionPresentation;
    std::vector<Handle(AIS_Shape)> m_sketchBatches;
    int m_selectedSolidFace=0;
    std::optional<modeling::Sketch> m_sketch;
    std::vector<DatumVisual> m_datums;
    std::vector<Handle(AIS_InteractiveObject)> m_datumPresentations,m_referenceCandidatePresentations;
    std::vector<ReferencePickItem> m_referenceCandidates;
    std::optional<modeling::DatumKind> m_referenceCandidateKind;
    std::string m_selectedDatum,m_hoveredDatum;
    Handle(AIS_Shape) m_referenceHover;
    void updateDatumStyles();
    std::vector<std::string> datumIdsAt(const QPointF& position);
    void updateReferenceHover(const QPointF& position);
    application::SketchToolController m_tools;
    std::optional<modeling::SketchEntity> m_drawingPreview;
    void placeSketchPoint(const QPointF& position);
    std::optional<modeling::Point2> m_firstPoint;
    std::optional<modeling::SketchPointRef> m_firstReference;
    std::optional<modeling::SketchPointRef> m_hoveredPoint;
    std::optional<modeling::SketchPointRef> m_selectedPoint;
    std::string m_selectedEntity;
    std::vector<std::string> m_selectedEntities,m_selectedDimensions;
    void selectDimension(const std::string& id,bool toggle);
    QElapsedTimer m_deleteSequence;
    bool m_shiftSelectionChanged=false;
    QPoint m_selectionCursor;
    struct PointDrag {
        modeling::Sketch original;
        modeling::SketchPointRef reference;
        QPointF pressPosition;
        bool moved = false;
        std::optional<modeling::Sketch> candidate;
        std::optional<modeling::SketchPointRef> target;
        QString error;
        std::vector<std::pair<modeling::SketchPointRef,modeling::Point2>> targets;
        modeling::Point2 grabOffset;
        bool circleBody=false;
    };
    std::optional<PointDrag> m_pointDrag;
    Handle(AIS_TextLabel) m_interactionLabel;
    struct SketchInference {
        std::optional<modeling::RelationKind> axis;
        std::optional<modeling::SketchPointRef> target, alignX, alignY;
        std::vector<std::pair<modeling::Point2,modeling::Point2>> guides;
        modeling::Point2 symbolPosition;
    };
    SketchInference m_inference;
    std::vector<Handle(AIS_Shape)> m_inferenceGuides;
    std::vector<Handle(AIS_Point)> m_pointPresentations;
    Handle(AIS_Shape) m_selectedEntityPresentation;
    bool m_sketchEditing = false;
    bool m_regionSelecting = false;
    bool m_regionClickCandidate = false;
    QPointF m_regionPressPosition;
    Handle(AIS_Shape) m_previewPresentation;
    std::vector<Handle(AIS_Shape)> m_materialPreviewPresentations;
    std::vector<Handle(AIS_Shape)> m_selectedContourPresentations;
    std::array<Handle(AIS_Shape), 3> m_referencePlanes;
    std::array<Handle(AIS_TextLabel), 3> m_planeLabels;
    std::array<Handle(AIS_Shape), 3> m_planeCenterlines;
    std::array<Handle(AIS_Trihedron), 2> m_sketchAxes;
    std::array<Handle(AIS_Trihedron), 3> m_globalAxes;
    Handle(AIS_Point) m_sketchOrigin;
    Handle(AIS_Point) m_globalOrigin;
    std::optional<modeling::SketchPlane> m_hoveredPlane;
    std::optional<modeling::SketchPlane> m_selectedPlane;
    bool m_planesVisible = false;
    bool m_selectingPlane = false;
    bool m_planeClickCandidate = false;
    QPointF m_planePressPosition;
    double m_planeHalfExtent = 50.0;
    QString m_error;
    bool m_attemptedInitialization = false;
};

} // namespace thecad::ui

Q_DECLARE_METATYPE(thecad::modeling::SketchEntity)

Q_DECLARE_METATYPE(thecad::modeling::Point2)

Q_DECLARE_METATYPE(thecad::modeling::SketchPlane)

Q_DECLARE_METATYPE(thecad::modeling::Sketch)
