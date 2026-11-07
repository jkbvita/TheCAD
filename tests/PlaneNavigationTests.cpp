#include <Image_PixMap.hxx>
#include <Geom_Point.hxx>
#include <Prs3d_LineAspect.hxx>
#include "ui/MainWindow.h"
#include "modeling/Box.h"
#include "modeling/DimensionInference.h"
#include <QElapsedTimer>
#include "ui/CadViewport.h"
#include "presentation/SelectionAppearance.h"
#include <Graphic3d_Camera.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
namespace thecad::ui {
using namespace presentation;
using namespace modeling;
class PlaneWindow:public MainWindow {
public:QStringList errors;
protected:UnsavedChoice confirmUnsaved()override{return UnsavedChoice::Discard;}void reportError(const QString& e)override{errors<<e;}
};
class PlaneNavigationTest:public QObject {
    Q_OBJECT
    QPoint project(CadViewport* v,gp_Pnt p){v->synchronizeSurface();int x,y;v->m_graphics.view()->Convert(p.X(),p.Y(),p.Z(),x,y);return {qRound(x/v->devicePixelRatioF()),qRound(y/v->devicePixelRatioF())};}
    void drag(CadViewport* v,Qt::MouseButton button,Qt::KeyboardModifiers modifiers,QPoint from,QPoint to){QTest::mousePress(v,button,modifiers,from);QMouseEvent move(QEvent::MouseMove,to,v->mapToGlobal(to),Qt::NoButton,button,modifiers);QApplication::sendEvent(v,&move);QTest::mouseRelease(v,button,modifiers,to);}
    void action(PlaneWindow& w,const char* name){auto* a=w.findChild<QAction*>(name);QVERIFY(a&&a->isEnabled());a->trigger();QApplication::processEvents();}
    void select(PlaneWindow& w,const QString& id){auto* tree=w.findChild<QTreeWidget*>("featureTree");for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString()==id){tree->setCurrentItem(*i,0,QItemSelectionModel::ClearAndSelect);return;}QFAIL("Missing feature");}
    void confirm(PlaneWindow& w){auto* buttons=w.findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
private slots:
    void navigationAnnotationWork(){
        PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());
        Sketch sketch;for(int i=0;i<24;++i){const auto id="line-"+std::to_string(i);const double x=(i%4)*28.0-55,y=(i/4)*18.0-45;sketch.entities.push_back({EntityKind::Line,{x,y},{x+20,y},1,id});sketch.relations.push_back({"relation-"+std::to_string(i),RelationKind::Horizontal,{id,PointRole::First},{id,PointRole::Second}});if(i%3==0){auto d=inferDimension(sketch,{}, {id},{x+10,y+6});d.id="dimension-"+std::to_string(i);sketch.dimensions.push_back(d);}}
        v->beginSketch(sketch);v->selectSketchTool();QApplication::processEvents();QVERIFY(!v->m_relationMarkers.empty());QVERIFY(!v->m_dimensions.guides().empty());
        QVERIFY(!v->m_graphics.view()->SetImmediateUpdate(Standard_False));
        QSignalSpy changed(v,&CadViewport::sketchChanged);const auto frame=sketchAxes(*v->m_sketch);
        const QPoint start(400,300);QTest::mousePress(v,Qt::MiddleButton,Qt::NoModifier,start);
        const auto marker=v->m_relationMarkers.front();const auto guide=v->m_dimensions.guides().front();int churn=0;QElapsedTimer elapsed;elapsed.start();
        for(int i=1;i<=40;++i){const QPoint point=start+QPoint(i,i/2);QMouseEvent move(QEvent::MouseMove,point,v->mapToGlobal(point),Qt::NoButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(v,&move);if(v->m_relationMarkers.empty()||v->m_relationMarkers.front()!=marker)++churn;}
        qInfo("Navigation input: %lld us for 40 moves; annotation replacements: %d",elapsed.nsecsElapsed()/1000,churn);
        QCOMPARE(churn,0);QCOMPARE(v->m_dimensions.guides().front(),guide);QVERIFY(v->m_cameraAnnotationsDirty);
        v->repaint();QVERIFY(!v->m_cameraAnnotationsDirty);QCOMPARE(v->m_relationMarkers.front(),marker);QCOMPARE(v->m_dimensions.guides().front(),guide);
        const auto markerPixel=project(v,marker->Component()->Pnt());QVERIFY((markerPixel-v->m_relationHits.front().bounds.center().toPoint()).manhattanLength()<3);
        QTest::mouseRelease(v,Qt::MiddleButton,Qt::NoModifier,start+QPoint(40,20));QApplication::processEvents();
        // Wheel events also coalesce, but a zoom frame must resize annotation arrows.
        const auto zoomGuide=v->m_dimensions.guides().front();const auto zoomMarker=v->m_relationMarkers.front();
        for(int i=0;i<8;++i){QWheelEvent wheel(start,v->mapToGlobal(start),{},QPoint(0,15),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(v,&wheel);}
        QCOMPARE(v->m_dimensions.guides().front(),zoomGuide);QVERIFY(v->m_cameraAnnotationsDirty);v->repaint();QVERIFY(!v->m_cameraAnnotationsDirty);QVERIFY(v->m_dimensions.guides().front()!=zoomGuide);QCOMPARE(v->m_relationMarkers.front(),zoomMarker);
        QTest::mousePress(v,Qt::MiddleButton,Qt::ControlModifier,start);const auto panGuide=v->m_dimensions.guides().front();const auto panMarker=v->m_relationMarkers.front();
        const QPoint panEnd=start+QPoint(25,10);QMouseEvent pan(QEvent::MouseMove,panEnd,v->mapToGlobal(panEnd),Qt::NoButton,Qt::MiddleButton,Qt::ControlModifier);QApplication::sendEvent(v,&pan);v->repaint();QCOMPARE(v->m_dimensions.guides().front(),panGuide);QCOMPARE(v->m_relationMarkers.front(),panMarker);
        // Losing the mouse grab must finish the deferred layout too.
        QEvent lostGrab(QEvent::UngrabMouse);QApplication::sendEvent(v,&lostGrab);v->releaseMouse();QApplication::processEvents();QCOMPARE(v->m_camera.mode(),CadViewport::Navigation::None);QVERIFY(!v->m_cameraAnnotationsDirty);
        for(const auto& hit:v->m_relationHits)QVERIFY(v->relationAt(hit.bounds.center())==std::optional<std::string>(hit.id));
        QCOMPARE(changed.count(),0);QVERIFY(frame.Location().Distance(sketchAxes(*v->m_sketch).Location())<1e-12);QVERIFY(frame.Direction().IsEqual(sketchAxes(*v->m_sketch).Direction(),1e-12));QVERIFY(!v->m_relationMarkers.empty());QVERIFY(!v->m_dimensions.guides().empty());w.setWindowModified(false);w.close();
    }
    void navigationAndSketchPlaneStayIndependent(){
        PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());Sketch s;s.entities={{EntityKind::Line,{-25,0},{25,0},1,"line"}};v->beginSketch(s);v->selectSketchTool();const auto frame=sketchAxes(*v->m_sketch);const auto before=v->m_graphics.view()->Camera()->Direction();QSignalSpy changed(v,&CadViewport::sketchChanged);
        drag(v,Qt::MiddleButton,Qt::NoModifier,{420,260},{560,330});QVERIFY(before.Angle(v->m_graphics.view()->Camera()->Direction())>0.05);QCOMPARE(v->m_sketch->entities.size(),std::size_t(1));QVERIFY(frame.Location().Distance(sketchAxes(*v->m_sketch).Location())<1e-10);QVERIFY(frame.Direction().IsEqual(sketchAxes(*v->m_sketch).Direction(),1e-12));QCOMPARE(changed.count(),0);
        const auto center=v->m_graphics.view()->Camera()->Center();const auto direction=v->m_graphics.view()->Camera()->Direction();drag(v,Qt::MiddleButton,Qt::ControlModifier,{430,250},{480,300});QVERIFY(center.Distance(v->m_graphics.view()->Camera()->Center())>1);QVERIFY(direction.IsEqual(v->m_graphics.view()->Camera()->Direction(),1e-12));const double scale=v->m_graphics.view()->Camera()->Scale();drag(v,Qt::MiddleButton,Qt::ShiftModifier,{450,300},{450,240});QVERIFY(v->m_graphics.view()->Camera()->Scale()<scale);QCOMPARE(changed.count(),0);
        // Continue creating on the same 2D plane while the camera is oblique.
        v->setSketchTool(EntityKind::Point);const auto pixel=project(v,sketchPoint(*v->m_sketch,{10,15}));QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,pixel);QCOMPARE(v->m_sketch->entities.size(),std::size_t(2));const auto& point=v->m_sketch->entities.back();QVERIFY(std::hypot(point.first.x-10,point.first.y-15)<0.7);QCOMPARE(changed.count(),1);
        const auto count=v->m_sketch->entities.size();drag(v,Qt::MiddleButton,Qt::NoModifier,{350,250},{430,320});QCOMPARE(v->m_sketch->entities.size(),count);QCOMPARE(changed.count(),1);action(w,"actionNormalTo");QVERIFY(v->m_graphics.view()->Camera()->Direction().Dot(frame.Direction())< -0.999999);
        v->m_graphics.view()->SetProj(1,0,0);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,{400,300});QCOMPARE(v->m_sketch->entities.size(),count);action(w,"actionNormalTo");
        const auto eye=v->m_graphics.view()->Camera()->Eye(),target=v->m_graphics.view()->Camera()->Center();drag(v,Qt::LeftButton,Qt::NoModifier,{400,300},{480,330});QVERIFY(eye.Distance(v->m_graphics.view()->Camera()->Eye())<1e-10);QVERIFY(target.Distance(v->m_graphics.view()->Camera()->Center())<1e-10);QVERIFY(w.errors.isEmpty());w.setWindowModified(false);w.close();
    }
    void wheelDirectionAndAnchoring_data(){
        QTest::addColumn<bool>("perspective");QTest::addColumn<bool>("sketch");QTest::addColumn<bool>("pixelDelta");
        for(bool perspective:{false,true})for(bool sketch:{false,true})for(bool pixelDelta:{false,true}){
            const auto name=QString("%1-%2-%3").arg(perspective?"perspective":"orthographic",sketch?"sketch":"part",pixelDelta?"pixel":"angle");
            QTest::newRow(qPrintable(name))<<perspective<<sketch<<pixelDelta;
        }
    }
    void wheelDirectionAndAnchoring(){
        QFETCH(bool,perspective);QFETCH(bool,sketch);QFETCH(bool,pixelDelta);
        PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());
        v->setShape(makeBox(60,40,20));v->resetView();
        if(sketch){Sketch s;s.entities={{EntityKind::Line,{15,12},{35,12},1,"line"}};v->beginSketch(s);v->selectSketchTool();}
        const auto camera=v->m_graphics.view()->Camera();const gp_Pnt fixed(15,12,sketch?0:20);
        v->m_graphics.view()->SetAt(30,20,fixed.Z());v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);camera->SetScale(140);camera->SetProjectionType(perspective?Graphic3d_Camera::Projection_Perspective:Graphic3d_Camera::Projection_Orthographic);
        QSignalSpy changed(v,&CadViewport::sketchChanged);Handle(Graphic3d_Camera) original=new Graphic3d_Camera(camera);
        const auto wheel=[&](QPoint position,int direction){QWheelEvent event(position,v->mapToGlobal(position),pixelDelta?QPoint(0,40*direction):QPoint(),pixelDelta?QPoint():QPoint(0,120*direction),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(v,&event);};
        // Up zooms out without moving the target, regardless of cursor location.
        wheel({v->width()/5,v->height()/4},1);QVERIFY(camera->Scale()>original->Scale());QVERIFY(camera->Center().Distance(original->Center())<1e-10);
        Handle(Graphic3d_Camera) first=new Graphic3d_Camera(camera);camera->Copy(original);
        wheel({v->width()*4/5,v->height()*3/4},1);QVERIFY(camera->Eye().Distance(first->Eye())<1e-10);QVERIFY(camera->Center().Distance(first->Center())<1e-10);QCOMPARE(camera->Scale(),first->Scale());
        // Down zooms in while keeping the model point under the cursor fixed.
        camera->Copy(original);const auto cursor=project(v,fixed);QVERIFY((cursor-v->rect().center()).manhattanLength()>20);wheel(cursor,-1);
        QVERIFY(camera->Scale()<original->Scale());QVERIFY((project(v,fixed)-cursor).manhattanLength()<3);QVERIFY(camera->Direction().IsEqual(original->Direction(),1e-12));QVERIFY(camera->Up().IsEqual(original->Up(),1e-12));QCOMPARE(camera->ProjectionType(),original->ProjectionType());
        const auto scale=camera->Scale();const auto eye=camera->Eye();wheel(cursor,0);QCOMPARE(camera->Scale(),scale);QVERIFY(camera->Eye().Distance(eye)<1e-10);
        QCOMPARE(changed.count(),0);if(sketch){QCOMPARE(v->m_sketch->entities.size(),std::size_t(1));QCOMPARE(v->m_sketch->entities.front().first.x,15.0);}
        QVERIFY(w.errors.isEmpty());w.setWindowModified(false);w.close();
    }
    void cursorZoomAndPickedPivot(){
        PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());v->setShape(makeBox(60,40,20));v->resetView();const gp_Pnt vertex(60,0,20);const auto pixel=project(v,vertex);QTest::mouseClick(v,Qt::MiddleButton,Qt::NoModifier,pixel);QVERIFY(v->m_camera.anchor());QVERIFY(v->m_camera.anchor()->Distance(vertex)<1e-6);const auto anchor=project(v,vertex);drag(v,Qt::MiddleButton,Qt::NoModifier,{8,8},{70,45});QVERIFY((project(v,vertex)-anchor).manhattanLength()<3);
        v->m_graphics.view()->SetAt(0,0,0);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(150);const gp_Pnt fixed(15,12,0);const auto cursor=project(v,fixed);QWheelEvent wheel(cursor,v->mapToGlobal(cursor),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(v,&wheel);QVERIFY((project(v,fixed)-cursor).manhattanLength()<3);w.setWindowModified(false);w.close();
    }
    void perspectiveZoomKeepsCursorAnchor(){
        PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());
        v->setShape(makeBox(60,40,20));v->resetView();v->m_graphics.view()->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);const gp_Pnt point(25,15,20);const auto cursor=project(v,point);
        QWheelEvent wheel(cursor,v->mapToGlobal(cursor),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(v,&wheel);QVERIFY((project(v,point)-cursor).manhattanLength()<3);
        drag(v,Qt::MiddleButton,Qt::ShiftModifier,cursor,cursor-QPoint(0,30));QVERIFY((project(v,point)-cursor).manhattanLength()<3);QCOMPARE(v->m_graphics.view()->Camera()->ProjectionType(),Graphic3d_Camera::Projection_Perspective);w.setWindowModified(false);w.close();
    }
    void hoverOutlineAndBlueFillOnGrayModel(){
        PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());
        v->setShape(makeBox(60,40,20));v->resetView();
        v->m_graphics.view()->SetAt(30,20,20);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(100);
        Quantity_Color base;v->m_presentation->Color(base);QVERIFY(base.Distance(modelGray())<1e-8);
        const auto pixel=project(v,{25,15,20});const auto native=v->nativePosition(pixel);
        const auto rendered=[&](){Image_PixMap image;if(!v->m_graphics.view()->ToPixMap(image,qRound(v->width()*v->devicePixelRatioF()),qRound(v->height()*v->devicePixelRatioF())))return Quantity_Color(Quantity_NOC_RED);return image.PixelColor(native.x(),native.y(),true).GetRGB();};
        const auto gray=rendered();QVERIFY(std::abs(gray.Red()-gray.Green())<0.02);QVERIFY(std::abs(gray.Red()-gray.Blue())<0.02);QVERIFY(gray.Red()>0.3);
        const auto eye=v->m_graphics.view()->Camera()->Eye(),target=v->m_graphics.view()->Camera()->Center();const auto up=v->m_graphics.view()->Camera()->Up();const auto scale=v->m_graphics.view()->Camera()->Scale();const auto projection=v->m_graphics.view()->Camera()->ProjectionType();
        QMouseEvent hover(QEvent::MouseMove,pixel,v->mapToGlobal(pixel),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(v,&hover);
        QVERIFY(!v->m_selectionHover.IsNull());Quantity_Color border;v->m_selectionHover->Color(border);QVERIFY(border.Distance(hoverOrange())<1e-8);
        // Check the rendered interior too: orange hover must never fill the face.
        QVERIFY(rendered().Distance(gray)<0.02);
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,pixel);QVERIFY(rendered().Distance(selectionBlue())<0.02);
        QApplication::sendEvent(v,&hover);QVERIFY(v->m_selectionHover.IsNull());QVERIFY(rendered().Distance(selectionBlue())<0.02);
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,{8,8});QVERIFY(v->m_selectionPresentations.empty());QVERIFY(rendered().Distance(gray)<0.02);
        QVERIFY(eye.Distance(v->m_graphics.view()->Camera()->Eye())<1e-10);QVERIFY(target.Distance(v->m_graphics.view()->Camera()->Center())<1e-10);QVERIFY(up.IsEqual(v->m_graphics.view()->Camera()->Up(),1e-12));QCOMPARE(v->m_graphics.view()->Camera()->Scale(),scale);QCOMPARE(v->m_graphics.view()->Camera()->ProjectionType(),projection);
        // Reference-plane hover changes only the border. Selection has precedence.
        Handle(AIS_Shape) plane=new AIS_Shape(referencePlaneFace(SketchPlane::XY,50));styleReferencePlane(plane,false,false);Quantity_Color neutral;plane->Color(neutral);
        styleReferencePlane(plane,false,true);Quantity_Color hovered;plane->Color(hovered);QVERIFY(hovered.Distance(neutral)<1e-8);QVERIFY(plane->Attributes()->FaceBoundaryAspect()->Aspect()->Color().Distance(hoverOrange())<1e-8);
        styleReferencePlane(plane,true,true);Quantity_Color selected;plane->Color(selected);QVERIFY(selected.Distance(neutral)<1e-8);QVERIFY(plane->Attributes()->FaceBoundaryAspect()->Aspect()->Color().Distance(selectionBlue())<1e-8);QCOMPARE(plane->Attributes()->FaceBoundaryAspect()->Aspect()->Width(),3.0f);QVERIFY(plane->Transparency()>0.9);
        QVERIFY(w.errors.isEmpty());w.setWindowModified(false);w.close();
    }
    void selectionPlanesAndAutomaticMidplane(){
        QTemporaryDir dir;auto doc=document::PartDocument::createBox({60,40,20});const auto path=dir.filePath("plane-presentation.thepart");doc.save(path);PlaneWindow w;w.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(w.openPartFile(path));auto* v=w.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());v->m_graphics.view()->SetAt(30,20,20);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(100);v->m_graphics.view()->Redraw();const auto eye=v->m_graphics.view()->Camera()->Eye();const auto pixel=project(v,{25,15,20});QMouseEvent hover(QEvent::MouseMove,pixel,v->mapToGlobal(pixel),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(v,&hover);QVERIFY(!v->m_selectionHover.IsNull());QVERIFY(v->m_selectionHover->Shape().ShapeType()!=TopAbs_FACE);QCOMPARE(v->m_selectionHover->DisplayMode(),int(AIS_WireFrame));QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,pixel);QVERIFY(!v->m_selectionPresentations.empty());const auto highlight=v->m_selectionPresentations.front();QCOMPARE(highlight->Transparency(),0.0);Quantity_Color color;highlight->Color(color);checkBlue(color);QVERIFY(eye.Distance(v->m_graphics.view()->Camera()->Eye())<1e-10);
        v->clearSelection();select(w,"origin:XY");action(w,"actionReferencePlane");auto* constraint=w.findChild<QComboBox*>("planeConstraint");constraint->setCurrentIndex(1);confirm(w);const auto first=w.document().features().back().id;QVERIFY(std::abs(w.document().features().back().reference.value)<1e-12);
        select(w,QString::fromStdString(first));action(w,"actionReferencePlane");w.findChild<QDoubleSpinBox*>("datumValue")->setValue(20);confirm(w);const auto second=w.document().features().back().id;
        select(w,QString::fromStdString(first));auto* tree=w.findChild<QTreeWidget*>("featureTree");for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==second)(*i)->setSelected(true);action(w,"actionReferencePlane");QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::PlaneMid));confirm(w);QVERIFY(std::abs(w.document().features().back().datum.frame.Location().Z()-10)<1e-9);
        for(std::size_t i=0;i<v->m_datums.size();++i)if(v->m_datums[i].geometry.kind==DatumKind::Plane){auto plane=Handle(AIS_Shape)::DownCast(v->m_datumPresentations[i*2]);QVERIFY(plane->Transparency()>0.9);QCOMPARE(plane->Attributes()->ShadingAspect()->Aspect()->ShadingModel(),Graphic3d_TOSM_UNLIT);}
        QVERIFY(w.errors.isEmpty());w.setWindowModified(false);w.close();
    }
    void checkBlue(const Quantity_Color& c){double r,g,b;c.Values(r,g,b,Quantity_TOC_sRGB);QCOMPARE(qRound(r*255),6);QCOMPARE(qRound(g*255),180);QCOMPARE(qRound(b*255),255);}
};
}
QTEST_MAIN(thecad::ui::PlaneNavigationTest)
#include "PlaneNavigationTests.moc"
