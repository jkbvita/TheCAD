#include "ui/CadViewport.h"
#include "modeling/Box.h"
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include "document/PartDocument.h"
#include "modeling/SketchSnapping.h"
#include <BRepCheck_Analyzer.hxx>
#include <QApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <cmath>
#include <memory>

namespace thecad::ui {
class SketchInteractionTest final: public QObject {
    Q_OBJECT
    std::unique_ptr<QMainWindow> host;
    CadViewport* viewport=nullptr;
    QPoint pixel(modeling::Point2 point) { return viewport->screenPoint(point).toPoint(); }
    void click(modeling::Point2 point,QPoint offset={}) {
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel(point)+offset);
    }
    void hover(modeling::Point2 point,QPoint offset={}) {
        const auto p=pixel(point)+offset;
        QMouseEvent event(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&event);
    }
private slots:
    void initTestCase() {
        host=std::make_unique<QMainWindow>(); viewport=new CadViewport({});
        host->setCentralWidget(viewport); host->resize(1100,800); host->show();
        QVERIFY(QTest::qWaitForWindowExposed(host.get())); QTRY_VERIFY(viewport->isReady());
    }
    void init() { viewport->beginSketch({}); QApplication::processEvents(); }
    void pixelSnappingPolicy() {
        using namespace modeling;
        std::vector<ProjectedSketchPoint> candidates{{{"a",PointRole::First},{12.5,6.25},100,200}};
        auto hit=nearestSketchPoint(candidates,107,201,9);
        QVERIFY(hit); QCOMPARE(hit->model.x,12.5); QCOMPARE(hit->model.y,6.25);
        QVERIFY(!nearestSketchPoint(candidates,110,200,9));
        candidates.front().x*=1.5; candidates.front().y*=1.5;
        QVERIFY(nearestSketchPoint(candidates,160.5,301.5,13.5));
        QVERIFY(!nearestSketchPoint(candidates,165,300,13.5));
    }
    void connectedLinesOriginClosureSelectionAndPersistence() {
        viewport->setSketchTool(modeling::EntityKind::Line);
        hover({0,0},{4,3}); QVERIFY(viewport->m_hoveredPoint);
        QCOMPARE(viewport->m_hoveredPoint->role,modeling::PointRole::Origin);
        click({0,0},{4,3}); click({40,0},{0,2}); click({40,30},{2,0}); click({0,30},{0,-2});
        click({0,0},{-4,3});
        const auto sketch=*viewport->m_sketch;
        QCOMPARE(sketch.entities.size(),std::size_t(4));
        QCOMPARE(sketch.entities.front().first.x,0.0); QCOMPARE(sketch.entities.front().first.y,0.0);
        QCOMPARE(sketch.entities.back().second.x,0.0); QCOMPARE(sketch.entities.back().second.y,0.0);
        for(std::size_t n=0;n<4;++n) {
            const auto& a=sketch.entities[n];const auto& b=sketch.entities[(n+1)%4];
            QCOMPARE(a.second.x,b.first.x); QCOMPARE(a.second.y,b.first.y);
        }
        modeling::validateSketchRelations(sketch);
        QVERIFY(sketch.relations.size()>=9);
        click({0,0}); click(sketch.entities.front().second);
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4)); // no zero/duplicate edges
        QTest::keyClick(viewport,Qt::Key_Escape);
        QVERIFY(!viewport->m_firstPoint); QVERIFY(!viewport->m_sketchTool);
        const auto& first=sketch.entities.front();
        click({(first.first.x+first.second.x)/2,0});
        QCOMPARE(viewport->m_selectedEntity,first.id);
        QVERIFY(viewport->m_pointPresentations.size()>=3); // real origin plus selected endpoints
        click(first.second,{3,2});
        QVERIFY(viewport->m_selectedPoint); QVERIFY(!viewport->m_selectedPoint->entityId.empty());
        auto part=document::PartDocument::createEmpty(); const auto id=part.addSketch(sketch);
        const auto contours=modeling::closedContours(sketch); QCOMPARE(contours.size(),std::size_t(1));
        part.addExtrude(id,contours.front().entityIds,20,modeling::Direction::Positive);
        QVERIFY(BRepCheck_Analyzer(part.shape()).IsValid());
        QTemporaryDir dir;const auto path=dir.filePath("connected.thepart"); part.save(path);
        const auto reopened=document::PartDocument::load(path);
        QCOMPARE(reopened.features().front().sketch.relations.size(),sketch.relations.size());
        QCOMPARE(reopened.features().front().sketch.entities.back().second.x,0.0);
        QVERIFY(BRepCheck_Analyzer(reopened.shape()).IsValid());
    }
    void rectangleAndCircleSnapOrigin() {
        viewport->setSketchTool(modeling::EntityKind::Rectangle);
        click({0,0},{-4,4}); hover({30,20}); click({30,20});
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4));
        QVERIFY(viewport->m_sketch->relations.size()>=9);
        modeling::validateSketchRelations(*viewport->m_sketch);
        const auto origin=viewport->m_sketch->entities.front().first;
        QCOMPARE(origin.x,0.0); QCOMPARE(origin.y,0.0);
        viewport->setSketchTool(modeling::EntityKind::Circle);
        click({0,0},{4,-3}); hover({-10,0}); click({-10,0});
        const auto& circle=viewport->m_sketch->entities.back();
        QCOMPARE(circle.kind,modeling::EntityKind::Circle); QCOMPARE(circle.first.x,0.0); QCOMPARE(circle.first.y,0.0);
        QVERIFY(circle.radius>9.5 && circle.radius<10.5);
        modeling::validateSketchRelations(*viewport->m_sketch);
        QCOMPARE(viewport->m_sketchPresentation->ZLayer(), Graphic3d_ZLayerId_Topmost);
    }
    void existingSolidDoesNotFlipSketchCamera() {
        QVERIFY(viewport->setShape(modeling::makeBox(100,70,40)));
        for (const auto plane : {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ}) {
            viewport->endSketch(); viewport->resetView();
            modeling::Sketch sketch; sketch.plane=plane; viewport->beginSketch(sketch);
            const auto axes=modeling::sketchAxes(plane);
            QVERIFY(viewport->m_view->Camera()->Direction().Dot(axes.Direction()) < -0.999999);
            QVERIFY(viewport->m_view->Camera()->Up().Dot(axes.YDirection()) > 0.999999);
            QVERIFY(viewport->m_view->Camera()->Center().Distance(gp_Pnt(0,0,0)) < 1e-8);
        }
        viewport->setShape({});
    }
    void editedAndRemovedPointsUpdateContinuousAnchor() {
        viewport->setSketchTool(modeling::EntityKind::Line); click({0,0}); click({30,20});
        auto replacement=viewport->m_sketch->entities.front(); replacement.second={40,25};
        viewport->setSketch(modeling::editSketchEntity(*viewport->m_sketch,replacement));
        QVERIFY(viewport->m_firstPoint); QCOMPARE(viewport->m_firstPoint->x,40.0); QCOMPARE(viewport->m_firstPoint->y,25.0);
        click({-20,30});
        QCOMPARE(viewport->m_sketch->entities.back().first.x,40.0);
        QCOMPARE(viewport->m_sketch->entities.back().first.y,25.0);
        const auto id=viewport->m_sketch->entities.back().id;
        viewport->setSketch(modeling::removeSketchEntity(*viewport->m_sketch,id));
        QVERIFY(!viewport->m_firstPoint); QVERIFY(!viewport->m_firstReference);
        modeling::validateSketchRelations(*viewport->m_sketch);
    }
    void invalidTinySegmentPreservesSketch() {
        viewport->setSketchTool(modeling::EntityKind::Line); click({0,0});
        QSignalSpy errors(viewport,&CadViewport::sketchInteractionError);
        viewport->commitDrawing({0.0000005,0},{});
        QCOMPARE(errors.count(),1); QVERIFY(viewport->m_sketch->entities.empty());
        QVERIFY(viewport->m_firstPoint); QCOMPARE(viewport->m_firstPoint->x,0.0);
    }
    void panZoomAndEndpointSnap() {
        viewport->setSketchTool(modeling::EntityKind::Line);click({0,0});click({30,20});QTest::keyClick(viewport,Qt::Key_Escape);
        const auto endpoint=viewport->m_sketch->entities.front().second;
        const auto center=viewport->rect().center();
        QTest::mousePress(viewport,Qt::MiddleButton,Qt::NoModifier,center);
        QMouseEvent move(QEvent::MouseMove,center+QPoint(110,55),viewport->mapToGlobal(center+QPoint(110,55)),Qt::NoButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&move);
        QTest::mouseRelease(viewport,Qt::MiddleButton,Qt::NoModifier,center+QPoint(110,55));
        QWheelEvent wheel(center,viewport->mapToGlobal(center),QPoint(),QPoint(0,240),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(viewport,&wheel);QApplication::processEvents();
        viewport->setSketchTool(modeling::EntityKind::Line);click(endpoint,{4,3});click({-10,30});
        const auto& second=viewport->m_sketch->entities.back();
        QCOMPARE(second.first.x,endpoint.x);QCOMPARE(second.first.y,endpoint.y);
        modeling::validateSketchRelations(*viewport->m_sketch);
        QVERIFY(viewport->m_sketch->relations.size()>=2);
    }
    void cleanupTestCase() { host.reset(); }
};
}
QTEST_MAIN(thecad::ui::SketchInteractionTest)
#include "SketchInteractionTests.moc"
