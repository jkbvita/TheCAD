#include "modeling/Box.h"
#include "ui/CadViewport.h"

#include <Graphic3d_Camera.hxx>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <memory>
#include <cmath>

namespace thecad::ui {

class CadViewportTest final : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<CadViewport> viewport;

    void drag(Qt::MouseButton button, const QPoint& start, const QPoint& end)
    {
        QTest::mousePress(viewport.get(), button, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end),
                         Qt::NoButton, button, Qt::NoModifier);
        QApplication::sendEvent(viewport.get(), &move);
        QTest::mouseRelease(viewport.get(), button, Qt::NoModifier, end);
        QApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        viewport = std::make_unique<CadViewport>(modeling::makeBox(100, 70, 40));
        viewport->resize(800, 600);
        QSignalSpy failure(viewport.get(), &CadViewport::initializationFailed);
        viewport->show();
        QVERIFY(QTest::qWaitForWindowExposed(viewport.get()));
        QVERIFY2(viewport->isReady(), qPrintable(viewport->initializationError()));
        QCOMPARE(failure.count(), 0);
    }

    void orbitChangesDirection()
    {
        viewport->resetView();
        const auto before = viewport->m_view->Camera()->Direction();
        drag(Qt::LeftButton, QPoint(350, 300), QPoint(450, 350));
        QVERIFY(before.Angle(viewport->m_view->Camera()->Direction()) > 0.01);
        QCOMPARE(viewport->m_navigation, CadViewport::Navigation::None);
    }

    void panMovesCenter_data()
    {
        QTest::addColumn<int>("button");
        QTest::newRow("middle") << int(Qt::MiddleButton);
        QTest::newRow("right") << int(Qt::RightButton);
    }

    void panMovesCenter()
    {
        QFETCH(int, button);
        viewport->resetView();
        const auto before = viewport->m_view->Camera()->Center();
        const auto direction = viewport->m_view->Camera()->Direction();
        drag(static_cast<Qt::MouseButton>(button), QPoint(350, 300), QPoint(420, 350));
        QVERIFY(before.Distance(viewport->m_view->Camera()->Center()) > 1.0);
        QVERIFY(direction.Angle(viewport->m_view->Camera()->Direction()) < 1e-10);
    }

    void wheelZoomsBothWays()
    {
        viewport->resetView();
        const double before = viewport->m_view->Camera()->Scale();
        const QPointF position(400, 300);
        for (const int delta : {120, -120}) {
            QWheelEvent event(position, viewport->mapToGlobal(position.toPoint()), {}, QPoint(0, delta),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(viewport.get(), &event);
            const double scale = viewport->m_view->Camera()->Scale();
            if (delta > 0) {
                QVERIFY(scale < before);
            } else {
                QVERIFY(std::abs(scale - before) < before * 1e-10);
            }
        }
    }

    void resizeAndFitRenderSolid()
    {
        viewport->resize(960, 640);
        QApplication::processEvents();
        viewport->resetView();
        QTest::keyClick(viewport.get(), Qt::Key_F);
        viewport->m_view->Redraw();
        const QString directory = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR",
                                                       QDir::tempPath() + "/TheCAD-tests");
        QVERIFY(QDir().mkpath(directory));
        const QString filename = directory + "/viewport.png";
        QVERIFY(viewport->m_view->Dump(filename.toUtf8().constData()));
        const QImage image(filename);
        QVERIFY(!image.isNull());
        // A fitted solid occupies the center; it must differ from the background.
        QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2)
                != image.pixelColor(5, image.height() / 2));
    }

    void cleanupTestCase()
    {
        viewport.reset(); // Exercise graphics teardown while QApplication still exists.
    }
};

} // namespace thecad::ui

QTEST_MAIN(thecad::ui::CadViewportTest)
#include "CadViewportTests.moc"
