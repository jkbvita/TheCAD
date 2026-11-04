#pragma once

#include "modeling/Sketch.h"
#include "modeling/ReferencePlanes.h"
#include <array>
#include <AIS_TextLabel.hxx>
#include <AIS_Point.hxx>
#include <AIS_Trihedron.hxx>
#include <optional>
#include <QMetaType>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

namespace thecad::ui {

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
    void setSelectedReferencePlane(std::optional<modeling::SketchPlane> plane);
    void beginSketch(const modeling::Sketch& sketch);
    void endSketch();
    void setSketchTool(modeling::EntityKind kind);
    void selectSketchTool();
    void selectSketchEntity(const QString& id);
    void setSketch(const modeling::Sketch& sketch);
    void beginContourSelection(const modeling::Sketch& sketch);
    void endContourSelection();
    void previewShape(const TopoDS_Shape& shape);
    void clearPreview();
    void fitAll();
    void resetView();

signals:
    void initializationFailed(const QString& message);
    void sketchInteractionError(const QString& message);
    void sketchChanged(const thecad::modeling::Sketch& sketch);
    void sketchEntitySelected(const QString& id);
    void sketchPointSelected(const QString& entityId, int role);
    void sketchEntityCreated(const thecad::modeling::SketchEntity& entity);
    void sketchRegionClicked(thecad::modeling::Point2 point);
    void referencePlaneSelected(thecad::modeling::SketchPlane plane);
    void planeSelectionCanceled();
    void solidFaceSelected(int index);

protected:
    QPaintEngine* paintEngine() const override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    friend class CadViewportTest;
    friend class SketchInteractionTest;
    friend class ModelingWorkflowTest;
    friend class GraphicalPlaneWorkflowTest;
    enum class Navigation { None, Orbit, Pan };
    void initialize();
    void releaseViewer();
    void synchronizeSurface();
    void orientSketch();
    void updateReferencePlanes();
    void updatePlaneStyles();
    void updateSketchAxes();
    void updateGlobalAxes();
    int faceAt(const QPointF& position);
    std::optional<modeling::ReferencePlaneHit> planeAt(const QPointF& position);
    void updateSketchPresentation(const std::optional<modeling::SketchEntity>& preview = {});
    std::optional<modeling::Point2> sketchPosition(const QPointF& position);
    struct SnapHit { modeling::SketchPointRef reference; modeling::Point2 point; };
    std::optional<SnapHit> pointAt(const QPointF& position) const;
    std::optional<std::string> entityAt(const QPointF& position) const;
    QPointF screenPoint(modeling::Point2 point) const;
    modeling::Point2 drawingPoint(const QPointF& position, std::optional<modeling::SketchPointRef>& reference);
    void updatePointPresentation();
    void cancelPointDrag();
    void updatePointDrag(const QPointF& position);
    void finishPointDrag();
    void updateInteractionPresentation();
    void clearInteractionPresentation();
    void resetSketchInference();
    void commitDrawing(modeling::Point2 point, const std::optional<modeling::SketchPointRef>& reference);
    QPoint nativePosition(const QPointF& position) const;

    TopoDS_Shape m_shape;
    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;
    Handle(AIS_Shape) m_presentation;
    Handle(AIS_Shape) m_sketchPresentation;
    std::optional<modeling::Sketch> m_sketch;
    std::optional<modeling::EntityKind> m_sketchTool;
    std::optional<modeling::Point2> m_firstPoint;
    std::optional<modeling::SketchPointRef> m_firstReference;
    std::optional<modeling::SketchPointRef> m_hoveredPoint;
    std::optional<modeling::SketchPointRef> m_selectedPoint;
    std::string m_selectedEntity;
    struct PointDrag {
        modeling::Sketch original;
        modeling::SketchPointRef reference;
        QPointF pressPosition;
        bool moved = false;
        std::optional<modeling::Sketch> candidate;
        std::optional<modeling::SketchPointRef> target;
        QString error;
        std::vector<std::pair<modeling::SketchPointRef,modeling::Point2>> targets;
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
    QSize m_renderSize;
    QString m_error;
    Navigation m_navigation = Navigation::None;
    Qt::MouseButton m_navigationButton = Qt::NoButton;
    QPoint m_previousPosition;
    bool m_attemptedInitialization = false;
};

} // namespace thecad::ui

Q_DECLARE_METATYPE(thecad::modeling::SketchEntity)

Q_DECLARE_METATYPE(thecad::modeling::Point2)

Q_DECLARE_METATYPE(thecad::modeling::SketchPlane)

Q_DECLARE_METATYPE(thecad::modeling::Sketch)
