#include "ui/MainWindow.h"
#include "ui/CadViewport.h"

#include <QAction>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace thecad::ui {

MainWindow::MainWindow(const TopoDS_Shape& shape, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("TheCAD");
    resize(1200, 800);
    auto* viewport = new CadViewport(shape, this);
    setCentralWidget(viewport);

    auto* toolbar = addToolBar("View");
    toolbar->setMovable(false);
    auto* fit = toolbar->addAction("Fit all");
    fit->setShortcut(QKeySequence(Qt::Key_F));
    connect(fit, &QAction::triggered, viewport, &CadViewport::fitAll);
    auto* reset = toolbar->addAction("Isometric");
    reset->setShortcut(QKeySequence(Qt::Key_Home));
    connect(reset, &QAction::triggered, viewport, &CadViewport::resetView);

    statusBar()->showMessage("Orbit: left drag  |  Pan: middle / right drag  |  Zoom: wheel  |  Fit: F");
    statusBar()->addPermanentWidget(new QLabel("Prototype box: 100 x 70 x 40 mm", this));
    connect(viewport, &CadViewport::initializationFailed, this, [this](const QString& message) {
        statusBar()->showMessage("3D viewport initialization failed");
        // Report after showEvent has returned, rather than nesting events during initialization.
        QTimer::singleShot(0, this, [this, message] {
            QMessageBox::critical(this, "TheCAD viewport error",
                message + "\n\nCheck your graphics driver and OpenGL support.");
        });
    });
}

} // namespace thecad::ui
