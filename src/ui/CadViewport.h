#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
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
    void fitAll();
    void resetView();

signals:
    void initializationFailed(const QString& message);

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
    enum class Navigation { None, Orbit, Pan };
    void initialize();
    void releaseViewer();
    QPoint nativePosition(const QPointF& position) const;

    TopoDS_Shape m_shape;
    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;
    QString m_error;
    Navigation m_navigation = Navigation::None;
    Qt::MouseButton m_navigationButton = Qt::NoButton;
    QPoint m_previousPosition;
    bool m_attemptedInitialization = false;
};

} // namespace thecad::ui
