#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/Theme.h"
#include <Graphic3d_Camera.hxx>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
namespace thecad::ui {
using namespace modeling;
using document::PartDocument;
class ReferenceWindow:public MainWindow {
public:
    QStringList errors;
    QString savePath;
    bool allowDeletion=false;
    bool realDeletionDialog=false;
    std::vector<document::Feature> askedDependents;
protected:
    QString chooseSavePath(bool) override{return savePath;}
    UnsavedChoice confirmUnsaved() override{return UnsavedChoice::Discard;}
    void reportError(const QString& error) override{errors<<error;}
    bool confirmFeatureDeletion(const std::vector<std::string>& ids,const std::vector<document::Feature>& dependents) override{askedDependents=dependents;if(realDeletionDialog)return MainWindow::confirmFeatureDeletion(ids,dependents);return allowDeletion;}
};
class ReferenceManagerTest:public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    struct CameraState {
        gp_Pnt eye,target;gp_Dir up;double scale;Graphic3d_Camera::Projection projection;
        explicit CameraState(CadViewport* v):eye(v->m_graphics.view()->Camera()->Eye()),target(v->m_graphics.view()->Camera()->Center()),up(v->m_graphics.view()->Camera()->Up()),scale(v->m_graphics.view()->Camera()->Scale()),projection(v->m_graphics.view()->Camera()->ProjectionType()){}
        bool unchanged(CadViewport* v)const{const auto c=v->m_graphics.view()->Camera();return eye.Distance(c->Eye())<1e-9&&target.Distance(c->Center())<1e-9&&gp_Vec(up).Subtracted(gp_Vec(c->Up())).Magnitude()<1e-12&&std::abs(scale-c->Scale())<1e-9&&projection==c->ProjectionType();}
    };
    void command(ReferenceWindow& w,const char* name){auto* action=w.findChild<QAction*>(name);QVERIFY2(action&&action->isEnabled(),name);action->trigger();QApplication::processEvents();}
    QTreeWidgetItem* item(ReferenceWindow& w,const std::string& id){auto* tree=w.findChild<QTreeWidget*>("featureTree");for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id)return *i;return nullptr;}
    void select(ReferenceWindow& w,const std::string& id){auto* node=item(w,id);QVERIFY(node);w.findChild<QTreeWidget*>("featureTree")->setCurrentItem(node,0,QItemSelectionModel::ClearAndSelect);QApplication::processEvents();}
    CadViewport* show(ReferenceWindow& w){w.showMaximized();if(!QTest::qWaitForWindowExposed(&w))return nullptr;auto* v=w.findChild<CadViewport*>();for(int i=0;i<50&&!v->isReady();++i)QTest::qWait(10);return v->isReady()?v:nullptr;}
    QPoint project(CadViewport* v,gp_Pnt p){v->synchronizeSurface();int x,y;v->m_graphics.view()->Convert(p.X(),p.Y(),p.Z(),x,y);return {qRound(x/v->devicePixelRatioF()),qRound(y/v->devicePixelRatioF())};}
    void confirm(ReferenceWindow& w){auto* panel=w.findChild<QWidget*>("referenceGeometryPanel");QVERIFY(panel);auto* b=panel->findChild<QDialogButtonBox*>("referenceGeometryButtons")->button(QDialogButtonBox::Ok);QVERIFY(b->isEnabled());QTest::mouseClick(b,Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    static GeometryReference datum(std::string id){GeometryReference r;r.source=ReferenceSource::Datum;r.featureId=std::move(id);return r;}
    static std::string xyz(PartDocument& doc,DatumKind kind,gp_Pnt position){DatumDefinition d;d.method=kind==DatumKind::Point?DatumMethod::PointXYZ:kind==DatumKind::Axis?DatumMethod::AxisXYZ:DatumMethod::PlaneXYZ;d.position=position;return doc.addReferenceGeometry(d);}
private slots:
    void initTestCase(){QVERIFY(settings.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());}
    void measureUpdatesFromViewportPicksWithoutChangingPart()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("measure.thepart");
        PartDocument::createBox({60, 50, 20}).save(path);
        ReferenceWindow window;
        auto* viewport = show(window);
        QVERIFY(viewport);
        QVERIFY(window.openPartFile(path));
        auto* view = viewport->m_graphics.view().get();
        view->SetAt(30, 25, 10);
        view->SetProj(1, -2, 3);
        view->SetUp(0, 0, 1);
        view->Camera()->SetScale(120);
        view->Redraw();

        viewport->setSelectionFilters(FilterEdges);
        const auto firstEdge = project(viewport, {30, 0, 20});
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, firstEdge);
        QCOMPARE(viewport->selection().size(), std::size_t(1));
        QCOMPARE(viewport->selection().front().kind, SelectionKind::Edge);
        const CameraState camera(viewport);
        auto* measure = window.findChild<QAction*>("actionMeasure");
        auto* readout = window.findChild<QLabel*>("measureReadout");
        QVERIFY(measure && measure->isEnabled() && readout && readout->isHidden());
        auto* tabs = window.findChild<QTabWidget*>("modelingTabs");
        tabs->setCurrentIndex(2);
        QToolButton* button = nullptr;
        for (auto* candidate : tabs->findChildren<QToolButton*>())
            if (candidate->defaultAction() == measure)
                button = candidate;
        QVERIFY(button && button->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(measure->isChecked() && readout->isVisible());
        const auto number = [](double value) { return QLocale().toString(value, 'f', 3); };
        QVERIFY(readout->text().contains("Length  " + number(60)));
        QVERIFY(!viewport->m_measureLabel.IsNull());

        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, project(viewport, {60, 25, 20}));
        QCOMPARE(viewport->selection().size(), std::size_t(2));
        QVERIFY(readout->text().contains("Angle  " + number(90)));
        QVERIFY(readout->text().contains("Minimum distance  " + number(0)));
        QVERIFY(viewport->m_measureSpan.IsNull());

        viewport->setSelectionFilters(FilterVertices);
        const auto left = project(viewport, {0, 0, 20});
        const auto right = project(viewport, {60, 0, 20});
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, left);
        QCOMPARE(viewport->selection().size(), std::size_t(1));
        QVERIFY(readout->text().contains("Z  " + number(20)));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, right);
        QVERIFY(readout->text().contains("Minimum distance  " + number(60)));
        QVERIFY(!viewport->m_measureSpan.IsNull());
        QVERIFY(camera.unchanged(viewport));

        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, right);
        QCOMPARE(viewport->selection().size(), std::size_t(1));
        QVERIFY(viewport->m_measureSpan.IsNull());
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, {5, 5});
        QVERIFY(viewport->selection().empty());
        QVERIFY(viewport->m_measureLabel.IsNull());
        viewport->setSelectionFilters(FilterFaces);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, project(viewport, {20, 20, 20}));
        QCOMPARE(viewport->selection().size(), std::size_t(1));
        QCOMPARE(viewport->selection().front().kind, SelectionKind::Face);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, project(viewport, {30, 0, 10}));
        QCOMPARE(viewport->selection().size(), std::size_t(2));
        QVERIFY(readout->text().contains("Angle  " + number(90)));
        QVERIFY(!viewport->m_measureLabel.IsNull());
        QTest::keyClick(viewport, Qt::Key_Escape);
        QVERIFY(!measure->isChecked() && readout->isHidden());
        QVERIFY(viewport->m_measureLabel.IsNull());
        QVERIFY(camera.unchanged(viewport));
        QVERIFY(!window.isWindowModified());
        QCOMPARE(window.document().features().size(), std::size_t(1));
        QVERIFY(window.errors.isEmpty());
    }

    void referenceCommandsKeepBasePlaneVisibility()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("base-planes.thepart");
        PartDocument::createBox({60, 50, 20}).save(path);
        ReferenceWindow window;
        auto* viewport = show(window);
        QVERIFY(viewport);
        QVERIFY(window.openPartFile(path));
        QVERIFY(!viewport->referencePlanesVisible());
        select(window, "origin:XY");
        QVERIFY(!viewport->referencePlanesVisible());

        viewport->clearSelection();
        command(window, "actionReferencePlane");
        QVERIFY(!viewport->referencePlanesVisible());
        auto* first = window.findChild<QComboBox*>("datumReference1");
        QVERIFY(first && first->currentIndex() < 0);
        window.findChild<QCheckBox*>("datumAutomatic")->setChecked(false);
        auto* view = viewport->m_graphics.view().get();
        view->SetAt(0, 0, 0);
        view->SetProj(0, 0, 1);
        view->SetUp(0, 1, 0);
        view->Camera()->SetScale(120);
        view->Redraw();
        const auto hiddenPlane = project(viewport, {-25, -30, 0});
        QVERIFY(viewport->rect().contains(hiddenPlane));
        QCOMPARE(viewport->referenceCandidateAt(hiddenPlane), -1);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, hiddenPlane);
        QCOMPARE(first->currentIndex(), -1);

        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        auto* front = item(window, "origin:XY");
        QVERIFY(tree && tree->isVisible() && tree->isEnabled() && front);
        tree->scrollToItem(front);
        const auto row = tree->visualItemRect(front);
        QVERIFY(tree->viewport()->rect().contains(row.center()));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
        QCOMPARE(first->currentText(), QString("Front plane"));
        QVERIFY(!viewport->referencePlanesVisible());
        confirm(window);
        QVERIFY(!viewport->referencePlanesVisible());
        QCOMPARE(window.document().features().back().reference.references.front().source,
                 ReferenceSource::BasePlane);

        viewport->showReferencePlanes(true);
        command(window, "actionReferenceAxis");
        QVERIFY(viewport->referencePlanesVisible());
        QTest::keyClick(viewport, Qt::Key_Escape);
        QVERIFY(viewport->referencePlanesVisible());
        QVERIFY(window.errors.isEmpty());
    }

    void cameraIsExactAcrossSelectionsReferenceEditingAndThemes(){
        QTemporaryDir dir;auto doc=PartDocument::createBox({60,50,20});const auto p=xyz(doc,DatumKind::Point,{85,15,10}),a=xyz(doc,DatumKind::Axis,{90,20,10}),pl=xyz(doc,DatumKind::Plane,{0,0,45});Sketch sketch;sketch.entities={{EntityKind::Rectangle,{5,5},{15,15},1,"profile"}};const auto sid=doc.addSketch(sketch);const auto boss=doc.addExtrude(sid,{"profile"},30,Direction::Positive);const auto path=dir.filePath("camera.thepart");doc.save(path);
        ReferenceWindow w;auto* v=show(w);QVERIFY(v);QVERIFY(w.openPartFile(path));v->m_graphics.view()->SetAt(20,15,10);v->m_graphics.view()->SetProj(1,-2,3);v->m_graphics.view()->SetUp(0,0,1);v->m_graphics.view()->Camera()->SetScale(190);v->m_graphics.view()->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);v->m_graphics.view()->Redraw();QApplication::processEvents();const CameraState camera(v);
        for(const auto& id:{doc.features()[0].id,p,a,pl,sid,boss,std::string("origin:XY"),std::string("origin:XZ"),std::string("origin:YZ"),std::string("origin:")}){select(w,id);QVERIFY2(camera.unchanged(v),id.c_str());}
        v->showReferencePlanes(false);v->setSelectionFilters(FilterFaces);const auto face=project(v,{25,15,20});QTest::mouseMove(v,face);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,face);QVERIFY(!v->selection().empty());QVERIFY(camera.unchanged(v));
        v->setSelectionFilters(FilterEdges);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{30,0,20}));QVERIFY(!v->selection().empty());QCOMPARE(v->selection()[0].kind,SelectionKind::Edge);QVERIFY(camera.unchanged(v));
        v->setSelectionFilters(FilterVertices);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{60,0,20}));QVERIFY(!v->selection().empty());QCOMPARE(v->selection()[0].kind,SelectionKind::Vertex);QVERIFY(camera.unchanged(v));v->setSelectionFilters(0);
        select(w,boss);command(w,"actionEditFeature");QVERIFY(camera.unchanged(v));w.findChild<QDoubleSpinBox*>("featureLength")->setValue(31);QVERIFY(camera.unchanged(v));QTest::keyClick(v,Qt::Key_Escape);QApplication::processEvents();QVERIFY(camera.unchanged(v));
        select(w,pl);command(w,"actionEditFeature");QVERIFY(camera.unchanged(v));auto* spin=w.findChild<QDoubleSpinBox*>("datumPositionZ");spin->setValue(48);QApplication::processEvents();QVERIFY(camera.unchanged(v));confirm(w);QVERIFY(camera.unchanged(v));
        select(w,"origin:XY");command(w,"actionReferencePlane");QVERIFY(camera.unchanged(v));w.findChild<QDoubleSpinBox*>("datumValue")->setValue(17);QVERIFY(camera.unchanged(v));QTest::keyClick(v,Qt::Key_Escape);QApplication::processEvents();QVERIFY(camera.unchanged(v));
        const auto count=w.document().features().size();const bool modified=w.isWindowModified();for(auto mode:{ThemeMode::Light,ThemeMode::Dark,ThemeMode::System,ThemeMode::Dark}){ThemeManager::instance().setMode(mode);QApplication::processEvents();QVERIFY(camera.unchanged(v));QCOMPARE(w.document().features().size(),count);QCOMPARE(w.isWindowModified(),modified);}
        QCOMPARE(QSettings("TheCAD","TheCAD").value("appearance/theme").toString(),QString("dark"));QVERIFY(qApp->palette().color(QPalette::Base).lightness()<70);QVERIFY(themeColor(ThemeColor::Defined).lightness()>200);
        QVERIFY(w.errors.isEmpty());w.setWindowModified(false);w.close();
    }
    void deleteUndoRedoMultiSelectionAndDependencyCancellation(){
        QTemporaryDir dir;auto doc=PartDocument::createEmpty();const auto p=xyz(doc,DatumKind::Point,{15,10,0}),a=xyz(doc,DatumKind::Axis,{30,0,0}),pl=xyz(doc,DatumKind::Plane,{0,0,20});const auto path=dir.filePath("delete.thepart");doc.save(path);ReferenceWindow w;auto* v=show(w);QVERIFY(v);QVERIFY(w.openPartFile(path));
        for(const auto& id:{p,a,pl}){select(w,id);v->setFocus();QTest::keyClick(v,Qt::Key_Delete);QCOMPARE(w.document().features().size(),std::size_t(2));QVERIFY(std::none_of(v->m_datums.begin(),v->m_datums.end(),[&](const auto& d){return d.id==id;}));QTest::keyClick(v,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(w.document().features().size(),std::size_t(3));QTest::keyClick(v,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(w.document().features().size(),std::size_t(2));QTest::keyClick(v,Qt::Key_Z,Qt::ControlModifier);}
        select(w,"origin:XY");QVERIFY(!w.findChild<QAction*>("actionDeleteFeatures")->isEnabled());select(w,"origin:");QVERIFY(!w.findChild<QAction*>("actionDeleteFeatures")->isEnabled());
        select(w,p);auto* tree=w.findChild<QTreeWidget*>("featureTree");item(w,a)->setSelected(true);QVERIFY(w.findChild<QAction*>("actionDeleteFeatures")->isEnabled());tree->setFocus();QTest::keyClick(tree,Qt::Key_Delete);QCOMPARE(w.document().features().size(),std::size_t(1));command(w,"actionUndo");QCOMPARE(w.document().features().size(),std::size_t(3));
        // Viewport selection uses the same deletion command as the tree.
        v->clearSelection();v->setSelectionFilters(FilterReferences);v->m_graphics.view()->SetAt(0,0,0);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(100);QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{15,10,0}));QCOMPARE(v->selection()[0].featureId,p);v->setFocus();QTest::keyClick(v,Qt::Key_Delete);QCOMPARE(w.document().features().size(),std::size_t(2));command(w,"actionUndo");
        auto dependent=doc;auto profile=dependent.sketchOnReferencePlane(pl);profile.entities={{EntityKind::Rectangle,{0,0},{15,10},1,"profile"}};const auto sid=dependent.addSketch(profile);const auto boss=dependent.addExtrude(sid,{"profile"},5,Direction::Positive);dependent.save(path);QVERIFY(w.openPartFile(path));select(w,pl);command(w,"actionDeleteFeatures");QCOMPARE(w.document().features().size(),std::size_t(5));QCOMPARE(w.askedDependents.size(),std::size_t(2));QCOMPARE(w.askedDependents[0].id,sid);QCOMPARE(w.askedDependents[1].id,boss);
        w.realDeletionDialog=true;bool sawDialog=false;QTimer::singleShot(0,&w,[&]{auto* box=w.findChild<QMessageBox*>();if(!box)return;sawDialog=box->text().contains(QString::fromStdString(sid))&&box->text().contains(QString::fromStdString(boss))&&box->defaultButton()==box->button(QMessageBox::Cancel);box->button(QMessageBox::Cancel)->click();});command(w,"actionDeleteFeatures");QVERIFY(sawDialog);QCOMPARE(w.document().features().size(),std::size_t(5));w.realDeletionDialog=false;
        w.allowDeletion=true;command(w,"actionDeleteFeatures");QCOMPARE(w.document().features().size(),std::size_t(2));QVERIFY(w.document().shape().IsNull());command(w,"actionUndo");QCOMPARE(w.document().features().size(),std::size_t(5));QVERIFY(!w.document().shape().IsNull());command(w,"actionRedo");QCOMPARE(w.document().features().size(),std::size_t(2));w.savePath=dir.filePath("deleted.thepart");command(w,"actionSaveAs");QVERIFY(w.openPartFile(w.savePath));QCOMPARE(w.document().features().size(),std::size_t(2));QVERIFY(w.errors.isEmpty());w.close();
    }
    void deleteSketchesAndSolidFeaturesFromTree()
    {
        QTemporaryDir dir;
        auto part = PartDocument::createEmpty();
        Sketch profile;
        profile.entities = {{EntityKind::Rectangle, {0, 0}, {30, 20}, 1, "profile"}};
        const auto profileId = part.addSketch(profile);
        const auto bossId = part.addExtrude(profileId, {"profile"}, 12, Direction::Positive);
        Sketch hole;
        hole.entities = {{EntityKind::Circle, {10, 10}, {}, 3, "hole"}};
        const auto holeId = part.addSketch(hole);
        const auto cutId = part.addCut(holeId, {"hole"}, 12, true, Direction::Positive);
        const auto unusedId = part.addSketch({});
        const auto path = dir.filePath("features.thepart");
        part.save(path);

        ReferenceWindow window;
        auto* viewport = show(window);
        QVERIFY(viewport);
        QVERIFY(window.openPartFile(path));
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        auto* remove = window.findChild<QAction*>("actionDeleteFeatures");
        QVERIFY(tree && remove);
        const CameraState camera(viewport);

        select(window, holeId);
        QCOMPARE(item(window, holeId)->parent(), item(window, cutId));
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Delete);
        QCOMPARE(window.askedDependents.size(), std::size_t(1));
        QCOMPARE(window.askedDependents.front().id, cutId);
        QCOMPARE(window.document().features().size(), std::size_t(5));
        QVERIFY(!window.isWindowModified());

        const auto* cutItem = item(window, cutId);
        QVERIFY(cutItem);
        const auto position = tree->visualItemRect(cutItem).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                tree->viewport()->mapToGlobal(position));
        QApplication::sendEvent(tree->viewport(), &event);
        auto* menu = window.findChild<QMenu*>("featureTreeContextMenu");
        QVERIFY(menu && menu->isVisible() && menu->actions().contains(remove));
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(remove).center());
        QCOMPARE(window.document().features().size(), std::size_t(4));
        QVERIFY(!item(window, cutId));
        QVERIFY(item(window, holeId) && item(window, holeId)->parent() == tree->topLevelItem(0));
        QVERIFY(camera.unchanged(viewport));
        command(window, "actionUndo");
        QVERIFY(item(window, cutId));
        command(window, "actionRedo");
        QVERIFY(!item(window, cutId));

        select(window, profileId);
        item(window, "origin:XY")->setSelected(true);
        QVERIFY(!remove->isEnabled());
        select(window, profileId);
        item(window, unusedId)->setSelected(true);
        window.allowDeletion = true;
        command(window, "actionDeleteFeatures");
        QCOMPARE(window.askedDependents.size(), std::size_t(1));
        QCOMPARE(window.askedDependents.front().id, bossId);
        QCOMPARE(window.document().features().size(), std::size_t(1));
        QCOMPARE(window.document().features().front().id, holeId);
        QVERIFY(viewport->displayedShape().IsNull());
        QVERIFY(!viewport->m_sketch);
        QVERIFY(camera.unchanged(viewport));

        window.savePath = dir.filePath("deleted-features.thepart");
        command(window, "actionSaveAs");
        QVERIFY(window.openPartFile(window.savePath));
        QCOMPARE(window.document().features().size(), std::size_t(1));
        select(window, holeId);
        command(window, "actionEditFeature");
        QVERIFY(!remove->isEnabled());
        command(window, "actionFinishSketch");
        select(window, holeId);
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Delete);
        QVERIFY(window.document().features().empty());
        QVERIFY(!viewport->m_sketch);
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
    }

    void inferMethodsFromTreeAndViewportAndPreserveInvalidSelections(){
        QTemporaryDir dir;auto doc=PartDocument::createEmpty();const auto p=xyz(doc,DatumKind::Point,{-20,-10,10}),q=xyz(doc,DatumKind::Point,{20,-10,10}),r=xyz(doc,DatumKind::Point,{0,20,10});const auto path=dir.filePath("methods.thepart");doc.save(path);ReferenceWindow w;auto* v=show(w);QVERIFY(v);QVERIFY(w.openPartFile(path));v->clearSelection();
        command(w,"actionReferencePlane");auto* buttons=w.findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());for(const auto& id:{p,q,r})select(w,id);QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::PlaneThreePoints));QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());confirm(w);const auto pl=w.document().features().back().id;
        select(w,pl);command(w,"actionReferencePlane");QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::PlaneOffset));w.findChild<QDoubleSpinBox*>("datumValue")->setValue(12);confirm(w);QCOMPARE(w.document().features().back().datum.frame.Location().Z(),22.0);
        v->clearSelection();command(w,"actionReferenceAxis");select(w,p);buttons=w.findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());select(w,q);QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::AxisTwoPoints));confirm(w);
        v->clearSelection();select(w,"origin:XY");auto* tree=w.findChild<QTreeWidget*>("featureTree");item(w,"origin:XZ")->setSelected(true);command(w,"actionReferenceAxis");QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::AxisTwoPlanes));confirm(w);
        v->clearSelection();command(w,"actionReferenceAxis");select(w,pl);select(w,p);QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::AxisNormalToPlane));confirm(w);
        v->clearSelection();command(w,"actionReferencePlane");select(w,p);select(w,q);buttons=w.findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());QVERIFY(w.findChild<QComboBox*>("datumReference1")->currentIndex()>=0);QVERIFY(w.findChild<QComboBox*>("datumReference2")->currentIndex()>=0);select(w,r);QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::mouseClick(w.findChild<QPushButton*>("datumClearReference2"),Qt::LeftButton);QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::keyClick(v,Qt::Key_Escape);
        // Pick the circumference itself, rather than only its derived center.
        auto round=PartDocument::createEmpty();Sketch s;s.entities={{EntityKind::Circle,{0,0},{},10,"circle"}};const auto sketchId=round.addSketch(s);round.addExtrude(sketchId,{"circle"},15,Direction::Positive);round.save(path);QVERIFY(w.openPartFile(path));v->m_graphics.view()->SetAt(0,0,15);v->m_graphics.view()->SetProj(0,0,1);v->m_graphics.view()->SetUp(0,1,0);v->m_graphics.view()->Camera()->SetScale(70);v->clearSelection();command(w,"actionReferencePoint");QTest::mouseClick(v,Qt::LeftButton,Qt::NoModifier,project(v,{7.071067812,7.071067812,15}));QCOMPARE(w.findChild<QComboBox*>("datumMethod")->currentData().toInt(),int(DatumMethod::PointOnReference));confirm(w);QCOMPARE(w.document().features().back().reference.references[0].source,ReferenceSource::BodyCircleCenter);QCOMPARE(w.document().features().back().datum.frame.Location().Z(),15.0);
        QVERIFY(w.errors.isEmpty());w.setWindowModified(false);w.close();
    }
};
}
QTEST_MAIN(thecad::ui::ReferenceManagerTest)
#include "ReferenceManagerTests.moc"
