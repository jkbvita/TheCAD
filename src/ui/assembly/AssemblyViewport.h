#pragma once
#include "viewport/GraphicsScene.h"
#include "viewport/CameraNavigation.h"
#include <QWidget>
#include <AIS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <map>
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
};
class AssemblyViewport final : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyViewport(QWidget* parent = nullptr);
    bool isReady() const { return m_graphics.ready(); }
    QString initializationError() const { return m_error; }
    void setComponents(const std::vector<AssemblyVisual>& visuals);
    void setSelected(const std::string& id, int face = 0);
    void setReferenceHighlights(const std::vector<AssemblyVisual>& references);
    void setMatePicking(bool active) { m_matePicking = active; }
    void setInserting(bool active) { m_inserting = active; }
    void setRotationMode(bool rotate) { m_rotate = rotate; }
    bool rotationMode() const { return m_rotate; }
    void fitAll();
    void standardView(int index);
    gp_Pnt cursorPoint(const QPointF& position) const;
    QPoint project(const gp_Pnt& point) const;
    Handle(Graphic3d_Camera) camera() const;
    void applyTheme();
signals:
    void picked(const QString& component, int face, Qt::KeyboardModifiers modifiers);
    void dragStarted();
    void dragPlacement(const QString& component, const gp_Trsf& placement);
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

private:
    friend class AssemblyWorkflowTest;
    void initialize();
    QPoint native(const QPointF& point) const;
    std::pair<std::string, int> hit(const QPointF& point);
    TopoDS_Shape selectedShape(const std::string& id, int face) const;
    void refreshSelection();
    void hover(const QPointF& point);
    viewport::GraphicsScene m_graphics;
    viewport::CameraNavigation m_navigation;
    std::vector<AssemblyVisual> m_visuals;
    std::map<std::string, Handle(AIS_Shape)> m_presentations;
    Handle(AIS_Shape) m_highlight, m_hover;
    std::vector<Handle(AIS_Shape)> m_references;
    std::vector<AssemblyVisual> m_referenceVisuals;
    std::string m_selected, m_hovered, m_dragId;
    int m_selectedFace = 0, m_hoveredFace = 0;
    QPointF m_press;
    gp_Trsf m_dragPose;
    gp_Pnt m_dragPoint;
    bool m_dragMoved = false, m_matePicking = false, m_inserting = false, m_rotate = false,
         m_attempted = false, m_fitPending = false;
    QString m_error;
};
} // namespace thecad::ui
