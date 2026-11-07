#include "modeling/Box.h"
#include "document/PartDocument.h"
#include "presentation/SelectionAppearance.h"
#include "ui/CadViewport.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <gp_Cone.hxx>
#include <Graphic3d_Camera.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <QMainWindow>
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
    std::unique_ptr<QMainWindow> host;
    std::unique_ptr<CadViewport> viewport;

    void verifyNativeSurface()
    {
        const auto glView = Handle(OpenGl_View)::DownCast(viewport->m_graphics.view()->View());
        QVERIFY(!glView.IsNull());
        const auto& surface = glView->GlWindow();
        const QSize expected(qRound(viewport->width() * viewport->devicePixelRatioF()),
                             qRound(viewport->height() * viewport->devicePixelRatioF()));
        QTRY_COMPARE(surface->Width(), expected.width());
        QTRY_COMPARE(surface->Height(), expected.height());
        QVERIFY(std::abs(viewport->m_graphics.view()->Camera()->Aspect()
                - double(expected.width()) / expected.height()) < 0.01);
    }

    void drag(Qt::MouseButton button, const QPoint& start, const QPoint& end,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
    {
        QTest::mousePress(viewport.get(), button, modifiers, start);
        QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end),
                         Qt::NoButton, button, modifiers);
        QApplication::sendEvent(viewport.get(), &move);
        QTest::mouseRelease(viewport.get(), button, modifiers, end);
        QApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        viewport = std::make_unique<CadViewport>(modeling::makeBox(100, 70, 40));
        host = std::make_unique<QMainWindow>();
        host->setCentralWidget(viewport.get());
        host->resize(1200, 800);
        QSignalSpy failure(viewport.get(), &CadViewport::initializationFailed);
        host->showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(host.get()));
        QTRY_VERIFY(viewport->isReady());
        QVERIFY2(viewport->isReady(), qPrintable(viewport->initializationError()));
        QCOMPARE(failure.count(), 0);
    }

    void startupMaximizeRestoreResize()
    {
        QVERIFY(host->isMaximized());
        QVERIFY(!host->isFullScreen());
        verifyNativeSurface();
        host->showNormal();
        QTRY_VERIFY(!host->isMaximized());
        verifyNativeSurface();
        for (const QSize size : {QSize(1024, 768), QSize(700, 500), QSize(1300, 850)}) {
            host->resize(size);
            QTest::qWait(50);
            verifyNativeSurface();
        }
        host->showMaximized();
        QTest::qWait(50);
        verifyNativeSurface();
        host->showNormal();
        host->resize(1000, 750);
        QTest::qWait(50);
        verifyNativeSurface();
    }

    void replacingShapeUpdatesPresentation()
    {
        QVERIFY(viewport->setShape(modeling::makeBox(32.5, 16.0, 8.25)));
        QVERIFY(viewport->m_presentation->Shape().IsSame(viewport->m_shape));
        const auto previousShape = viewport->m_shape;
        QVERIFY(viewport->setShape(TopoDS_Shape()));
        QVERIFY(viewport->m_shape.IsNull());
        QVERIFY(viewport->setShape(modeling::makeBox(100, 70, 40)));
    }

    void fullRevolveHidesSeamAndShadesTip()
    {
        auto part = document::PartDocument::createEmpty();
        modeling::Sketch sketch;
        sketch.entities = {{modeling::EntityKind::Line, {0, 0}, {-35, 0}, 1, "base"},
                           {modeling::EntityKind::Line, {-35, 0}, {-35, 37}, 1, "axis"},
                           {modeling::EntityKind::Line, {-35, 37}, {0, 0}, 1, "taper"}};
        document::Feature revolve;
        revolve.kind = document::FeatureKind::Revolve;
        revolve.sketchId = part.addSketch(sketch);
        revolve.contour = {"base", "axis", "taper"};
        modeling::GeometryReference axis;
        axis.source = modeling::ReferenceSource::SketchLine;
        axis.featureId = revolve.sketchId;
        axis.entityId = "axis";
        revolve.axis = axis;
        part.addSolidFeature(revolve);
        const auto sample = qEnvironmentVariable("THECAD_TEST_REVOLVE_PART");
        const auto shape = sample.isEmpty() ? part.shape() : document::PartDocument::load(sample).shape();
        TopExp_Explorer solids(shape, TopAbs_SOLID);
        QVERIFY(solids.More());
        solids.Next();
        QVERIFY(!solids.More());
        const auto outline = presentation::selectionOutline(shape);
        TopExp_Explorer rim(outline, TopAbs_EDGE);
        QVERIFY(rim.More());
        QVERIFY(BRep_Tool::IsClosed(TopoDS::Edge(rim.Current())));
        rim.Next();
        QVERIFY(!rim.More());

        QVERIFY(viewport->setShape(shape));
        QVERIFY(viewport->displayedShape().IsSame(shape));
        QVERIFY(!viewport->m_presentation->Attributes()->FaceBoundaryDraw());
        int tipFacets = 0;
        TopoDS_Face coneFace;
        Handle(Poly_Triangulation) coneMesh;
        for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next())
        {
            const auto face = TopoDS::Face(faces.Current());
            const BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Cone)
                continue;
            coneFace = face;
            TopLoc_Location location;
            coneMesh = BRep_Tool::Triangulation(face, location);
            QVERIFY(!coneMesh.IsNull() && coneMesh->HasNormals());
            const auto apex = surface.Cone().Apex().Transformed(location.Transformation().Inverted());
            for (int i = 1; i <= coneMesh->NbTriangles(); ++i)
            {
                int nodes[3];
                coneMesh->Triangle(i).Get(nodes[0], nodes[1], nodes[2]);
                int tip = -1, tips = 0;
                for (int j = 0; j < 3; ++j)
                {
                    if (coneMesh->Node(nodes[j]).SquareDistance(apex) <= Precision::SquareConfusion())
                    {
                        tip = j;
                        ++tips;
                    }
                }
                if (tips != 1)
                    continue;
                const auto expected = gp_Vec(coneMesh->Normal(nodes[(tip + 1) % 3])) +
                                      gp_Vec(coneMesh->Normal(nodes[(tip + 2) % 3]));
                QVERIFY(coneMesh->Normal(nodes[tip]).Angle(gp_Dir(expected)) < 1e-5);
                ++tipFacets;
            }
        }
        QVERIFY(tipFacets > 100);
        const int nodeCount = coneMesh->NbNodes();
        QVERIFY(viewport->setShape(shape));
        TopLoc_Location location;
        QVERIFY(BRep_Tool::Triangulation(coneFace, location) == coneMesh);
        QCOMPARE(coneMesh->NbNodes(), nodeCount);
        const auto output = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if (!sample.isEmpty() && !output.isEmpty())
        {
            viewport->m_graphics.view()->SetProj(1, 0.3, 0.8);
            viewport->fitAll();
            viewport->m_graphics.view()->Redraw();
            QVERIFY(QDir().mkpath(output));
            QVERIFY(viewport->m_graphics.view()->Dump(
                (output + "/revolve-viewport.png").toUtf8().constData()));
        }
        QVERIFY(viewport->setShape(modeling::makeBox(100, 70, 40)));
    }
    void referenceGraphicsHandleLargeValidPart()
    {
        QVERIFY(viewport->setShape(modeling::makeBox(1e9, 10, 20)));
        viewport->showReferencePlanes(true);
        QVERIFY(viewport->m_planeHalfExtent <= 1e9);
        QVERIFY(!viewport->m_referencePlanes[0].IsNull());
        viewport->showReferencePlanes(false);
        QVERIFY(viewport->setShape(modeling::makeBox(100, 70, 40)));
    }

    void sketchMouseInputUsesModelCoordinates()
    {
        QVERIFY(viewport->setShape({}));
        QSignalSpy entities(viewport.get(), &CadViewport::sketchEntityCreated);
        for (const auto plane : {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ}) {
            for (const auto kind : {modeling::EntityKind::Line, modeling::EntityKind::Rectangle, modeling::EntityKind::Circle}) {
                viewport->beginSketch({plane, {}});
                viewport->setSketchTool(kind);
                QApplication::processEvents();
                const auto toPixel = [&](modeling::Point2 point) {
                    const auto world = modeling::sketchPoint(plane, point);
                    int x, y;
                    viewport->m_graphics.view()->Convert(world.X(), world.Y(), world.Z(), x, y);
                    return QPoint(qRound(x / viewport->devicePixelRatioF()), qRound(y / viewport->devicePixelRatioF()));
                };
                entities.clear();
                QTest::mouseClick(viewport.get(), Qt::LeftButton, Qt::NoModifier, toPixel({-20, -15}));
                QTest::mouseClick(viewport.get(), Qt::LeftButton, Qt::NoModifier, toPixel({20, 15}));
                QCOMPARE(entities.count(), 1);
                const auto entity = qvariant_cast<modeling::SketchEntity>(entities.takeFirst().at(0));
                QCOMPARE(entity.kind, kind);
                QVERIFY(std::abs(entity.first.x + 20) < 0.5);
                QVERIFY(std::abs(entity.first.y + 15) < 0.5);
                QVERIFY(std::abs(entity.second.x - 20) < 0.5);
                QVERIFY(std::abs(entity.second.y - 15) < 0.5);
                if (kind == modeling::EntityKind::Circle) {
                    QVERIFY(std::abs(entity.radius - 50) < 0.5);
                }
            }
        }
        viewport->endSketch();
        QVERIFY(viewport->setShape(modeling::makeBox(100, 70, 40)));
    }
    void orbitChangesDirection()
    {
        viewport->resetView();
        const auto before = viewport->m_graphics.view()->Camera()->Direction();
        drag(Qt::MiddleButton, QPoint(350, 300), QPoint(450, 350));
        QVERIFY(before.Angle(viewport->m_graphics.view()->Camera()->Direction()) > 0.01);
        QCOMPARE(viewport->m_camera.mode(), CadViewport::Navigation::None);
    }

    void panMovesCenter_data()
    {
        QTest::addColumn<int>("button");
        QTest::newRow("middle") << int(Qt::MiddleButton);

    }

    void panMovesCenter()
    {
        QFETCH(int, button);
        viewport->resetView();
        const auto before = viewport->m_graphics.view()->Camera()->Center();
        const auto direction = viewport->m_graphics.view()->Camera()->Direction();
        drag(static_cast<Qt::MouseButton>(button), QPoint(350, 300), QPoint(420, 350),Qt::ControlModifier);
        QVERIFY(before.Distance(viewport->m_graphics.view()->Camera()->Center()) > 1.0);
        QVERIFY(direction.Angle(viewport->m_graphics.view()->Camera()->Direction()) < 1e-10);
    }

    void wheelZoomsBothWays()
    {
        viewport->resetView();
        const double before = viewport->m_graphics.view()->Camera()->Scale();
        const QPointF position(400, 300);
        for (const int delta : {120, -120}) {
            QWheelEvent event(position, viewport->mapToGlobal(position.toPoint()), {}, QPoint(0, delta),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(viewport.get(), &event);
            const double scale = viewport->m_graphics.view()->Camera()->Scale();
            if (delta > 0) {
                QVERIFY(scale > before);
            } else {
                QVERIFY(std::abs(scale - before) < before * 1e-10);
            }
        }
    }

    void resizeAndFitRenderSolid()
    {
        host->resize(960, 700);
        QApplication::processEvents();
        viewport->resetView();
        QTest::keyClick(viewport.get(), Qt::Key_F);
        verifyNativeSurface();
        viewport->m_graphics.view()->Redraw();
        const QString directory = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR",
                                                       QDir::tempPath() + "/TheCAD-tests");
        QVERIFY(QDir().mkpath(directory));
        const QString filename = directory + "/viewport.png";
        QVERIFY(viewport->m_graphics.view()->Dump(filename.toUtf8().constData()));
        const QImage image(filename);
        QVERIFY(!image.isNull());
        // A fitted solid occupies the center; it must differ from the background.
        QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2)
                != image.pixelColor(5, image.height() / 2));
    }

    void cleanupTestCase()
    {
        viewport.reset(); // Exercise graphics teardown while QApplication still exists.
        host.reset();
    }
};

} // namespace thecad::ui

QTEST_MAIN(thecad::ui::CadViewportTest)
#include "CadViewportTests.moc"
