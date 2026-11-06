#include "modeling/SketchTools.h"
#include "modeling/DimensionAnnotation.h"
#include "modeling/DimensionInference.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/RelationCommands.h"
#include "ui/CadViewport.h"
#include "modeling/Box.h"
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_GraphicDriver.hxx>
#include <Graphic3d_TypeOfLimit.hxx>
#include <Image_PixMap.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <QDir>
#include <QImage>
#include <set>
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
    void independentlyDefinedGeometryColorsAndDragging() {
        using namespace modeling;
        Sketch sketch;sketch.entities={{EntityKind::Line,{-40,20},{20,20},1,"horizontal"},{EntityKind::Circle,{25,-25},{},8,"circle"},{EntityKind::Point,{-25,-25},{},1,"fixed"}};
        sketch.relations={{"axis",RelationKind::Horizontal,{"horizontal",PointRole::First},{"horizontal",PointRole::Second}},{"pin",RelationKind::FixedPoint,{"fixed",PointRole::First},{},{-25,-25}}};
        viewport->beginSketch(sketch);viewport->selectSketchTool();hover({60,60});
        QVERIFY(viewport->m_definition.valid);QVERIFY(!viewport->m_sketchFullyDefined);
        bool black=false,blue=false;for(const auto& batch:viewport->m_sketchBatches){Quantity_Color color;batch->Color(color);black|=color.Distance(Quantity_Color(Quantity_NOC_BLACK))<1e-6;blue|=color.Distance(Quantity_Color(0.1,0.22,0.60,Quantity_TOC_RGB))<1e-6;}
        QVERIFY(!black&&blue);
        // Inspect the actual marker sprite colors, without screenshot comparisons.
        QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(5));
        const auto markerColor=[&](std::size_t index){const auto image=viewport->m_pointPresentations[index]->Attributes()->PointAspect()->Aspect()->MarkerImage()->GetImage();return image->PixelColor(image->SizeX()/2,image->SizeY()/2).GetRGB();};
        QVERIFY(markerColor(1).Distance(Quantity_Color(Quantity_NOC_BLUE))<0.01);
        QVERIFY(markerColor(2).Distance(Quantity_Color(Quantity_NOC_BLUE))<0.01);
        QVERIFY(markerColor(3).Distance(Quantity_Color(Quantity_NOC_BLUE))<0.01);
        QVERIFY(markerColor(4).Distance(Quantity_Color(Quantity_NOC_BLACK))<0.01);
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({20,20}));QVERIFY(viewport->m_pointDrag);
        const auto target=pixel({40,35});QMouseEvent move(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);QVERIFY(viewport->m_pointDrag->candidate);
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,target);
        QVERIFY(viewport->m_sketch->entities[0].second.x>30);QVERIFY(std::abs(viewport->m_sketch->entities[0].first.y-viewport->m_sketch->entities[0].second.y)<1e-6);
        QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(2));
        QVERIFY(viewport->m_definition.definedCurves.empty());
        auto anchored=*viewport->m_sketch;const auto& line=anchored.entities[0];
        anchored.relations.push_back({"anchor",RelationKind::FixedPoint,{"horizontal",PointRole::First},{},line.first});
        SketchDimension length;length.id="length";length.kind=DimensionKind::LineLength;length.first={"horizontal",PointRole::First};length.second={"horizontal",PointRole::Second};length.value=line.second.x-line.first.x;
        viewport->setSketch(setSketchDimension(anchored,length));
        QCOMPARE(viewport->m_definition.definedCurves,std::vector<std::string>{"horizontal"});
        black=false;blue=false;for(const auto& batch:viewport->m_sketchBatches){Quantity_Color color;batch->Color(color);black|=color.Distance(Quantity_Color(Quantity_NOC_BLACK))<1e-6;blue|=color.Distance(Quantity_Color(0.1,0.22,0.60,Quantity_TOC_RGB))<1e-6;}
        QVERIFY(black&&blue);
        viewport->setSketch(removeSketchDimension(*viewport->m_sketch,"length"));
        QVERIFY(viewport->m_definition.definedCurves.empty());
    }
    void definedCirclesAndConstructionGeometryRenderBlack() {
        using namespace modeling;
        Sketch sketch;sketch.entities={{EntityKind::Circle,{-25,20},{},8,"circle"},{EntityKind::Circle,{25,20},{},8,"construction-circle",true},{EntityKind::Line,{-20,-20},{20,-20},1,"centerline",true},{EntityKind::Point,{40,-30},{},1,"free"}};
        sketch=addSelectionRelations(sketch,RelationKind::FixedEntity,{"circle","construction-circle","centerline"},"fix");viewport->beginSketch(sketch);viewport->selectSketchTool();hover({70,60});
        QVERIFY(!viewport->m_sketchFullyDefined);QCOMPARE(viewport->m_sketchBatches.size(),std::size_t(2));
        for(const auto& batch:viewport->m_sketchBatches){Quantity_Color color;batch->Color(color);QVERIFY(color.Distance(Quantity_Color(Quantity_NOC_BLACK))<1e-6);}
        QVERIFY(!viewport->m_constructionPresentation.IsNull());
        QCOMPARE(viewport->m_constructionPresentation->Attributes()->WireAspect()->Aspect()->Type(),Aspect_TOL_DOTDASH);
        const auto color=[&](std::size_t index){const auto image=viewport->m_pointPresentations[index]->Attributes()->PointAspect()->Aspect()->MarkerImage()->GetImage();return image->PixelColor(image->SizeX()/2,image->SizeY()/2).GetRGB();};
        QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(6));
        for(std::size_t i=1;i<5;++i)QVERIFY(color(i).Distance(Quantity_Color(Quantity_NOC_BLACK))<0.01);
        QVERIFY(color(5).Distance(Quantity_Color(Quantity_NOC_BLUE))<0.01);
    }
    void midpointCuesCreationAndPersistentDragging() {
        using namespace modeling;
        for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ}){
            Sketch sketch;sketch.plane=plane;sketch.entities={{EntityKind::Line,{-40,20},{40,20},1,"guide"}};
            viewport->beginSketch(sketch);viewport->selectSketchTool();hover({0,20});QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(3));QVERIFY(!viewport->pointAt(pixel({0,20})));
            viewport->setSketchTool(EntityKind::Point);hover({0,20},{2,1});QVERIFY(viewport->m_hoveredPoint);QCOMPARE(viewport->m_hoveredPoint->role,PointRole::Midpoint);QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(4));
            hover({-20,-20});QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(3));click({0,20},{2,1});QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Midpoint);QCOMPARE(viewport->m_sketch->entities[1].first.x,0.0);QCOMPARE(viewport->m_sketch->entities[1].first.y,20.0);
            viewport->selectSketchTool();QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(4));
            viewport->beginSketch(sketch);viewport->setSketchTool(EntityKind::Line);hover({0,20},{1,-1});QCOMPARE(viewport->m_hoveredPoint->role,PointRole::Midpoint);click({0,20},{1,-1});QVERIFY(viewport->m_firstReference);QCOMPARE(viewport->m_firstReference->role,PointRole::Midpoint);click({25,45});QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Midpoint);QCOMPARE(viewport->m_sketch->entities[1].first.x,0.0);QCOMPARE(viewport->m_sketch->entities[1].first.y,20.0);
            auto guide=viewport->m_sketch->entities[0];guide.second={60,40};auto resized=editSketchEntity(*viewport->m_sketch,guide);viewport->setSketch(resized);const auto midpoint=sketchPointValue(*viewport->m_sketch,{"guide",PointRole::Midpoint});QVERIFY(std::abs(viewport->m_sketch->entities[1].first.x-midpoint.x)<1e-6);QVERIFY(std::abs(viewport->m_sketch->entities[1].first.y-midpoint.y)<1e-6);QVERIFY(std::abs(midpoint.x)>1);validateSketchRelations(*viewport->m_sketch);
            viewport->beginSketch(sketch);viewport->setSketchTool(EntityKind::Line,true);click({0,20});click({-20,40});QVERIFY(viewport->m_sketch->entities[1].construction);QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Midpoint);
            viewport->beginSketch(sketch);viewport->setSketchTool(EntityKind::Circle);hover({0,20});QVERIFY(!viewport->m_hoveredPoint);QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(3));
        }
        Sketch drag;drag.entities={{EntityKind::Line,{-40,20},{40,20},1,"guide"},{EntityKind::Point,{-25,-20},{},1,"point"}};drag=modeling::addSelectionRelations(drag,RelationKind::FixedEntity,{"guide"},"fix");viewport->beginSketch(drag);viewport->selectSketchTool();
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({-25,-20}));const auto end=pixel({0,20})+QPoint(2,1);QMouseEvent move(QEvent::MouseMove,end,viewport->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);QVERIFY(viewport->m_pointDrag && viewport->m_pointDrag->target);QCOMPARE(viewport->m_pointDrag->target->role,PointRole::Midpoint);QVERIFY(viewport->m_pointDrag->candidate);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,end);QCOMPARE(viewport->m_sketch->relations.back().kind,RelationKind::Midpoint);QVERIFY(std::abs(viewport->m_sketch->entities[1].first.y-20)<1e-6);
    }
    void allFreeRectangleCornersResizeFromOriginAnchor() {
        using namespace modeling;
        for(const auto plane:{SketchPlane::XY,SketchPlane::XZ,SketchPlane::YZ})for(int corner=0;corner<3;++corner){
            Sketch empty;empty.plane=plane;viewport->beginSketch(empty);viewport->setSketchTool(EntityKind::Rectangle);click({0,0});click({-40,30});viewport->selectSketchTool();const auto original=*viewport->m_sketch;
            const Point2 start=corner==0?Point2{-40,0}:corner==1?Point2{0,30}:Point2{-40,30};const Point2 finish=corner==0?Point2{-55,9}:corner==1?Point2{9,45}:Point2{-55,45};
            QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(start));QVERIFY(viewport->m_pointDrag);const auto target=pixel(finish);QMouseEvent move(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);QVERIFY(viewport->m_pointDrag->candidate);validateSketchRelations(*viewport->m_sketch);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,target);
            const auto& s=*viewport->m_sketch;QCOMPARE(s.entities[0].first.x,0.0);QCOMPARE(s.entities[0].first.y,0.0);QCOMPARE(s.relations.size(),original.relations.size());
            QVERIFY(std::abs(s.entities[0].second.x-(corner==1?-40:-55))<0.5);QVERIFY(std::abs(s.entities[2].second.y-(corner==0?30:45))<0.5);
            QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({0,0}));QVERIFY(!viewport->m_pointDrag);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,pixel({0,0}));
        }
    }
    void pointToolSnapsSelectsDragsAndPersistsRelations() {
        using namespace modeling;Sketch sketch;sketch.entities={{EntityKind::Line,{-40,20},{40,20},1,"line"},{EntityKind::Circle,{30,-25},{},8,"circle"}};
        viewport->beginSketch(sketch);viewport->setSketchTool(EntityKind::Point);click({0,0},{3,2});click({0,20},{0,4});click({38,-25},{3,0});click({-30,-20});
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(6));QCOMPARE(viewport->m_sketch->entities[2].kind,EntityKind::Point);
        QCOMPARE(viewport->m_sketch->entities[2].first.x,0.0);QCOMPARE(viewport->m_sketch->entities[2].first.y,0.0);
        QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Coincident);QCOMPARE(viewport->m_sketch->relations[1].kind,RelationKind::Midpoint);QCOMPARE(viewport->m_sketch->relations[2].kind,RelationKind::PointOnCircle);
        QVERIFY(closedContours(*viewport->m_sketch).size()==1); // Only the existing circle is a profile.
        viewport->selectSketchTool();click({0,0});QVERIFY(viewport->m_selectedPoint);QCOMPARE(viewport->m_selectedPoint->entityId,viewport->m_sketch->entities[2].id);
        const auto id=viewport->m_sketch->entities.back().id;QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({-30,-20}));
        const auto target=pixel({-20,-30});QMouseEvent motion(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&motion);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,target);
        QCOMPARE(viewport->m_sketch->entities.back().id,id);QVERIFY(std::abs(viewport->m_sketch->entities.back().first.x+20)<0.5);validateSketchRelations(*viewport->m_sketch);
    }
    void centerlineAndPolygonCreationPreviewAndDrag() {
        using namespace modeling;viewport->setSketchTool(EntityKind::Line,true);click({-30,-20});hover({30,-20});click({30,-20});
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));QVERIFY(viewport->m_sketch->entities[0].construction);QVERIFY(!viewport->m_constructionPresentation.IsNull());QVERIFY(closedContours(*viewport->m_sketch).empty());
        viewport->beginSketch({});viewport->setSketchTool(EntityKind::Polygon);click({0,0});hover({20,0});QVERIFY(viewport->m_drawingPreview);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(0));QVERIFY(!viewport->m_sketchPresentation.IsNull());
        click({20,0});QCOMPARE(viewport->m_sketch->polygons.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->polygons[0].edges.size(),std::size_t(6));QCOMPARE(closedContours(*viewport->m_sketch).size(),std::size_t(1));
        viewport->selectSketchTool();const auto vertex=viewport->m_sketch->entities[1].first;QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(vertex));
        const auto target=pixel({25,8});QMouseEvent motion(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&motion);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,target);
        validatePolygons(*viewport->m_sketch,true);QVERIFY(viewport->m_sketch->entities[1].first.y>1);
    }
    void dimensionPlacementDragAndLiveGeometry() {
        using namespace modeling;
        Sketch sketch;sketch.entities={{EntityKind::Line,{-30,-20},{30,-20},1,"line"},{EntityKind::Circle,{-30,30},{},8,"circle"}};
        auto linear=inferDimension(sketch,{},{"line"},{0,-35});linear.id="length";linear.driving=false;
        auto circle=inferDimension(sketch,{},{"circle"},{-10,45});circle.id="diameter";circle.driving=false;
        sketch.dimensions={linear,circle};viewport->beginSketch(sketch);viewport->selectSketchTool();
        QSignalSpy changes(viewport,&CadViewport::sketchChanged);
        const auto move=[&](Point2 target){const auto p=pixel(target);QMouseEvent event(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&event);};
        const auto original=*viewport->m_sketch;
        for(const Point2 target: {Point2{10,-45},Point2{-20,-40}}) {
            const auto anchor=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);
            QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(anchor));QVERIFY(viewport->m_dimensionDrag);
            const auto count=changes.count();move(target);QVERIFY(viewport->m_dimensionDrag->moved);QCOMPARE(changes.count(),count);
            QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,pixel(target));QCOMPARE(changes.count(),count+1);
            const auto text=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);QVERIFY(std::hypot(text.x-target.x,text.y-target.y)<0.5);
            QCOMPARE(viewport->m_sketch->dimensions[0].value,original.dimensions[0].value);
            QCOMPARE(viewport->m_sketch->entities[0].second.x,30.0);
        }
        auto anchor=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(anchor));move({40,-45});QTest::keyClick(viewport,Qt::Key_Escape);
        const auto restored=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);QVERIFY(std::hypot(restored.x-anchor.x,restored.y-anchor.y)<1e-7);QCOMPARE(changes.count(),2);
        // During geometry dragging, the rendered text must already use the candidate geometry.
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({30,-20}));QVERIFY(viewport->m_pointDrag);QVERIFY(!viewport->m_dimensionDrag);
        move({45,-10});QVERIFY(viewport->m_pointDrag->candidate);QCOMPARE(changes.count(),2);
        const auto text=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);
        QVERIFY(std::hypot(text.x-anchor.x,text.y-anchor.y)>1);
        const auto expected=sketchPoint(*viewport->m_sketch,text);QVERIFY(viewport->m_dimensions.labels()[0]->Position().Distance(expected)<1e-7);
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,pixel({45,-10}));QCOMPARE(changes.count(),3);
        // Circular annotation rotates on repeated drags, including while Smart Dimension is active.
        viewport->setSmartDimensionPicking(true);anchor=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[1]);
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(anchor));QVERIFY(viewport->m_dimensionDrag);move({-50,45});
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,pixel({-50,45}));QVERIFY(viewport->m_tools.dimensionPicking());
        const auto g=dimensionGraphics(*viewport->m_sketch,viewport->m_sketch->dimensions[1],0.2);
        QVERIFY(g.segments.front().second.x < -30);QCOMPARE(viewport->m_sketch->entities[1].radius,8.0);
        viewport->setSmartDimensionPicking(false);
    }
    void geometryPickingToleranceAndHover() {
        using namespace modeling;Sketch sketch;sketch.entities={{EntityKind::Line,{-40,-20},{40,-20},1,"a"},{EntityKind::Line,{-40,-15},{40,-15},1,"b"},{EntityKind::Circle,{20,30},{},1,"small"},{EntityKind::Line,{-25,30},{-24,30},1,"short"}};
        viewport->beginSketch(sketch);viewport->selectSketchTool();QCOMPARE(viewport->pickingTolerance(),8.0);
        for(const double scale:{100.0,200.0}) {
            viewport->m_graphics.view()->Camera()->SetScale(scale);
            QVERIFY(viewport->entityAt(pixel({0,-20})+QPoint(0,6))==std::optional<std::string>("a"));
            QVERIFY(!viewport->entityAt(pixel({0,-20})+QPoint(0,12)));
            QVERIFY(viewport->entityAt(pixel({21,30})+QPoint(6,0))==std::optional<std::string>("small"));
            QVERIFY(viewport->entityAt(pixel({-24.5,30})+QPoint(0,6))==std::optional<std::string>("short"));
            hover({0,-20},{0,6});QCOMPARE(viewport->m_hoveredEntity,std::string("a"));QVERIFY(!viewport->m_hoveredEntityPresentation.IsNull());
            hover({-40,-20},{2,0});QVERIFY(viewport->m_hoveredPoint);QVERIFY(viewport->m_hoveredEntity.empty());
            hover({21,30});QVERIFY(!viewport->m_hoveredPoint);QCOMPARE(viewport->m_hoveredEntity,std::string("small"));
            viewport->setSmartDimensionPicking(true);QSignalSpy references(viewport,&CadViewport::dimensionReferencePicked);
            click({21,30});QCOMPARE(references.count(),1);QCOMPARE(references[0][0].toString(),QString("small"));QCOMPARE(references[0][1].toInt(),-1);
            viewport->setSmartDimensionPicking(false);
            // The closest of nearby parallel lines wins.
            QVERIFY(viewport->entityAt(pixel({0,-15})+QPoint(0,-1))==std::optional<std::string>("b"));
        }
        viewport->setPickingTolerance(4);QVERIFY(!viewport->entityAt(pixel({0,-20})+QPoint(0,6)));
        viewport->setPickingTolerance(12);QVERIFY(viewport->entityAt(pixel({0,-20})+QPoint(0,10)));
        viewport->setPickingTolerance(8);
    }
    void constrainedLineAndCircleDragging() {
        using namespace modeling;Sketch sketch;sketch.entities={{EntityKind::Line,{0,0},{30,40},1,"line"},{EntityKind::Circle,{-30,-20},{},5,"circle"}};sketch.relations={{"anchor",RelationKind::Coincident,{"line",PointRole::First},{}}};
        SketchDimension length;length.id="length";length.kind=DimensionKind::LineLength;length.first={"line",PointRole::First};length.second={"line",PointRole::Second};length.value=50;length.annotation={20,45};
        SketchDimension radius;radius.id="radius";radius.kind=DimensionKind::CircleRadius;radius.first={"circle",PointRole::Center};radius.value=5;radius.annotation={-40,-30};sketch.dimensions={length,radius};
        viewport->beginSketch(requireSolvedSketch(solvePlanarSketch(sketch)));viewport->selectSketchTool();
        const auto drag=[&](Point2 from,Point2 to){QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(from));const auto p=pixel(to);QMouseEvent event(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&event);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,p);};
        drag({30,40},{40,60});const auto endpoint=viewport->m_sketch->entities[0].second;QVERIFY(std::hypot(endpoint.x-30,endpoint.y-40)>1);QVERIFY(std::abs(std::hypot(endpoint.x,endpoint.y)-50)<1e-6);QCOMPARE(viewport->m_sketch->entities[0].first.x,0.0);
        drag({-30,-20},{-20,-30});QVERIFY(std::abs(viewport->m_sketch->entities[1].radius-5)<1e-6);QVERIFY(std::abs(viewport->m_sketch->entities[1].first.x+20)<1);
        auto pinned=*viewport->m_sketch;SketchRelation fixed;fixed.id="fixed-center";fixed.kind=RelationKind::FixedPoint;fixed.first={"circle",PointRole::Center};fixed.fixedPosition=pinned.entities[1].first;pinned.relations.push_back(fixed);viewport->setSketch(pinned);const auto center=pinned.entities[1].first;drag(center,{-40,-40});QCOMPARE(viewport->m_sketch->entities[1].first.x,center.x);QCOMPARE(viewport->m_sketch->entities[1].first.y,center.y);
    }
    void renderingQualityBeforeAndAfter() {
        const auto expected=viewport->m_graphics.view()->RenderingParams();
        if (viewport->m_graphics.viewer()->Driver()->InquireLimit(Graphic3d_TypeOfLimit_MaxMsaa) >= 4)
            QCOMPARE(expected.NbMsaaSamples,4);
        modeling::Sketch sketch; sketch.entities={
            {modeling::EntityKind::Line,{-45,-12},{40,24},1,"diagonal"},
            {modeling::EntityKind::Circle,{18,-25},{},12,"circle"}};
        viewport->setSketch(sketch);
        const QString directory=qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR",QDir::currentPath()+"/test-artifacts")+"/rendering";
        QVERIFY(QDir().mkpath(directory));
        for (const double zoom : {0.75,1.0,3.0}) {
            viewport->m_graphics.view()->Camera()->SetScale(150.0/zoom);
            const QString stem=directory+QString("/sketch-%1").arg(zoom);
            viewport->m_graphics.view()->ChangeRenderingParams().NbMsaaSamples=0;
            viewport->m_graphics.view()->ChangeRenderingParams().IsAntialiasingEnabled=false;
            viewport->m_sketchPresentation->SetWidth(3.0);
            viewport->m_sketchPresentation->Attributes()->SetDeviationCoefficient(0.001);
            viewport->m_sketchPresentation->Attributes()->SetDeviationAngle(0.5);
            viewport->m_graphics.context()->Redisplay(viewport->m_sketchPresentation,false);
            QVERIFY(viewport->m_graphics.view()->Dump((stem+"-before.png").toUtf8().constData()));
            viewport->m_graphics.view()->ChangeRenderingParams()=expected;
            viewport->m_sketchPresentation->SetWidth(2.0*viewport->devicePixelRatioF());
            viewport->m_sketchPresentation->Attributes()->SetDeviationCoefficient(1e-5);
            viewport->m_sketchPresentation->Attributes()->SetDeviationAngle(0.015);
            viewport->m_graphics.context()->Redisplay(viewport->m_sketchPresentation,false);
            QVERIFY(viewport->m_graphics.view()->Dump((stem+"-after.png").toUtf8().constData()));
            const QImage before(stem+"-before.png"),after(stem+"-after.png");
            QVERIFY(!before.isNull() && !after.isNull()); QCOMPARE(before.size(),after.size());
            QVERIFY(before!=after);
            QCOMPARE(viewport->m_sketch->entities[0].first.x,-45.0);
            QCOMPARE(viewport->m_sketch->entities[1].radius,12.0);
        }
    }
    void endpointsVisibleDragJoinAndPropagate() {
        modeling::Sketch sketch{modeling::SketchPlane::XY,{
            {modeling::EntityKind::Line,{-40,-10},{-20,0},1,"drag-a"},
            {modeling::EntityKind::Line,{10,20},{35,35},1,"drag-b"}}};
        viewport->beginSketch(sketch); viewport->selectSketchTool();
        QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(5));
        QSignalSpy changes(viewport,&CadViewport::sketchChanged);
        const auto direction=viewport->m_graphics.view()->Camera()->Direction();
        const auto drag=[&](modeling::Point2 from,modeling::Point2 to,QPoint offset={}) {
            QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(from));
            const auto p=pixel(to)+offset;
            QMouseEvent event(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(viewport,&event);
            QVERIFY(viewport->m_pointDrag); QVERIFY(viewport->m_pointDrag->candidate);
            QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,p);
        };
        drag({-20,0},{10,20},{3,2});
        QCOMPARE(changes.count(),1); QVERIFY(!viewport->m_pointDrag);
        QCOMPARE(viewport->m_sketch->entities[0].second.x,10.0);
        QCOMPARE(viewport->m_sketch->entities[0].second.y,20.0);
        QVERIFY(modeling::areSketchPointsCoincident(*viewport->m_sketch,{"drag-a",modeling::PointRole::Second},{"drag-b",modeling::PointRole::First}));
        drag({10,20},{-5,25});
        QCOMPARE(changes.count(),2);
        const auto& a=viewport->m_sketch->entities[0];const auto& b=viewport->m_sketch->entities[1];
        QCOMPARE(a.second.x,b.first.x);QCOMPARE(a.second.y,b.first.y);
        QVERIFY(direction.Angle(viewport->m_graphics.view()->Camera()->Direction())<1e-10);
        modeling::validateSketchRelations(*viewport->m_sketch);
        auto part=document::PartDocument::createEmpty();part.addSketch(*viewport->m_sketch);
        QTemporaryDir dir;const auto path=dir.filePath("dragged.thepart");part.save(path);
        const auto restored=document::PartDocument::load(path).features()[0].sketch;
        QVERIFY(modeling::areSketchPointsCoincident(restored,{"drag-a",modeling::PointRole::Second},{"drag-b",modeling::PointRole::First}));
    }
    void dragToOriginAndRejectOrCancelSafely() {
        modeling::Sketch sketch{modeling::SketchPlane::XY,{{modeling::EntityKind::Line,{-40,-10},{-20,0},1,"drag"}}};
        viewport->beginSketch(sketch);viewport->selectSketchTool();
        QSignalSpy changes(viewport,&CadViewport::sketchChanged);
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({-20,0}));
        auto p=pixel({0,0})+QPoint(3,2);
        QMouseEvent move(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&move);
        QVERIFY(!viewport->m_interactionLabel.IsNull()); QCOMPARE(changes.count(),0);
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,p);
        QCOMPARE(changes.count(),1); QCOMPARE(viewport->m_sketch->entities[0].second.x,0.0);
        QCOMPARE(viewport->m_sketch->entities[0].second.y,0.0);
        QVERIFY(modeling::areSketchPointsCoincident(*viewport->m_sketch,{"drag",modeling::PointRole::Second},{}));
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({0,0}));
        QVERIFY(!viewport->m_pointDrag);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,pixel({0,0}));
        viewport->beginSketch(sketch);viewport->selectSketchTool();changes.clear();
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel({-20,0}));
        p=pixel({10,25});QMouseEvent preview(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&preview);QVERIFY(viewport->m_pointDrag->moved);
        QTest::keyClick(viewport,Qt::Key_Escape);
        QVERIFY(!viewport->m_pointDrag);QCOMPARE(changes.count(),0);
        QCOMPARE(viewport->m_sketch->entities[0].second.x,-20.0);
        viewport->beginSketch({});viewport->setSketchTool(modeling::EntityKind::Rectangle);click({0,0});click({30,20});QTest::keyClick(viewport,Qt::Key_Escape);
        const auto original=*viewport->m_sketch; const auto corner=original.entities[0].second;changes.clear();
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,pixel(corner));
        p=pixel({40,10});QMouseEvent bad(QEvent::MouseMove,p,viewport->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&bad); QVERIFY(viewport->m_pointDrag); QVERIFY(viewport->m_pointDrag->candidate);
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,p);QCOMPARE(changes.count(),1);
        QVERIFY(std::abs(viewport->m_sketch->entities[0].second.x-40)<0.5);QCOMPARE(viewport->m_sketch->entities[0].second.y,corner.y);
        QCOMPARE(viewport->m_sketch->relations.size(),original.relations.size());
    }
    void axialInferenceAllPlanesAndDirections() {
        for(const auto plane:{modeling::SketchPlane::XY,modeling::SketchPlane::XZ,modeling::SketchPlane::YZ}) {
            for(const auto target:{modeling::Point2{50,20},modeling::Point2{-20,20},modeling::Point2{20,50},modeling::Point2{20,-20}}) {
                modeling::Sketch sketch;sketch.plane=plane;viewport->beginSketch(sketch);viewport->setSketchTool(modeling::EntityKind::Line);
                click({20,20}); const bool horizontal=target.y==20;
                hover(target,horizontal?QPoint(0,3):QPoint(3,0));
                QCOMPARE(viewport->m_inference.axis,horizontal?modeling::RelationKind::Horizontal:modeling::RelationKind::Vertical);
                QVERIFY(!viewport->m_inferenceGuides.empty());QVERIFY(!viewport->m_interactionLabel.IsNull());
                QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(0));
                click(target,horizontal?QPoint(0,3):QPoint(3,0));
                const auto& entity=viewport->m_sketch->entities[0];
                if(horizontal) QCOMPARE(entity.first.y,entity.second.y);else QCOMPARE(entity.first.x,entity.second.x);
                QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));
                QVERIFY(viewport->m_inferenceGuides.empty());modeling::validateSketchRelations(*viewport->m_sketch);
            }
        }
    }
    void inferenceHysteresisAlignmentAndFreeAngles() {
        viewport->setSketchTool(modeling::EntityKind::Line);click({20,20});
        hover({50,20},{0,5});QCOMPARE(viewport->m_inference.axis,modeling::RelationKind::Horizontal);
        hover({50,20},{0,9});QCOMPARE(viewport->m_inference.axis,modeling::RelationKind::Horizontal);
        hover({50,20},{0,16});QVERIFY(!viewport->m_inference.axis);
        hover({50,47});QVERIFY(!viewport->m_inference.axis);QVERIFY(viewport->m_inferenceGuides.empty());
        click({50,47});QVERIFY(viewport->m_sketch->relations.empty());
        modeling::Sketch sketch{modeling::SketchPlane::XY,{{modeling::EntityKind::Line,{-30,-20},{-10,-5},1,"guide-anchor"}}};
        viewport->beginSketch(sketch);viewport->setSketchTool(modeling::EntityKind::Line);click({20,25});
        hover({-30,45},{2,0});QVERIFY(viewport->m_inference.alignX);QVERIFY(!viewport->m_inferenceGuides.empty());
        click({-30,45},{2,0});QCOMPARE(viewport->m_sketch->entities.back().second.x,-30.0);
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QVERIFY(viewport->m_sketch->relations.empty());
        QVERIFY(viewport->m_inferenceGuides.empty());
    }
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
        QVERIFY(!viewport->m_firstPoint); QVERIFY(!viewport->m_tools.drawing());
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
            QVERIFY(viewport->m_graphics.view()->Camera()->Direction().Dot(axes.Direction()) < -0.999999);
            QVERIFY(viewport->m_graphics.view()->Camera()->Up().Dot(axes.YDirection()) > 0.999999);
            QVERIFY(viewport->m_graphics.view()->Camera()->Center().Distance(gp_Pnt(0,0,0)) < 1e-8);
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
