#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <Graphic3d_Camera.hxx>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cmath>

namespace thecad::ui {
class PlaneTestWindow : public MainWindow {
public:
    QString path;
    QStringList errors;
protected:
    QString chooseSavePath() override { return path; }
    QString chooseOpenPath() override { return path; }
    UnsavedChoice confirmUnsaved() override { return UnsavedChoice::Discard; }
    void reportError(const QString& message) override { errors.append(message); }
};

class GraphicalPlaneWorkflowTest final : public QObject {
    Q_OBJECT
private:
    QPoint project(CadViewport* viewport, const gp_Pnt& point)
    {
        int x, y;
        viewport->m_view->Convert(point.X(), point.Y(), point.Z(), x, y);
        return {qRound(x / viewport->devicePixelRatioF()), qRound(y / viewport->devicePixelRatioF())};
    }
    std::optional<QPoint> pickPoint(CadViewport* viewport, modeling::SketchPlane plane)
    {
        for (const double u : {-0.85, -0.45, 0.2, 0.65, 0.85}) {
            for (const double v : {-0.85, -0.45, 0.2, 0.65, 0.85}) {
                const auto pixel = project(viewport, modeling::sketchPoint(plane,
                    {u * viewport->m_planeHalfExtent, v * viewport->m_planeHalfExtent}));
                if (!viewport->rect().contains(pixel)) continue;
                const auto hit = viewport->planeAt(pixel);
                if (hit && hit->plane == plane) return pixel;
            }
        }
        return {};
    }
    void command(PlaneTestWindow& window, const char* name)
    {
        auto* action = window.findChild<QAction*>(name);
        QVERIFY(action && action->isEnabled());
        if (qstrcmp(name, "actionCreateSketch") == 0)
            window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(1);
        for (auto* button : window.findChildren<QToolButton*>()) {
            if (button->defaultAction() == action && button->isVisible()) {
                QTest::mouseClick(button, Qt::LeftButton);
                QApplication::processEvents();
                return;
            }
        }
        action->trigger();
        QApplication::processEvents();
    }
    void drag(CadViewport* viewport, QPoint first, QPoint second)
    {
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, first);
        QMouseEvent move(QEvent::MouseMove, second, viewport->mapToGlobal(second), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, second);
        QApplication::processEvents();
    }
private slots:
    void graphicalSelectionHoverOrbitCancelAndPersistence()
    {
        QTemporaryDir temporary;
        PlaneTestWindow window;
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>();
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(viewport->m_planesVisible);
        const bool dirty = window.isWindowModified();
        command(window, "actionCreateSketch");
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(viewport->m_selectingPlane);
        QVERIFY(window.document().features().empty());
        std::optional<QPoint> empty;
        for (const QPoint point : {QPoint(5, 5), QPoint(viewport->width()-5, 5), QPoint(5, viewport->height()-5)}) {
            if (!viewport->planeAt(point)) { empty = point; break; }
        }
        QVERIFY(empty);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *empty);
        QVERIFY(window.document().features().empty());
        const auto direction = viewport->m_view->Camera()->Direction();
        drag(viewport, QPoint(viewport->width()/2, viewport->height()/2),
             QPoint(viewport->width()/2+80, viewport->height()/2+45));
        QVERIFY(direction.Angle(viewport->m_view->Camera()->Direction()) > 0.01);
        QVERIFY(window.document().features().empty());
        QVERIFY(viewport->m_selectingPlane);
        QTest::keyClick(viewport, Qt::Key_Escape);
        QVERIFY(!viewport->m_selectingPlane);
        QVERIFY(window.document().features().empty());
        QCOMPARE(window.isWindowModified(), dirty);
        QVERIFY(!viewport->m_planesVisible);
        for (const auto plane : {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ}) {
            command(window, "actionCreateSketch");
            QVERIFY(viewport->m_selectingPlane);
            command(window, "actionIsometric");
            const auto pixel = pickPoint(viewport, plane);
            QVERIFY(pixel);
            QMouseEvent hover(QEvent::MouseMove, *pixel, viewport->mapToGlobal(*pixel),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &hover);
            QCOMPARE(viewport->m_hoveredPlane, std::optional<modeling::SketchPlane>(plane));
            const auto index = static_cast<std::size_t>(plane);
            QVERIFY(std::abs(viewport->m_referencePlanes[index]->Transparency() - 0.5) < 1e-6);
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(viewport, &leave);
            QVERIFY(!viewport->m_hoveredPlane);
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *pixel);
            QCOMPARE(window.document().features().back().sketch.plane, plane);
            QVERIFY(viewport->m_sketchEditing);
            QVERIFY(!viewport->m_planesVisible);
            QVERIFY(!viewport->m_sketchAxes[0].IsNull());
            QVERIFY(!viewport->m_sketchOrigin.IsNull());
            const auto axes = modeling::sketchAxes(plane);
            QVERIFY(viewport->m_view->Camera()->Direction().Dot(axes.Direction()) < -0.999);
            QVERIFY(viewport->m_view->Camera()->Up().Dot(axes.YDirection()) > 0.999);
            command(window, "actionRectangle");
            const auto first = project(viewport, modeling::sketchPoint(plane, {-12, -10}));
            const auto second = project(viewport, modeling::sketchPoint(plane, {12, 10}));
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, first);
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, second);
            command(window, "actionFinishSketch");
            QVERIFY(!viewport->m_sketchEditing);
            QVERIFY(!viewport->m_planesVisible);
            QCOMPARE(window.document().features().back().sketch.entities.size(), std::size_t(4));
        }
        QCOMPARE(window.document().features().size(), std::size_t(3));
        // Plane-first in the viewport: selected green differs from amber hover.
        viewport->showReferencePlanes(true);
        const auto selectedPixel = pickPoint(viewport, modeling::SketchPlane::XY);
        QVERIFY(selectedPixel);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *selectedPixel);
        QCOMPARE(window.document().features().size(), std::size_t(3));
        QCOMPARE(viewport->m_selectedPlane, std::optional<modeling::SketchPlane>(modeling::SketchPlane::XY));
        QVERIFY(std::abs(viewport->m_referencePlanes[0]->Transparency()-0.4) < 1e-6);
        command(window, "actionCreateSketch");
        QVERIFY(viewport->m_sketchEditing);
        QCOMPARE(window.document().features().size(), std::size_t(4));
        command(window, "actionFinishSketch");
        // Tree plane-first uses the real reference and has no plane-choice dialog.
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        auto* origin = tree->topLevelItem(1);
        tree->setCurrentItem(origin->child(1));
        command(window, "actionCreateSketch");
        QCOMPARE(window.document().features().back().sketch.plane, modeling::SketchPlane::XZ);
        command(window, "actionFinishSketch");
        window.path = temporary.filePath("perpendicular.thepart");
        command(window, "actionSave");
        QVERIFY(!window.isWindowModified());
        QVERIFY(window.close());
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        command(window, "actionNew");
        command(window, "actionOpen");
        QCOMPARE(window.document().features().size(), std::size_t(5));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(window.document().features()[std::size_t(i)].sketch.plane, static_cast<modeling::SketchPlane>(i));
            tree->setCurrentItem(tree->topLevelItem(0)->child(i));
            if (i == 0) {
                const auto rectangle = tree->visualItemRect(tree->topLevelItem(0)->child(i));
                QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rectangle.center());
                QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rectangle.center());
                QApplication::processEvents();
                QVERIFY(viewport->m_sketchEditing);
            } else {
                command(window, "actionEditFeature");
            }
            const auto normal = modeling::sketchAxes(static_cast<modeling::SketchPlane>(i)).Direction();
            QVERIFY(viewport->m_view->Camera()->Direction().Dot(normal) < -0.999);
            QVERIFY(viewport->m_view->Camera()->Up().Dot(modeling::sketchAxes(static_cast<modeling::SketchPlane>(i)).YDirection()) > 0.999);
            command(window, "actionFinishSketch");
        }
        QVERIFY(window.errors.isEmpty());
        const QString artifacts = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            window.document().save(artifacts + "/perpendicular-sketches.thepart");
            viewport->showReferencePlanes(true);
            viewport->resetView();
            viewport->m_view->Redraw();
            QVERIFY(viewport->m_view->Dump((artifacts + "/reference-planes.png").toUtf8().constData()));
        }
        window.setWindowModified(false);
        QVERIFY(window.close());
    }
};
}
QTEST_MAIN(thecad::ui::GraphicalPlaneWorkflowTest)
#include "GraphicalPlaneWorkflowTests.moc"
