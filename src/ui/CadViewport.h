#pragma once

#include "modeling/Sketch.h"
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
    void beginSketch(const modeling::Sketch& sketch);
    void endSketch();
    void setSketchTool(modeling::EntityKind kind);
    void setSketch(const modeling::Sketch& sketch);
    void beginContourSelection(const modeling::Sketch& sketch);
    void endContourSelection();
    void previewShape(const TopoDS_Shape& shape);
    void clearPreview();
    void fitAll();
    void resetView();

signals:
    void initializationFailed(const QString& message);
    void sketchEntityCreated(const thecad::modeling::SketchEntity& entity);
    void sketchRegionClicked(thecad::modeling::Point2 point);

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
    friend class ModelingWorkflowTest;
    enum class Navigation { None, Orbit, Pan };
    void initialize();
    void releaseViewer();
    void synchronizeSurface();
    void orientSketch();
    void updateSketchPresentation(const std::optional<modeling::SketchEntity>& preview = {});
    std::optional<modeling::Point2> sketchPosition(const QPointF& position);
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
    bool m_sketchEditing = false;
    bool m_regionSelecting = false;
    Handle(AIS_Shape) m_previewPresentation;
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
