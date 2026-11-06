#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/SketchTools.h"
#include "modeling/DimensionAnnotation.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Graphic3d_Camera.hxx>
#include <TopoDS.hxx>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>

namespace thecad::ui {
class SelectionWindow:public MainWindow {
public:QString path;QStringList errors;
protected:QString chooseSavePath(bool) override{return path;}QString chooseOpenPath() override{return path;}UnsavedChoice confirmUnsaved() override{return UnsavedChoice::Discard;}void reportError(const QString& e) override{errors<<e;}
};
class SelectionWorkflowTest:public QObject {
    Q_OBJECT
    QPoint project(CadViewport* v,gp_Pnt p){v->synchronizeSurface();int x,y;v->m_graphics.view()->Convert(p.X(),p.Y(),p.Z(),x,y);return {qRound(x/v->devicePixelRatioF()),qRound(y/v->devicePixelRatioF())};}
    void command(SelectionWindow& w,const char* name){auto* a=w.findChild<QAction*>(name);QVERIFY2(a&&a->isEnabled(),name);a->trigger();QApplication::processEvents();}
    void treeSelect(SelectionWindow& w,const std::string& id){auto* tree=w.findChild<QTreeWidget*>("featureTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){tree->setCurrentItem(*it);return;}QFAIL("Feature not found in tree");}
    void hover(CadViewport* v,QPoint p){QMouseEvent e(QEvent::MouseMove,p,v->mapToGlobal(p),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(v,&e);}
private slots:
    void overlappingOriginPlanesAndAnnotations(){
        using namespace modeling;SelectionWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* v=window.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());
        const auto origin=project(v,{0,0,0});auto candidates=v->selectionCandidates(origin,true);
        QCOMPARE(std::count_if(candidates.begin(),candidates.end(),[](const auto& c){return c.value.reference.kind==SelectionKind::OriginPlane;}),3);
        v->showSelectOther(origin);auto* menu=v->m_selectOtherMenu.data();QVERIFY(menu);QAction* top=nullptr;for(auto* action:menu->actions())if(action->text().contains("Top")){top=action;break;}QVERIFY(top);menu->setActiveAction(top);QTest::keyClick(menu,Qt::Key_Return);QCOMPARE(v->selection().size(),std::size_t(1));QCOMPARE(v->selection()[0].index,int(SketchPlane::XZ));
        Sketch sketch;sketch.entities={{EntityKind::Line,{-20,10},{20,10},1,"first"},{EntityKind::Line,{-20,10},{20,10},1,"second"}};
        for(const auto& id:{std::string("first"),std::string("second")}){SketchDimension d;d.id=id+"-length";d.kind=DimensionKind::LineLength;d.first={id,PointRole::First};d.second={id,PointRole::Second};d.value=40;placeDimensionAnnotation(sketch,d,{0,25});sketch=setSketchDimension(sketch,d);}
        v->beginSketch(sketch);v->selectSketchTool();const auto text=dimensionGraphics(*v->m_sketch,v->m_sketch->dimensions[0],v->m_graphics.view()->Camera()->Scale()/v->height()).text;const auto label=v->screenPoint(text).toPoint()+QPoint(8,-3);candidates=v->selectionCandidates(label,true);
        QCOMPARE(std::count_if(candidates.begin(),candidates.end(),[](const auto& c){return c.value.reference.kind==SelectionKind::Dimension;}),2);
        v->showSelectOther(label);menu=v->m_selectOtherMenu.data();QVERIFY(menu);QAction* second=nullptr;for(auto* action:menu->actions())if(action->text().contains("second-length")){second=action;break;}QVERIFY(second);menu->setActiveAction(second);QTest::keyClick(menu,Qt::Key_Return);QCOMPARE(v->selectedSketchDimensions(),std::vector<std::string>{"second-length"});window.setWindowModified(false);window.close();
    }
    void topologyFiltersOcclusionAndSelectOther(){
        using namespace modeling;QTemporaryDir temporary;auto doc=document::PartDocument::createEmpty();Sketch profile;profile.entities={{EntityKind::Rectangle,{-40,-30},{40,30},1,"outline"}};auto id=doc.addSketch(profile);doc.addExtrude(id,{"outline"},20,Direction::Positive);Sketch hole;hole.entities={{EntityKind::Circle,{0,0},{},8,"hole"}};id=doc.addSketch(hole);doc.addCut(id,{"hole"},20,true,Direction::Positive);
        SelectionWindow window;window.path=temporary.filePath("hole.thepart");doc.save(window.path);window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));QVERIFY(window.openPartFile(window.path));auto* v=window.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());
        v->m_graphics.view()->SetAt(0,0,20);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(100);v->m_graphics.view()->Redraw();
        const auto face=project(v,{20,10,20}),edge=project(v,{0,30,20}),vertex=project(v,{40,30,20}),rim=project(v,{8,0,20});
        hover(v,rim);QVERIFY(!v->m_selectionHover.IsNull());QCOMPARE(v->m_selectionHover->Shape().ShapeType(),TopAbs_EDGE);QVERIFY(!window.isWindowModified());
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,face);QCOMPARE(v->selection().size(),std::size_t(1));QCOMPARE(v->selection()[0].kind,SelectionKind::Face);QCOMPARE(v->m_graphics.context()->NbSelected(),1);
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,rim);QCOMPARE(v->selection()[0].kind,SelectionKind::Edge);QVERIFY(!window.findChild<QAction*>("actionCreateSketch")->isEnabled());
        QTest::mouseClick(v,Qt::LeftButton,Qt::ShiftModifier,edge);QCOMPARE(v->selection().size(),std::size_t(2));QCOMPARE(v->m_graphics.context()->NbSelected(),2);
        QTest::mouseClick(v,Qt::LeftButton,Qt::ShiftModifier,edge);QCOMPARE(v->selection().size(),std::size_t(1));
        v->setFocus();QTest::keyClick(v,Qt::Key_F5);QTRY_VERIFY(window.findChild<QToolBar*>("selectionFilterToolbar")->isVisible());command(window,"selectionFilter2");QCOMPARE(v->selectionPolicy().filters,std::uint32_t(FilterEdges));
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,face);QVERIFY(v->selection().empty());
        for(double scale:{80.0,300.0}){v->m_graphics.view()->Camera()->SetScale(scale);const auto e=project(v,{0,30,20});QVERIFY(v->rect().contains(e+QPoint(0,-7)));QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,e+QPoint(0,-7));QCOMPARE(v->selection().size(),std::size_t(1));QCOMPARE(v->selection()[0].kind,SelectionKind::Edge);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,e+QPoint(0,-11));QVERIFY(v->selection().empty());}
        v->m_graphics.view()->Camera()->SetScale(100);v->setFocus();QTest::keyClick(v,Qt::Key_F6);QVERIFY(!v->selectionPolicy().enabled);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{20,10,20}));QVERIFY(!v->selection().empty());QCOMPARE(v->selection()[0].kind,SelectionKind::Face);QTest::keyClick(v,Qt::Key_F6);QVERIFY(v->selectionPolicy().enabled);command(window,"actionClearSelectionFilters");
        v->setSelectionFilters(FilterVertices);const auto corner=project(v,{40,30,20});QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,corner+QPoint(3,-2));QVERIFY(!v->selection().empty());QCOMPARE(v->selection()[0].kind,SelectionKind::Vertex);
        const auto alternatives=v->selectionCandidates(corner,true);QVERIFY(std::any_of(alternatives.begin(),alternatives.end(),[](const auto& c){return c.value.occluded;}));
        const auto visible=v->selectionCandidates(corner);QVERIFY(!visible.empty());for(const auto& c:visible){QVERIFY(!c.value.occluded);QVERIFY(BRep_Tool::Pnt(TopoDS::Vertex(c.shape)).Z()>19.9);}
        const auto prior=v->selection();v->showSelectOther(corner);QVERIFY(v->m_selectOtherMenu);const auto first=v->m_selectOtherMenu->activeAction();QTest::keyClick(v->m_selectOtherMenu,Qt::Key_Tab);QVERIFY(v->m_selectOtherMenu->activeAction()!=first);QTest::keyClick(v->m_selectOtherMenu,Qt::Key_Escape);QCOMPARE(v->selection(),prior);QVERIFY(!window.isWindowModified());
        v->showSelectOther(corner);auto* other=v->m_selectOtherMenu.data();QAction* behind=nullptr;for(auto* action:other->actions())if(action->text().contains("behind")){behind=action;break;}QVERIFY(behind);other->setActiveAction(behind);QTest::keyClick(other,Qt::Key_Return);QCOMPARE(v->selection().size(),std::size_t(1));QCOMPARE(v->selection()[0].kind,SelectionKind::Vertex);QVERIFY(BRep_Tool::Pnt(TopoDS::Vertex(v->m_selectionVertices(v->selection()[0].index))).Z()<0.1);
        v->setSelectionFilters(FilterBodies);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{20,10,20}));QVERIFY(!v->selection().empty());QCOMPARE(v->selection()[0].kind,SelectionKind::Body);
        v->setSelectionFilters(FilterFaces);v->m_graphics.view()->SetProj(0,-1,2);v->m_graphics.view()->SetUp(0,0,1);v->m_graphics.view()->Camera()->SetScale(100);const auto wall=project(v,{0,8,10});
        const auto wallHits=v->selectionCandidates(wall);QVERIFY(!wallHits.empty());QCOMPARE(BRepAdaptor_Surface(TopoDS::Face(wallHits.front().shape)).GetType(),GeomAbs_Cylinder);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,wall);QCOMPARE(v->selection()[0].kind,SelectionKind::Face);QVERIFY(!window.findChild<QAction*>("actionCreateSketch")->isEnabled());QVERIFY(window.errors.isEmpty());
        v->setSelectionFilters(0);command(window,"actionReferenceAxis");auto* method=window.findChild<QComboBox*>("datumMethod");QCOMPARE(method->currentData().toInt(),int(DatumMethod::AxisOnReference));QVERIFY(window.findChild<QComboBox*>("datumReference1")->currentIndex()>=0);auto* buttons=window.findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCOMPARE(window.document().features().back().datum.kind,DatumKind::Axis);
        const auto stale=alternatives.front();v->setShape(window.document().shape());v->clearSelection();v->selectCandidate(stale);QVERIFY(v->selection().empty());
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,QPoint(4,4));QVERIFY(v->selection().empty());QVERIFY(window.errors.isEmpty());window.setWindowModified(false);window.close();
    }
    void constructionControlsAndReferenceVisibility(){
        using namespace modeling;QTemporaryDir temporary;auto doc=document::PartDocument::createEmpty();Sketch sketch;sketch.entities={{EntityKind::Line,{-20,10},{20,10},1,"line"},{EntityKind::Circle,{10,-10},{},6,"circle"}};sketch.relations={{"horizontal",RelationKind::Horizontal,{"line",PointRole::First},{"line",PointRole::Second}}};SketchDimension radius;radius.id="radius";radius.kind=DimensionKind::CircleRadius;radius.first={"circle",PointRole::Center};radius.value=6;sketch=setSketchDimension(sketch,radius);const auto id=doc.addSketch(sketch);
        DatumDefinition definition;definition.method=DatumMethod::PlaneOffset;GeometryReference base;base.source=ReferenceSource::BasePlane;base.index=0;definition.references={base};definition.value=10;const auto planeId=doc.addReferenceGeometry(definition);
        SelectionWindow window;window.path=temporary.filePath("construction.thepart");doc.save(window.path);window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));QVERIFY(window.openPartFile(window.path));auto* v=window.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());treeSelect(window,id);command(window,"actionEditFeature");v->selectSketchTool();
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,v->screenPoint({0,10}).toPoint());auto* checkbox=window.findChild<QCheckBox*>("forConstruction");QVERIFY(checkbox&&checkbox->isVisible());QCOMPARE(checkbox->checkState(),Qt::Unchecked);QTest::mouseClick(checkbox,Qt::LeftButton,Qt::NoModifier,QPoint(8,checkbox->height()/2));QVERIFY(v->m_sketch->entities[0].construction);
        QTest::mouseClick(v,Qt::LeftButton,Qt::ShiftModifier,v->screenPoint({16,-10}).toPoint());QCOMPARE(v->selection().size(),std::size_t(2));QCOMPARE(checkbox->checkState(),Qt::PartiallyChecked);QTest::mouseClick(checkbox,Qt::LeftButton,Qt::NoModifier,QPoint(8,checkbox->height()/2));QVERIFY(v->m_sketch->entities[0].construction&&v->m_sketch->entities[1].construction);QVERIFY(closedContours(*v->m_sketch).empty());
        command(window,"actionUndo");QCOMPARE(checkbox->checkState(),Qt::PartiallyChecked);command(window,"actionRedo");QCOMPARE(checkbox->checkState(),Qt::Checked);QTest::mouseClick(checkbox,Qt::LeftButton,Qt::NoModifier,QPoint(8,checkbox->height()/2));QCOMPARE(checkbox->checkState(),Qt::Unchecked);QCOMPARE(closedContours(*v->m_sketch).size(),std::size_t(1));QCOMPARE(v->m_sketch->dimensions[0].id,std::string("radius"));QCOMPARE(v->m_sketch->relations[0].id,std::string("horizontal"));
        command(window,"actionFinishSketch");treeSelect(window,planeId);QCOMPARE(v->selection()[0].kind,SelectionKind::ReferencePlane);command(window,"actionReferenceVisibility");QVERIFY(!window.document().features().back().referenceVisible);QVERIFY(!v->m_graphics.context()->IsDisplayed(v->m_datumPresentations[0]));
        command(window,"actionUndo");QVERIFY(window.document().features().back().referenceVisible);command(window,"actionRedo");QVERIFY(!window.document().features().back().referenceVisible);command(window,"actionSave");QVERIFY(!document::PartDocument::load(window.path).features().back().referenceVisible);
        treeSelect(window,planeId);command(window,"actionReferenceVisibility");QVERIFY(window.document().features().back().referenceVisible);v->setSelectionFilters(FilterReferences);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->SetAt(0,0,10);v->m_graphics.view()->Camera()->SetScale(120);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{20,20,10}));QCOMPARE(v->selection()[0].kind,SelectionKind::ReferencePlane);
        QVERIFY(window.errors.isEmpty());window.setWindowModified(false);window.close();
    }
    void referencePointsAxesAndThreePointPreselection(){
        using namespace modeling;QTemporaryDir temporary;auto doc=document::PartDocument::createEmpty();std::vector<std::string> ids;
        for(const auto point:{gp_Pnt(-20,-15,10),gp_Pnt(20,15,10),gp_Pnt(-20,15,10)}){DatumDefinition d;d.position=point;ids.push_back(doc.addReferenceGeometry(d));}
        const auto reference=[](const std::string& id){GeometryReference r;r.source=ReferenceSource::Datum;r.featureId=id;return r;};DatumDefinition axis;axis.method=DatumMethod::AxisTwoPoints;axis.references={reference(ids[0]),reference(ids[1])};const auto axisId=doc.addReferenceGeometry(axis);
        SelectionWindow window;window.path=temporary.filePath("references.thepart");doc.save(window.path);window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));QVERIFY(window.openPartFile(window.path));auto* v=window.findChild<CadViewport*>();QTRY_VERIFY(v->isReady());v->setSelectionFilters(FilterReferences);v->m_graphics.view()->SetAt(0,0,10);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(120);
        QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{-20,-15,10}));QCOMPARE(v->selection().size(),std::size_t(1));QCOMPARE(v->selection()[0].kind,SelectionKind::ReferencePoint);QCOMPARE(v->selection()[0].featureId,ids[0]);
        QTest::mouseClick(v,Qt::LeftButton,Qt::ShiftModifier,project(v,{0,0,10}));QCOMPARE(v->selection().size(),std::size_t(2));QCOMPARE(v->selection().back().kind,SelectionKind::ReferenceAxis);QCOMPARE(v->selection().back().featureId,axisId);
        v->clearSelection();for(const auto point:{gp_Pnt(-20,-15,10),gp_Pnt(20,15,10),gp_Pnt(-20,15,10)})QTest::mouseClick(v,Qt::LeftButton,Qt::ShiftModifier,project(v,point));QCOMPARE(v->selection().size(),std::size_t(3));command(window,"actionReferencePlane");auto* method=window.findChild<QComboBox*>("datumMethod");QCOMPARE(method->currentData().toInt(),int(DatumMethod::PlaneThreePoints));for(int i=1;i<=3;++i)QVERIFY(window.findChild<QComboBox*>(QString("datumReference%1").arg(i))->currentIndex()>=0);auto* buttons=window.findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCOMPARE(window.document().features().back().datum.kind,DatumKind::Plane);QCOMPARE(v->selection().size(),std::size_t(1));QCOMPARE(v->selection()[0].kind,SelectionKind::ReferencePlane);
        QVERIFY(window.errors.isEmpty());window.setWindowModified(false);window.close();
    }
};
}
QTEST_MAIN(thecad::ui::SelectionWorkflowTest)
#include "SelectionWorkflowTests.moc"
