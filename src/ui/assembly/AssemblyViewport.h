#pragma once
#include "viewport/GraphicsScene.h"
#include "viewport/CameraNavigation.h"
#include "ui/input/TouchpadNavigation.h"
#include <QWidget>
#include <AIS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Quaternion.hxx>
#include <map>
#include <memory>
#include <optional>
#include <vector>
namespace thecad::ui
{
struct AssemblyVisual
{
    std::string id;
    TopoDS_Shape shape;
    gp_Trsf placement;
    bool visible = true, fixed = false, preview = false;
    bool filled = false; // Filled face selection versus datum outlines.
    bool constrained = false;
};
struct AssemblyHit
{
    std::string first;
    int second = 0;
    TopAbs_ShapeEnum kind = TopAbs_FACE;
    gp_Pnt point;
};
class AssemblyViewport final : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyViewport(QWidget* parent = nullptr);
    ~AssemblyViewport() override;
    bool isReady() const { return m_graphics.ready(); }
    QString initializationError() const { return m_error; }
    void setComponents(const std::vector<AssemblyVisual>& visuals);
    void setSelected(const std::string& id, int face = 0, TopAbs_ShapeEnum kind = TopAbs_FACE);
    void setReferenceHighlights(const std::vector<AssemblyVisual>& references);
    void setMatePicking(bool active) { m_matePicking = active; }
    void setInserting(bool active) { m_inserting = active; }
    void fitAll();
    void standardView(int index);
    gp_Pnt cursorPoint(const QPointF& position) const;
    QPoint project(const gp_Pnt& point) const;
    Handle(Graphic3d_Camera) camera() const;
    void applyTheme();
signals:
    void framePresented();
    void picked(const QString& component, int face, Qt::KeyboardModifiers modifiers);
    void entityPicked(const QString& component, TopAbs_ShapeEnum kind, int index,
                      Qt::KeyboardModifiers modifiers);
    void dragStarted();
    void dragRotationRequested(const QString& component, const gp_Pnt& localPivot,
                               const gp_Quaternion& rotation);
    void dragPointRequested(const QString& component, const gp_Pnt& local, const gp_Pnt& target,
                            const gp_Dir& normal, double tolerance, double rotationDeadband);
    void dragFinished(bool accepted);
    void insertionMoved(const gp_Pnt& point);
    void insertionConfirmed();
    void canceled();
    void initializationFailed(const QString& message);

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool event(QEvent*) override;

private:
    friend class TouchpadNavigationTest;
    friend class AssemblyWorkflowTest;
    struct PickCache;
    void initialize();
    QPoint native(const QPointF& point) const;
    AssemblyHit hit(const QPointF& point);
    gp_Pnt cursorPointAtDepth(const QPointF& point, const gp_Pnt& planePoint) const;
    TopoDS_Shape selectedShape(const std::string& id, int face,
                               TopAbs_ShapeEnum kind = TopAbs_FACE) const;
    void refreshSelection();
    void hover(const QPointF& point);
    void updateComponentDrag(const QPointF& position);
    viewport::GraphicsScene m_graphics;
    viewport::CameraNavigation m_navigation;
    TouchpadNavigation m_touchpad;
    gp_Pnt navigationPivot(const QPointF& position);
    void touchpadZoom(QPointF position, double factor);
    std::vector<AssemblyVisual> m_visuals;
    std::map<std::string, Handle(AIS_Shape)> m_presentations;
    std::map<std::string, std::unique_ptr<PickCache>> m_pickCaches;
    Handle(AIS_Shape) m_highlight, m_hover;
    std::vector<Handle(AIS_Shape)> m_references;
    std::vector<AssemblyVisual> m_referenceVisuals;
    std::string m_selected, m_hovered, m_dragId;
    int m_selectedFace = 0, m_hoveredFace = 0;
    TopAbs_ShapeEnum m_selectedKind = TopAbs_FACE, m_hoveredKind = TopAbs_FACE;
    QPointF m_press;
    gp_Pnt m_dragPoint, m_dragLocalPoint;
    std::optional<gp_Pnt> m_hitPoint;
    QPointF m_dragPrevious;
    double m_dragTolerance = 0.01;
    double m_dragRotationDeadband = 0;
    Qt::MouseButton m_dragButton = Qt::NoButton;
    QPointF m_rotationCenter;
    double m_rotationRadius = 100;
    bool m_dragMoved = false, m_matePicking = false, m_inserting = false,
         m_attempted = false, m_fitPending = false;
    QString m_error;
};
} // namespace thecad::ui
