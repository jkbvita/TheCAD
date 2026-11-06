#include "modeling/RelationCommands.h"
#include "modeling/DimensionInference.h"
#include <QFrame>
#include <QInputDialog>
#include "modeling/SketchTools.h"
#include <QSpinBox>
#include "modeling/DimensionAnnotation.h"
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include "modeling/ConstraintSolver.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <Graphic3d_Camera.hxx>
#include <QStackedWidget>
#include <QDockWidget>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cmath>

namespace thecad::ui {
class WorkflowWindow : public MainWindow {
public:
    QString path;
    QStringList errors;
protected:
    QString chooseSavePath() override { return path; }
    QString chooseOpenPath() override { return path; }
    UnsavedChoice confirmUnsaved() override { return UnsavedChoice::Discard; }
    void reportError(const QString& error) override { errors.append(error); }
};

class ModelingWorkflowTest final : public QObject {
    Q_OBJECT
private:
    void command(WorkflowWindow& window, const char* name)
    {
        auto* action = window.findChild<QAction*>(name);
        QVERIFY2(action, name);
        QVERIFY2(action->isEnabled(), name);
        for (auto* button : window.findChildren<QToolButton*>()) {
            if (button->defaultAction() == action && button->isVisible()) {
                QTest::mouseClick(button, Qt::LeftButton);
                QApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                return;
            }
        }
        action->trigger();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QPoint point(CadViewport* viewport, modeling::Point2 p)
    {
        const auto world = modeling::sketchPoint(modeling::SketchPlane::XY, p);
        int x, y;
        viewport->m_graphics.view()->Convert(world.X(), world.Y(), world.Z(), x, y);
        return {qRound(x / viewport->devicePixelRatioF()), qRound(y / viewport->devicePixelRatioF())};
    }
    void createSketch(WorkflowWindow& window)
    {
        // Exposure alone does not guarantee Windows has activated the window.
        // Native shortcuts require an active window even with direct Qt clicks.
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(1);
        command(window, "actionCreateSketch");
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        if (!window.findChild<QAction*>("actionFinishSketch")->isEnabled()) {
            bool picked = false;
            for (const double u : {-0.8, -0.4, 0.4, 0.8}) {
                for (const double v : {-0.8, -0.4, 0.4, 0.8}) {
                    const auto pixel = point(viewport, {u * viewport->m_planeHalfExtent, v * viewport->m_planeHalfExtent});
                    const auto hit = viewport->planeAt(pixel);
                    if (hit && hit->plane == modeling::SketchPlane::XY) {
                        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pixel);
                        picked = true;
                        break;
                    }
                }
                if (picked) break;
            }
            QVERIFY(picked);
        }
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
    }
    void editEntity(WorkflowWindow& window, modeling::EntityKind kind,
                    modeling::Point2 first, modeling::Point2 second, double radius = 1)
    {
        bool visitedDialog = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("sketchEntityDialog");
            if (!dialog) return;
            visitedDialog = true;
            dialog->findChild<QDoubleSpinBox*>("entityFirstX")->setValue(first.x);
            dialog->findChild<QDoubleSpinBox*>("entityFirstY")->setValue(first.y);
            if (kind == modeling::EntityKind::Circle) {
                dialog->findChild<QDoubleSpinBox*>("entityRadius")->setValue(radius);
            } else {
                dialog->findChild<QDoubleSpinBox*>("entitySecondX")->setValue(second.x);
                dialog->findChild<QDoubleSpinBox*>("entitySecondY")->setValue(second.y);
            }
            QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        });
        command(window, "actionEditEntity");
        QVERIFY(visitedDialog);
    }
    double volume(const TopoDS_Shape& shape)
    {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape, properties);
        return properties.Mass();
    }
    void confirmOperation(WorkflowWindow& window)
    {
        auto* dialog = window.findChild<QWidget*>("operationPanel");
        QVERIFY(dialog && dialog->isVisible());
        auto* ok = dialog->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok);
        QVERIFY2(ok->isEnabled(), qPrintable(dialog->findChild<QLabel*>("operationStatus")->text()));
        QTest::mouseClick(ok, Qt::LeftButton);
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(window.errors.isEmpty());
    }
private slots:
    void intersectingCircleRectangleExtrudeCutAndReopen() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{0,0},{15,0},1,"top"},{EntityKind::Line,{15,0},{15,-15},1,"right"},{EntityKind::Line,{15,-15},{0,-15},1,"bottom"},{EntityKind::Line,{0,-15},{0,0},1,"left"},{EntityKind::Circle,{0,0},{},10,"circle"},{EntityKind::Line,{7.5,-20},{7.5,5},1,"center-v",true},{EntityKind::Line,{-5,-7.5},{20,-7.5},1,"center-h",true}};
        viewport->sketchChanged(sketch);window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");auto* regions=window.findChild<QListWidget*>("operationContour");QVERIFY(regions);QCOMPARE(regions->count(),3);QVERIFY(regions->selectedItems().isEmpty());window.findChild<QDoubleSpinBox*>("featureLength")->setValue(5);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{3,-3}));QCOMPARE(regions->selectedItems().size(),1);QVERIFY(!viewport->m_previewPresentation.IsNull());QVERIFY(std::abs(volume(viewport->m_previewPresentation->Shape())-125*3.14159265358979323846)<1e-6);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-5,0}));QCOMPARE(regions->selectedItems().size(),2);QVERIFY(std::abs(volume(viewport->m_previewPresentation->Shape())-500*3.14159265358979323846)<1e-6);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-5,0}));confirmOperation(window);
        QVERIFY(std::abs(volume(window.document().shape())-125*3.14159265358979323846)<1e-6);QCOMPARE(window.document().features()[0].sketch.entities.size(),std::size_t(7));command(window,"actionUndo");QVERIFY(window.document().shape().IsNull());command(window,"actionRedo");QVERIFY(std::abs(volume(window.document().shape())-125*3.14159265358979323846)<1e-6);
        createSketch(window);viewport->sketchChanged(sketch);window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrudedCut");regions=window.findChild<QListWidget*>("operationContour");QVERIFY(regions);QCOMPARE(regions->count(),3);window.findChild<QDoubleSpinBox*>("featureLength")->setValue(2);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{3,-3}));QCOMPARE(regions->selectedItems().size(),1);confirmOperation(window);QVERIFY(std::abs(volume(window.document().shape())-75*3.14159265358979323846)<1e-6);
        QTemporaryDir directory;window.path=directory.filePath("intersection-cells.thepart");command(window,"actionSaveAs");command(window,"actionNew");command(window,"actionOpen");QVERIFY(std::abs(volume(window.document().shape())-75*3.14159265358979323846)<1e-6);QVERIFY(window.errors.isEmpty());window.close();
    }
    void midpointAndRectangleEditsUndoAsSingleActions() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        command(window,"actionRectangle");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-40,30}));command(window,"actionSelectSketchEntity");const auto before=*viewport->m_sketch;
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,30}));const auto target=point(viewport,{8,45});QMouseEvent move(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);QVERIFY(viewport->m_pointDrag && viewport->m_pointDrag->candidate);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,target);const auto height=viewport->m_sketch->entities[2].second.y;QVERIFY(height>40);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities[2].second.y,before.entities[2].second.y);QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities[2].second.y,height);
        command(window,"actionPoint");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-20,0}));QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(5));QCOMPARE(viewport->m_sketch->relations.back().kind,RelationKind::Midpoint);const auto relationId=viewport->m_sketch->relations.back().id;
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4));QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->relations.back().id,relationId);
        QTemporaryDir directory;command(window,"actionFinishSketch");window.path=directory.filePath("midpoint-rectangle.thepart");command(window,"actionSaveAs");command(window,"actionNew");command(window,"actionOpen");QCOMPARE(window.document().features()[0].sketch.relations.back().kind,RelationKind::Midpoint);QVERIFY(window.errors.isEmpty());window.close();
    }
    void circleCentersAndOutlineDragging() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Circle,{-25,-15},{},8,"circle"}};auto diameter=inferDimension(sketch,{}, {"circle"},{-15,-5});diameter.id="diameter";sketch=setSketchDimension(sketch,diameter);viewport->sketchChanged(sketch);command(window,"actionSelectSketchEntity");
        viewport->m_hoveredPoint.reset();viewport->m_hoveredEntity.clear();viewport->updatePointPresentation();QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(2));
        const auto center=viewport->m_sketch->entities[0].first;const auto press=point(viewport,{center.x,center.y-8});const auto end=press+QPoint(64,-37);
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,press);QVERIFY(viewport->m_pointDrag && viewport->m_pointDrag->circleBody);QVERIFY(!viewport->selectedSketchPoint());
        QMouseEvent move(QEvent::MouseMove,end,viewport->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);
        QVERIFY(QLineF(point(viewport,center),point(viewport,viewport->m_sketch->entities[0].first)).length()>20);QVERIFY(std::abs(viewport->m_sketch->entities[0].radius-8)<1e-7);QCOMPARE(viewport->m_sketch->dimensions[0].value,16.0);
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,end);QVERIFY(!viewport->m_pointDrag);const auto moved=viewport->m_sketch->entities[0].first;
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities[0].first.x,center.x);QCOMPARE(viewport->m_sketch->entities[0].first.y,center.y);
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities[0].first.x,moved.x);
        const auto second=point(viewport,{moved.x,moved.y-8});QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,second);QVERIFY(viewport->m_pointDrag && viewport->m_pointDrag->circleBody);
        QMouseEvent another(QEvent::MouseMove,second+QPoint(40,30),viewport->mapToGlobal(second+QPoint(40,30)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&another);QTest::keyClick(viewport,Qt::Key_Escape);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,second);QCOMPARE(viewport->m_sketch->entities[0].first.x,moved.x);
        viewport->m_hoveredPoint.reset();viewport->updatePointPresentation();QCOMPARE(viewport->m_pointPresentations.size(),std::size_t(2));QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void equalBatchSymbolsOnEveryMember() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Circle,{-35,-20},{},5,"a"},{EntityKind::Circle,{0,20},{},7,"b"},{EntityKind::Circle,{35,-20},{},9,"c"}};sketch=addSelectionRelations(sketch,RelationKind::Equal,{"a","b","c"},"equal-group");viewport->sketchChanged(sketch);command(window,"actionSelectSketchEntity");
        QCOMPARE(viewport->m_relationHits.size(),std::size_t(3));for(const auto& entity:sketch.entities)QCOMPARE(std::count_if(viewport->m_relationHits.begin(),viewport->m_relationHits.end(),[&](const auto& hit){return hit.entityId==entity.id;}),1);
        const auto groupId=sketch.relations[0].id;const auto icon=viewport->m_relationHits.back().bounds.center().toPoint();QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,icon);QCOMPARE(viewport->selectedSketchRelation(),groupId);QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(3));
        QTest::keyClick(viewport,Qt::Key_Delete);QVERIFY(viewport->m_sketch->relations.empty());QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(3));QVERIFY(viewport->m_relationHits.empty());
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_relationHits.size(),std::size_t(3));QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(2));QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QVERIFY(viewport->m_sketch->relations.empty());QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);
        viewport->showSketchRelations(false);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{42,-20}));command(window,"actionDisplayDeleteRelations");auto* list=window.findChild<QListWidget*>("sketchConstraints");QVERIFY(list);QCOMPARE(list->count(),1);list->setCurrentRow(0);
        QTimer::singleShot(0,&window,[&]{auto* dialog=window.findChild<QInputDialog*>();QVERIFY(dialog);dialog->findChild<QComboBox*>()->setCurrentText("Equal Curve Length");dialog->accept();});QTest::mouseClick(window.findChild<QPushButton*>("changeRelation"),Qt::LeftButton);QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::EqualCurveLength);QCOMPARE(viewport->m_sketch->relations[1].kind,RelationKind::EqualCurveLength);
        QTest::mouseClick(window.findChild<QPushButton*>("cancelDimension"),Qt::LeftButton);viewport->showSketchRelations(true);QCOMPARE(viewport->m_relationHits.size(),std::size_t(3));
        auto independent=*viewport->m_sketch;independent.entities.push_back({EntityKind::Circle,{-35,30},{},8,"d"});independent=addSelectionRelations(independent,RelationKind::Equal,{"b","d"},"independent-equal");viewport->sketchChanged(independent);QCOMPARE(viewport->m_relationHits.size(),std::size_t(5));QCOMPARE(std::count_if(viewport->m_relationHits.begin(),viewport->m_relationHits.end(),[](const auto& hit){return hit.entityId=="b";}),2);
        QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void shiftRelationsToolbarSymbolsAndDelete() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{-40,-20},{-10,-5},1,"a"},{EntityKind::Line,{10,20},{40,30},1,"b"}};viewport->sketchChanged(sketch);
        QTest::keyPress(viewport,Qt::Key_Shift);
        const auto choose=[&](Point2 p){QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,p));};
        choose({-25,-12.5});choose({25,25});QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(2));QVERIFY(!viewport->m_pointDrag);QVERIFY(!viewport->m_firstPoint);QVERIFY(!viewport->m_tools.drawing());
        choose({-25,-12.5});QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(1));choose({-25,-12.5});QTest::keyRelease(viewport,Qt::Key_Shift);
        auto* toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar && toolbar->isVisible());
        QVERIFY(!toolbar->isWindow());QCOMPARE(toolbar->parentWidget(),viewport);QVERIFY(toolbar->testAttribute(Qt::WA_NativeWindow));
        QVERIFY(viewport->rect().contains(toolbar->geometry()));
        QTest::keyClick(viewport,Qt::Key_Escape);QVERIFY(!toolbar->isVisible());
        viewport->relationsContextRequested(viewport->rect().bottomRight());toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar->isVisible());QVERIFY(viewport->rect().contains(toolbar->geometry()));
        QMouseEvent away(QEvent::MouseMove,QPoint(1,1),viewport->mapToGlobal(QPoint(1,1)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(viewport,&away);QVERIFY(!toolbar->isVisible());
        viewport->relationsContextRequested(point(viewport,{-25,-12.5}));toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar->isVisible());
        auto* parallel=toolbar->findChild<QToolButton*>(QString("contextRelation%1").arg(int(RelationKind::Parallel)));QVERIFY(parallel);
        QTest::mouseMove(parallel,parallel->rect().center());QVERIFY(toolbar->isVisible());QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(2));
        QTest::mouseClick(parallel,Qt::LeftButton);QVERIFY(!toolbar->isVisible());QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Parallel);const auto id=viewport->m_sketch->relations[0].id;
        QTRY_COMPARE(viewport->m_relationHits.size(),std::size_t(1));QCOMPARE(viewport->m_relationHits[0].bounds.size(),QSizeF(18,18));const auto icon=viewport->m_relationHits[0].bounds.center().toPoint();
        QMouseEvent hover(QEvent::MouseMove,icon,viewport->mapToGlobal(icon),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(viewport,&hover);QCOMPARE(viewport->m_hoveredRelation,id);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,icon);QCOMPARE(viewport->selectedSketchRelation(),id);QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(2));
        QTest::keyClick(viewport,Qt::Key_Delete);QVERIFY(viewport->m_sketch->relations.empty());QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->relations[0].id,id);QCOMPARE(viewport->m_relationHits.size(),std::size_t(1));
        const auto old=viewport->m_relationHits[0].bounds.center();auto moved=moveSketchPoint(*viewport->m_sketch,{"a",PointRole::Second},{-5,10});viewport->sketchChanged(moved);QVERIFY(QLineF(old,viewport->m_relationHits[0].bounds.center()).length()>1);
        viewport->m_graphics.view()->SetZoom(1.5);viewport->updateConstraintPresentation();QCOMPARE(viewport->m_relationHits[0].bounds.size(),QSizeF(18,18));
        auto* visible=window.findChild<QAction*>("actionShowSketchRelations");visible->setChecked(false);QVERIFY(viewport->m_relationHits.empty());
        command(window,"actionDisplayDeleteRelations");auto* list=window.findChild<QListWidget*>("sketchConstraints");QVERIFY(list && list->count()>0);
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->item(0)).center());QCOMPARE(viewport->selectedSketchRelation(),id);
        QTest::mouseClick(window.findChild<QPushButton*>("cancelDimension"),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);visible->setChecked(true);QVERIFY(!viewport->m_relationHits.empty());
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-60,55}));QVERIFY(viewport->selectedSketchEntities().empty());
        QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void shiftReleaseFromSidePanelShowsOneShotRelationsOverlay() {
        using namespace modeling;WorkflowWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{-40,-20},{-10,-5},1,"a"},{EntityKind::Line,{10,20},{40,30},1,"b"}};viewport->sketchChanged(sketch);
        auto* entities=window.findChild<QListWidget*>("sketchEntities");QVERIFY(entities);
        QTest::keyPress(viewport,Qt::Key_Shift);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{-25,-12.5}));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{25,25}));
        QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(2));
        QVERIFY(!window.findChild<QFrame*>("sketchRelationsContext"));
        entities->setFocus();QVERIFY(entities->hasFocus());
        QKeyEvent repeated(QEvent::KeyRelease,Qt::Key_Shift,Qt::NoModifier,QString{},true);QApplication::sendEvent(entities,&repeated);
        QVERIFY(!window.findChild<QFrame*>("sketchRelationsContext"));
        QTest::keyRelease(entities,Qt::Key_Shift);
        auto* toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar&&toolbar->isVisible());
        QCOMPARE(toolbar->window(),&window);QVERIFY(entities->hasFocus());QVERIFY(!toolbar->hasFocus());
        QVERIFY(viewport->rect().contains(toolbar->geometry()));
        QVERIFY((toolbar->pos()-(point(viewport,{25,25})+QPoint(14,14))).manhattanLength()<2);
        auto* equal=toolbar->findChild<QToolButton*>(QString("contextRelation%1").arg(int(RelationKind::Equal)));QVERIFY(equal);
        QVERIFY(!toolbar->findChild<QToolButton*>(QString("contextRelation%1").arg(int(RelationKind::Concentric))));
        QTest::mouseMove(equal,equal->rect().center());QVERIFY(toolbar->isVisible());
        QTest::mouseClick(equal,Qt::LeftButton);QVERIFY(!toolbar->isVisible());
        QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Equal);
        const auto id=viewport->m_sketch->relations[0].id;
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(viewport->m_sketch->relations.empty());
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->relations[0].id,id);
        // Releasing Shift by itself must not reopen a toolbar for an old selection.
        QTest::keyClick(viewport,Qt::Key_Shift);QVERIFY(!toolbar->isVisible());
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,QPoint(10,10));
        QTest::keyPress(viewport,Qt::Key_Shift);QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,QPoint(10,10));QTest::keyRelease(viewport,Qt::Key_Shift);
        QVERIFY(!toolbar->isVisible());QVERIFY(viewport->selectedSketchEntities().empty());
        QTest::keyPress(viewport,Qt::Key_Shift);
        for(const auto& entity:viewport->m_sketch->entities)QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{(entity.first.x+entity.second.x)/2,(entity.first.y+entity.second.y)/2}));
        QTest::keyRelease(viewport,Qt::Key_Shift);toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar->isVisible());
        // A click in the side panel dismisses it without changing constraints.
        QTest::mouseClick(entities->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(5,5));QVERIFY(!toolbar->isVisible());QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));
        QTest::keyPress(viewport,Qt::Key_Shift);QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{-25,-12.5}));
        command(window,"actionFinishSketch");QTest::keyRelease(&window,Qt::Key_Shift);QVERIFY(!toolbar->isVisible());QVERIFY(!viewport->m_shiftSelectionChanged);
        QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void circleContextConstructionAndInspectorChange() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Circle,{-25,-15},{},6,"a"},{EntityKind::Circle,{25,20},{},9,"b"}};viewport->sketchChanged(sketch);
        QTest::keyPress(viewport,Qt::Key_Shift);QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{-19,-15}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{34,20}));QTest::keyRelease(viewport,Qt::Key_Shift);
        auto* toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar && toolbar->isVisible());
        QVERIFY(!toolbar->findChild<QToolButton*>(QString("contextRelation%1").arg(int(RelationKind::Parallel))));
        auto* concentric=toolbar->findChild<QToolButton*>(QString("contextRelation%1").arg(int(RelationKind::Concentric)));QVERIFY(concentric);QTest::mouseClick(concentric,Qt::LeftButton);
        QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Concentric);QCOMPARE(viewport->m_sketch->entities[0].radius,6.0);QCOMPARE(viewport->m_sketch->entities[1].radius,9.0);
        viewport->relationsContextRequested(viewport->rect().center());toolbar=window.findChild<QFrame*>("sketchRelationsContext");auto* construction=toolbar->findChild<QToolButton*>("contextConstruction");QVERIFY(construction);QTest::mouseClick(construction,Qt::LeftButton);
        QVERIFY(viewport->m_sketch->entities[0].construction && viewport->m_sketch->entities[1].construction);QVERIFY(closedContours(*viewport->m_sketch).empty());
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(!viewport->m_sketch->entities[0].construction && !viewport->m_sketch->entities[1].construction);QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);
        viewport->relationsContextRequested(viewport->rect().center());toolbar=window.findChild<QFrame*>("sketchRelationsContext");construction=toolbar->findChild<QToolButton*>("contextConstruction");QCOMPARE(construction->toolTip(),QString("Normal Geometry"));QTest::mouseClick(construction,Qt::LeftButton);QVERIFY(!viewport->m_sketch->entities[0].construction);
        for(auto* button:window.findChildren<QToolButton*>())QVERIFY(button->defaultAction()!=window.findChild<QAction*>("actionConstructionLine"));
        QTemporaryDir directory;command(window,"actionFinishSketch");window.path=directory.filePath("context.thepart");command(window,"actionSaveAs");command(window,"actionNew");command(window,"actionOpen");QCOMPARE(window.document().features()[0].sketch.relations[0].kind,RelationKind::Concentric);QVERIFY(window.errors.isEmpty());window.close();
    }
    void contextualConflictAndRelationModification() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{-40,-20},{-10,-20},1,"a"},{EntityKind::Line,{10,20},{40,20},1,"b"}};
        sketch=addSelectionRelations(sketch,RelationKind::Parallel,{"a","b"},"parallel");viewport->sketchChanged(sketch);command(window,"actionSelectSketchEntity");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-25,-20}));
        command(window,"actionDisplayDeleteRelations");auto* list=window.findChild<QListWidget*>("sketchConstraints");QVERIFY(list && list->count()==1);list->setCurrentRow(0);
        QTimer::singleShot(0,&window,[&]{auto* dialog=window.findChild<QInputDialog*>();QVERIFY(dialog);dialog->findChild<QComboBox*>()->setCurrentText("Perpendicular");dialog->accept();});
        QTest::mouseClick(window.findChild<QPushButton*>("changeRelation"),Qt::LeftButton);QCOMPARE(viewport->m_sketch->relations[0].id,std::string("parallel-1"));QVERIFY2(viewport->m_sketch->relations[0].kind==RelationKind::Perpendicular,qPrintable(window.findChild<QLabel*>("constraintError")->text()));
        QTest::mouseClick(window.findChild<QPushButton*>("cancelDimension"),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        viewport->setFocus();QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Parallel);QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->relations[0].kind,RelationKind::Perpendicular);
        auto frozen=addSelectionRelations(*viewport->m_sketch,RelationKind::FixedEntity,{"a"},"fix-a");viewport->sketchChanged(frozen);const auto before=*viewport->m_sketch;
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-65,55}));
        QTest::keyPress(viewport,Qt::Key_Shift);QTest::mouseClick(viewport,Qt::LeftButton,Qt::ShiftModifier,point(viewport,{(before.entities[0].first.x+before.entities[0].second.x)/2,(before.entities[0].first.y+before.entities[0].second.y)/2}));QTest::keyRelease(viewport,Qt::Key_Shift);
        auto* toolbar=window.findChild<QFrame*>("sketchRelationsContext");QVERIFY(toolbar && toolbar->isVisible());
        auto* vertical=toolbar->findChild<QToolButton*>(QString("contextRelation%1").arg(int(RelationKind::Vertical)));QVERIFY(vertical);QTest::mouseClick(vertical,Qt::LeftButton);
        QCOMPARE(viewport->m_sketch->relations.size(),before.relations.size());QCOMPARE(viewport->m_sketch->entities[0].first.x,before.entities[0].first.x);QCOMPARE(viewport->m_sketch->entities[0].second.y,before.entities[0].second.y);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));
        QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void newSketchToolsConversionHexagonAndHistory() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        command(window,"actionCenterline");QVERIFY(window.findChild<QAction*>("actionCenterline")->isChecked());QVERIFY(!window.findChild<QAction*>("actionLine")->isChecked());
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-30,-30}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{30,-30}));QVERIFY(viewport->m_sketch->entities[0].construction);
        command(window,"actionSelectSketchEntity");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,-30}));const auto lineId=viewport->m_sketch->entities[0].id;
        command(window,"actionConstructionLine");QVERIFY(!viewport->m_sketch->entities[0].construction);QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(viewport->m_sketch->entities[0].construction);QCOMPARE(viewport->m_sketch->entities[0].id,lineId);
        command(window,"actionPolygon");QVERIFY(!window.findChild<QSpinBox*>("polygonSides"));QVERIFY(!window.findChild<QAction*>("actionEditPolygon"));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));const auto pixel=point(viewport,{18,0});QMouseEvent move(QEvent::MouseMove,pixel,viewport->mapToGlobal(pixel),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel);QCOMPARE(viewport->m_sketch->polygons.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->polygons[0].edges.size(),std::size_t(6));
        const auto edgeIds=viewport->m_sketch->polygons[0].edges;command(window,"actionSelectSketchEntity");
        auto first=viewport->m_sketch->entities[2];QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{(first.first.x+first.second.x)/2,(first.first.y+first.second.y)/2}));
        QVERIFY(!window.findChild<QDialog*>("polygonEditor"));
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(viewport->m_sketch->polygons.empty());
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->polygons[0].edges,edgeIds);
        validatePolygons(*viewport->m_sketch,true);
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");window.findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);window.findChild<QDoubleSpinBox*>("featureLength")->setValue(5);confirmOperation(window);QVERIFY(volume(window.document().shape())>1000);
        QTemporaryDir directory;window.path=directory.filePath("tools.thepart");command(window,"actionSaveAs");command(window,"actionNew");command(window,"actionOpen");QCOMPARE(window.document().features()[0].sketch.polygons[0].edges,edgeIds);QVERIFY(window.document().features()[0].sketch.entities[0].construction);QVERIFY(window.errors.isEmpty());window.close();
    }
    void pointToolDimensionsAndUndo() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);command(window,"actionPoint");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,20}));QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QVERIFY(closedContours(*viewport->m_sketch).empty());
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));
        command(window,"actionSelectSketchEntity");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,20}));command(window,"actionSmartDimension");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{40,35}));
        auto* editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);auto* value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");value->setValue(40);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));
        QTest::keyClick(viewport,Qt::Key_Escape);QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(viewport->m_sketch->dimensions.empty());QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void undoRedoKeyboardLimitAndBranching() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        for(int i=0;i<12;++i){auto sketch=*viewport->m_sketch;sketch.entities.push_back({EntityKind::Line,{-50.0+i*6,-30},{-50.0+i*6,-10},1,"edge-"+std::to_string(i)});viewport->sketchChanged(sketch);}
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(12));QVERIFY(window.findChild<QAction*>("actionUndo")->isEnabled());
        viewport->setFocus();for(int i=0;i<10;++i)QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QVERIFY(!window.findChild<QAction*>("actionUndo")->isEnabled());
        for(int i=0;i<10;++i)QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(12));QVERIFY(!window.findChild<QAction*>("actionRedo")->isEnabled());
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(11));
        // Selection and an exact no-op do not consume history or clear redo.
        command(window,"actionSelectSketchEntity");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-50,-20}));viewport->sketchChanged(*viewport->m_sketch);
        QVERIFY(window.findChild<QAction*>("actionRedo")->isEnabled());
        auto sketch=*viewport->m_sketch;sketch.entities.push_back({EntityKind::Circle,{45,30},{},4,"new-circle"});viewport->sketchChanged(sketch);
        QVERIFY(!window.findChild<QAction*>("actionRedo")->isEnabled());QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(11));
        QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void deleteDimensionedGeometryAndIndependentDimension() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{-30,-20},{30,-20},1,"line"},{EntityKind::Circle,{35,25},{},5,"circle"}};
        SketchDimension length;length.id="length";length.kind=DimensionKind::HorizontalDistance;length.first={"line",PointRole::First};length.second={"line",PointRole::Second};length.value=60;length.annotation={0,-35};
        auto diameter=length;diameter.id="diameter";diameter.kind=DimensionKind::CircleDiameter;diameter.first={"circle",PointRole::Center};diameter.second={};diameter.value=10;diameter.annotation={50,40};sketch.dimensions={length,diameter};viewport->sketchChanged(sketch);
        command(window,"actionSelectSketchEntity");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,-20}));QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(1));
        Quantity_Color selectedColor;viewport->m_selectedEntityPresentation->Color(selectedColor);QVERIFY(selectedColor.Blue()>0.9 && selectedColor.Green()>0.6);
        QTest::keyClick(viewport,Qt::Key_Delete);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->dimensions[0].id,std::string("diameter"));
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(2));
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);
        auto annotation=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,annotation));QCOMPARE(viewport->selectedSketchDimensions().size(),std::size_t(1));QVERIFY(viewport->selectedSketchEntities().empty());
        QTest::keyClick(viewport,Qt::Key_Delete);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->entities[0].second.x,30.0);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(2));
        // DD is two discrete consecutive presses; one D or a held key cannot delete.
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,-20}));QTest::keyClick(viewport,Qt::Key_D);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));
        QKeyEvent repeat(QEvent::KeyPress,Qt::Key_D,Qt::NoModifier,"d",true,1);QApplication::sendEvent(viewport,&repeat);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));
        QTest::keyClick(viewport,Qt::Key_D);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QTest::keyClick(viewport,Qt::Key_D);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);annotation=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,annotation));QTest::keyClick(viewport,Qt::Key_D);QTest::keyClick(viewport,Qt::Key_D);
        QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));
        QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void deleteContourAsOneUndoAction() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{-30,-20},{30,-20},1,"a"},{EntityKind::Line,{30,-20},{30,20},1,"b"},{EntityKind::Line,{30,20},{-30,20},1,"c"},{EntityKind::Line,{-30,20},{-30,-20},1,"d"}};
        for(int i=0;i<4;++i)sketch.relations.push_back({"join"+std::to_string(i),RelationKind::Coincident,{sketch.entities[i].id,PointRole::Second},{sketch.entities[(i+1)%4].id,PointRole::First}});
        SketchDimension width;width.id="width";width.kind=DimensionKind::HorizontalDistance;width.first={"a",PointRole::First};width.second={"a",PointRole::Second};width.value=60;width.annotation={0,-35};sketch.dimensions={width};viewport->sketchChanged(sketch);
        command(window,"actionSelectSketchEntity");
        for(const auto mid:{Point2{0,-20},Point2{30,0},Point2{0,20},Point2{-30,0}})QTest::mouseClick(viewport,Qt::LeftButton,Qt::ControlModifier,point(viewport,mid));
        QCOMPARE(viewport->selectedSketchEntities().size(),std::size_t(4));QTest::keyClick(viewport,Qt::Key_Delete);
        QVERIFY(viewport->m_sketch->entities.empty());QVERIFY(viewport->m_sketch->dimensions.empty());QVERIFY(viewport->m_sketch->relations.empty());
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4));QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(closedContours(*viewport->m_sketch).size(),std::size_t(1));
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QVERIFY(viewport->m_sketch->entities.empty());QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void annotationDragUndoAndDimensionValueUndo() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{-30,-20},{30,-20},1,"line"}};SketchDimension length;length.id="length";length.kind=DimensionKind::HorizontalDistance;length.first={"line",PointRole::First};length.second={"line",PointRole::Second};length.value=60;length.annotation={0,-35};sketch.dimensions={length};viewport->sketchChanged(sketch);command(window,"actionSelectSketchEntity");
        const auto initial=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);const auto from=point(viewport,initial),to=point(viewport,{15,-45});
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,from);
        QMouseEvent move(QEvent::MouseMove,to,viewport->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);
        QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,to);
        const auto moved=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);QVERIFY(std::hypot(moved.x-initial.x,moved.y-initial.y)>5);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);auto restored=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);QVERIFY(std::hypot(restored.x-initial.x,restored.y-initial.y)<1e-7);
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);restored=dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0]);QVERIFY(std::hypot(restored.x-moved.x,restored.y-moved.y)<1e-7);
        QTest::mouseDClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,restored));auto* editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);QVERIFY(!window.findChild<QAction*>("actionUndo")->isEnabled());
        auto* value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");value->setValue(80);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions[0].value,80.0);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->dimensions[0].value,60.0);QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->dimensions[0].value,80.0);
        QVERIFY(window.errors.isEmpty());command(window,"actionFinishSketch");window.close();
    }
    void featureHistorySavePointAndSessionReset() {
        QTemporaryDir directory;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(window.document().features().empty());QVERIFY(!viewport->m_sketchEditing);
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(window.document().features().size(),std::size_t(1));QVERIFY(viewport->m_sketchEditing);
        command(window,"actionLine");
        for(const auto p:{modeling::Point2{0,0},modeling::Point2{40,0},modeling::Point2{40,30},modeling::Point2{0,30},modeling::Point2{0,0}})
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,p));
        QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4));
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(3));
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4));
        GProp_GProps area;BRepGProp::SurfaceProperties(modeling::closedContours(*viewport->m_sketch).front().face,area);const double expectedVolume=area.Mass()*10;
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");
        QVERIFY(!window.findChild<QAction*>("actionUndo")->isEnabled());
        window.findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);
        window.findChild<QDoubleSpinBox*>("featureLength")->setValue(10);confirmOperation(window);
        QCOMPARE(window.document().features().size(),std::size_t(2));QVERIFY(std::abs(volume(window.document().shape())-expectedVolume)<1e-6);
        viewport->setFocus();QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(window.document().features().size(),std::size_t(1));QVERIFY(viewport->m_sketchEditing);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(4));QVERIFY(viewport->m_shape.IsNull());
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(window.document().features().size(),std::size_t(2));QVERIFY(!viewport->m_sketchEditing);QVERIFY(std::abs(volume(window.document().shape())-expectedVolume)<1e-6);
        window.path=directory.filePath("history.thepart");command(window,"actionSaveAs");QVERIFY(!window.isWindowModified());
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(window.isWindowModified());
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QVERIFY(!window.isWindowModified());
        command(window,"actionNew");QVERIFY(!window.findChild<QAction*>("actionUndo")->isEnabled());QVERIFY(!window.findChild<QAction*>("actionRedo")->isEnabled());
        command(window,"actionOpen");QCOMPARE(window.document().features().size(),std::size_t(2));QVERIFY(!window.findChild<QAction*>("actionUndo")->isEnabled());QVERIFY(!window.findChild<QAction*>("actionRedo")->isEnabled());QVERIFY(window.errors.isEmpty());window.close();
    }
    void sketchToolActionsFollowMode() {
        WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        const auto checked=[&](const char* name){return window.findChild<QAction*>(name)->isChecked();};
        QVERIFY(checked("actionLine"));
        command(window,"actionSmartDimension");QVERIFY(checked("actionSmartDimension"));QVERIFY(!checked("actionLine"));QVERIFY(viewport->m_tools.dimensionPicking());
        command(window,"actionCircle");QVERIFY(checked("actionCircle"));QVERIFY(!checked("actionSmartDimension"));QVERIFY(!viewport->m_tools.dimensionPicking());
        QTest::keyClick(viewport,Qt::Key_Escape);QVERIFY(!checked("actionCircle"));QVERIFY(checked("actionSelectSketchEntity"));
        command(window,"actionSmartDimension");QTest::keyClick(viewport,Qt::Key_Escape);QVERIFY(!checked("actionSmartDimension"));QVERIFY(!viewport->m_tools.dimensionPicking());
        command(window,"actionRectangle");QVERIFY(checked("actionRectangle"));command(window,"actionLine");QVERIFY(!checked("actionRectangle"));QVERIFY(checked("actionLine"));
        command(window,"actionFinishSketch");QVERIFY(!checked("actionLine"));QVERIFY(!checked("actionSelectSketchEntity"));window.close();
    }
    void smartDimensionCreateEditCancelAndConflict() {
        QTemporaryDir directory;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        using namespace modeling;Sketch sketch;sketch.entities={{EntityKind::Line,{0,0},{30,40},1,"line"},{EntityKind::Circle,{-30,-20},{},5,"circle"}};sketch.relations={{"anchor",RelationKind::Coincident,{"line",PointRole::First},{}}};viewport->sketchChanged(sketch);
        command(window,"actionSelectSketchEntity");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{15,20}));command(window,"actionSmartDimension");
        QVERIFY(!window.findChild<QWidget*>("constraintPage"));QVERIFY(!window.findChild<QDoubleSpinBox*>("smartDimensionValue"));
        const auto previewPoint=point(viewport,{50,55});QMouseEvent motion(QEvent::MouseMove,previewPoint,viewport->mapToGlobal(previewPoint),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(viewport,&motion);QVERIFY(viewport->m_dimensionPreview);QCOMPARE(viewport->m_dimensionPreview->kind,DimensionKind::LineLength);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{50,55}));auto* editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);QVERIFY(editor->isWindow());const auto annotationPixel=viewport->mapToGlobal(point(viewport,{50,55}));QVERIFY((editor->pos()-annotationPixel).manhattanLength()<editor->width()+editor->height());auto* value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");QVERIFY(value);QVERIFY(std::abs(value->value()-50)<1e-6);value->setValue(60);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->dimensions[0].value,60.0);QVERIFY(viewport->m_tools.dimensionPicking());QVERIFY(window.findChild<QAction*>("actionSmartDimension")->isChecked());const auto id=viewport->m_sketch->dimensions[0].id;
        QTest::mouseDClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0])));editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");value->setValue(70);QTest::keyClick(value,Qt::Key_Escape);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions[0].value,60.0);
        QTest::mouseDClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,dimensionAnnotationPosition(*viewport->m_sketch,viewport->m_sketch->dimensions[0])));editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");value->setValue(70);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions[0].value,70.0);QCOMPARE(viewport->m_sketch->dimensions[0].id,id);QVERIFY(window.findChild<QAction*>("actionSmartDimension")->isChecked());
        const auto end=viewport->m_sketch->entities[0].second;QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{end.x/2,end.y/2}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-15,-15}));editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");value->setValue(80);QTest::keyClick(value,Qt::Key_Return);
        QVERIFY(!editor->findChild<QLabel*>("smartDimensionError")->text().isEmpty());QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->dimensions[0].value,70.0);QTest::keyClick(value,Qt::Key_Escape);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-25,-20}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-40,-40}));editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");value->setValue(14);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(2));QVERIFY(std::abs(viewport->m_sketch->entities[1].radius-7)<1e-6);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-23,-20}));QTest::keyClick(viewport,Qt::Key_R);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-45,-35}));editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);editor->findChild<QCheckBox*>("smartDimensionReference")->setChecked(true);QTest::mouseClick(editor->findChild<QDialogButtonBox*>("smartDimensionButtons")->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions.back().kind,DimensionKind::CircleRadius);QVERIFY(!viewport->m_sketch->dimensions.back().driving);QCOMPARE(viewport->m_sketch->dimensions.back().value,7.0);QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(3));
        command(window,"actionFinishSketch");window.path=directory.filePath("dimensions.thepart");command(window,"actionSaveAs");command(window,"actionNew");command(window,"actionOpen");QCOMPARE(window.document().features()[0].sketch.dimensions.size(),std::size_t(3));QVERIFY(window.errors.isEmpty());window.close();
    }
    void dimensionEditRegeneratesExistingExtrusion() {
        WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        using namespace modeling;Sketch sketch;sketch.entities={{EntityKind::Line,{0,0},{50,0},1,"a"},{EntityKind::Line,{50,0},{50,30},1,"b"},{EntityKind::Line,{50,30},{0,30},1,"c"},{EntityKind::Line,{0,30},{0,0},1,"d"}};
        for(int i=0;i<4;++i){sketch.relations.push_back({"join-"+std::to_string(i),RelationKind::Coincident,{sketch.entities[i].id,PointRole::Second},{sketch.entities[(i+1)%4].id,PointRole::First}});sketch.relations.push_back({"axis-"+std::to_string(i),i%2?RelationKind::Vertical:RelationKind::Horizontal,{sketch.entities[i].id,PointRole::First},{sketch.entities[i].id,PointRole::Second}});}sketch.relations.push_back({"origin",RelationKind::Coincident,{"a",PointRole::First},{}});
        SketchDimension width;width.id="width";width.kind=DimensionKind::HorizontalDistance;width.first={"a",PointRole::First};width.second={"a",PointRole::Second};width.value=50;width.annotation={25,-10};auto height=width;height.id="height";height.kind=DimensionKind::VerticalDistance;height.second={"d",PointRole::First};height.value=30;height.annotation={-10,15};sketch.dimensions={width,height};viewport->sketchChanged(sketch);
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");window.findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);window.findChild<QDoubleSpinBox*>("featureLength")->setValue(10);confirmOperation(window);QVERIFY(std::abs(volume(window.document().shape())-15000)<1e-6);
        auto* tree=window.findChild<QTreeWidget*>("featureTree");QTreeWidgetItem* item=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()=="sketch-1")item=*it;QVERIFY(item);tree->setCurrentItem(item);command(window,"actionEditFeature");
        command(window,"actionSelectSketchEntity");QTest::mouseDClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,-10}));auto* value=window.findChild<QDoubleSpinBox*>("smartDimensionValue");QVERIFY(value);value->setValue(80);QTest::mouseClick(window.findChild<QDialogButtonBox*>("smartDimensionButtons")->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(std::abs(volume(viewport->m_shape)-24000)<1e-6);command(window,"actionFinishSketch");QVERIFY(std::abs(volume(window.document().shape())-24000)<1e-6);QCOMPARE(window.document().features()[0].sketch.dimensions[0].value,80.0);
        viewport->setFocus();QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QVERIFY(viewport->m_sketchEditing);QCOMPARE(viewport->m_sketch->dimensions[0].value,50.0);QVERIFY(std::abs(volume(viewport->m_shape)-15000)<1e-6);
        QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QVERIFY(!viewport->m_sketchEditing);QCOMPARE(window.document().features()[0].sketch.dimensions[0].value,80.0);QVERIFY(std::abs(volume(window.document().shape())-24000)<1e-6);
        QVERIFY(window.errors.isEmpty());window.close();
    }
    void horizontalVerticalAndFixedRelationUi() {
        WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        modeling::Sketch sketch;sketch.entities={{modeling::EntityKind::Line,{0,0},{30,40},1,"line"}};sketch.relations={{"anchor",modeling::RelationKind::Coincident,{"line",modeling::PointRole::First},{}}};viewport->sketchChanged(sketch);
        command(window,"actionSmartDimension");
        for(int kind:{1,2}) {
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,viewport->m_sketch->entities[0].second));
            const modeling::Point2 position=kind==1?modeling::Point2{15,-10}:modeling::Point2{-15,20};QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,position));auto* editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);
            editor->findChild<QDoubleSpinBox*>("smartDimensionValue")->setValue(kind==1?48:64);QTest::mouseClick(editor->findChild<QDialogButtonBox*>("smartDimensionButtons")->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!window.findChild<QDialog*>("smartDimensionEditor"));
        }
        QVERIFY(std::abs(viewport->m_sketch->entities[0].second.x-48)<1e-6);QVERIFY(std::abs(viewport->m_sketch->entities[0].second.y-64)<1e-6);QVERIFY(viewport->m_sketchFullyDefined);
        command(window,"actionFinishSketch");window.close();
        WorkflowWindow fixedWindow;fixedWindow.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&fixedWindow));viewport=fixedWindow.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(fixedWindow);
        sketch={};sketch.entities={{modeling::EntityKind::Circle,{10,10},{},5,"circle"}};viewport->sketchChanged(sketch);command(fixedWindow,"actionDisplayDeleteRelations");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{10,10}));fixedWindow.findChild<QComboBox*>("relationKind")->setCurrentIndex(3);QTest::mouseClick(fixedWindow.findChild<QPushButton*>("confirmRelation"),Qt::LeftButton);
        QCOMPARE(viewport->m_sketch->relations.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->relations[0].kind,modeling::RelationKind::FixedPoint);
        auto* list=fixedWindow.findChild<QListWidget*>("sketchConstraints");list->setCurrentRow(0);QTest::mouseClick(fixedWindow.findChild<QPushButton*>("deleteConstraint"),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(viewport->m_sketch->relations.empty());
        QTest::mouseClick(fixedWindow.findChild<QPushButton*>("cancelDimension"),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);command(fixedWindow,"actionFinishSketch");QVERIFY(fixedWindow.errors.isEmpty());fixedWindow.close();
    }
    void inferredLineSpacingAndAngularDimensions() {
        using namespace modeling;WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        Sketch sketch;sketch.entities={{EntityKind::Line,{0,0},{20,0},1,"first"},{EntityKind::Line,{0,15},{20,15},1,"second"}};SketchRelation start{"fixed-start",RelationKind::FixedPoint,{"first",PointRole::First},{},{0,0}},end{"fixed-end",RelationKind::FixedPoint,{"first",PointRole::Second},{},{20,0}};sketch.relations={start,end};viewport->sketchChanged(sketch);command(window,"actionSmartDimension");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{10,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{10,15}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{30,8}));auto* editor=window.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);auto* value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");QCOMPARE(value->value(),15.0);value->setValue(25);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(viewport->m_sketch->dimensions[0].kind,DimensionKind::LineDistance);QVERIFY(std::abs(viewport->m_sketch->entities[1].first.y-25)<1e-6);QVERIFY(std::abs(viewport->m_sketch->entities[1].second.y-25)<1e-6);command(window,"actionFinishSketch");window.close();
        WorkflowWindow angular;angular.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&angular));viewport=angular.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(angular);sketch.entities[1]={EntityKind::Line,{0,0},{20,20},1,"second"};sketch.relations={start,end,{"join",RelationKind::Coincident,{"second",PointRole::First},{"first",PointRole::First}}};viewport->sketchChanged(sketch);command(angular,"actionSmartDimension");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{10,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{10,10}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{30,10}));editor=angular.findChild<QDialog*>("smartDimensionEditor");QVERIFY(editor);value=editor->findChild<QDoubleSpinBox*>("smartDimensionValue");QVERIFY(value->suffix().contains(QChar(0x00b0)));QVERIFY(std::abs(value->value()-45)<1e-6);value->setValue(60);QTest::keyClick(value,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(viewport->m_sketch->dimensions.size()==1);QCOMPARE(viewport->m_sketch->dimensions[0].kind,DimensionKind::LineAngle);QVERIFY(std::abs(dimensionMeasurement(*viewport->m_sketch,viewport->m_sketch->dimensions[0])-60)<1e-6);command(angular,"actionFinishSketch");QVERIFY(angular.errors.isEmpty());angular.close();
    }
    void sharedEdgeHouseRegionsExtrudeTogether() {
        WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);using namespace modeling;
        Sketch house;house.entities={{EntityKind::Line,{0,0},{30,0},1,"bottom"},{EntityKind::Line,{30,0},{30,30},1,"right"},{EntityKind::Line,{30,30},{0,30},1,"shared"},{EntityKind::Line,{0,30},{0,0},1,"left"},{EntityKind::Line,{0,30},{15,40},1,"roof-left"},{EntityKind::Line,{15,40},{30,30},1,"roof-right"}};viewport->sketchChanged(house);window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");auto* list=window.findChild<QListWidget*>("operationContour");QVERIFY(list);QCOMPARE(list->count(),2);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{15,15}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{15,35}));QCOMPARE(list->selectedItems().size(),2);window.findChild<QDoubleSpinBox*>("featureLength")->setValue(10);confirmOperation(window);QVERIFY(std::abs(volume(window.document().shape())-10500)<1e-6);QVERIFY(window.errors.isEmpty());window.close();
    }
    void selectMultipleRegionsCreatesMultipleBodies() {
        WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        modeling::Sketch sketch;sketch.entities={{modeling::EntityKind::Circle,{-15,0},{},5,"left"},{modeling::EntityKind::Circle,{15,0},{},5,"right"}};viewport->sketchChanged(sketch);
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-15,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{15,0}));
        auto* regions=window.findChild<QListWidget*>("operationContour");QCOMPARE(regions->selectedItems().size(),2);confirmOperation(window);
        QCOMPARE(window.document().features().back().contours.size(),std::size_t(2));int solids=0;for(TopExp_Explorer it(window.document().shape(),TopAbs_SOLID);it.More();it.Next())++solids;QCOMPARE(solids,2);QVERIFY(window.errors.isEmpty());window.close();
    }

    void activeSketchExtrudeCancelConfirmAndDirectCut() {
        QTemporaryDir directory; WorkflowWindow window;window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());
        createSketch(window);command(window,"actionLine");
        for(const auto p:{modeling::Point2{0,0},modeling::Point2{40,0},modeling::Point2{40,30},modeling::Point2{0,30},modeling::Point2{0,0}})
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,p));
        const auto active=*viewport->m_sketch;QCOMPARE(active.entities.size(),std::size_t(4));
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        command(window,"actionExtrude");
        auto* operation=window.findChild<QWidget*>("operationPanel");QVERIFY(operation);
        QVERIFY(!operation->isWindow());QVERIFY(window.findChild<QDialog*>("operationDialog")==nullptr);
        QCOMPARE(window.findChild<QStackedWidget*>("managerStack")->currentWidget(),operation);
        QCOMPARE(window.findChild<QDockWidget*>("historyDock")->windowTitle(),QString("PropertyManager"));
        QCOMPARE(operation->findChild<QLabel*>("operationStartingReference")->text(),QString("Sketch Plane"));
        QCOMPARE(operation->findChild<QLabel*>("operationEndCondition")->text(),QString("Blind"));
        QVERIFY(!operation->findChild<QPushButton*>("operationPreview"));
        QVERIFY(!window.findChild<QTreeWidget*>("featureTree")->isVisible());
        QCOMPARE(operation->findChild<QComboBox*>("operationSketch")->currentData().toString().toStdString(),window.document().features()[0].id);
        QVERIFY(!viewport->m_firstPoint);QCOMPARE(operation->findChild<QListWidget*>("operationContour")->currentRow(),-1);
        QCOMPARE(window.document().features()[0].sketch.entities.size(),std::size_t(0));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{20,15}));
        auto* depth=operation->findChild<QDoubleSpinBox*>("featureLength");depth->setValue(20);
        QVERIFY(!viewport->m_previewPresentation.IsNull());const double before=volume(viewport->m_previewPresentation->Shape());
        depth->setValue(35);QVERIFY(std::abs(volume(viewport->m_previewPresentation->Shape())/before-35.0/20.0)<1e-8);
        const auto previousDirection=viewport->m_graphics.view()->Camera()->Direction();
        const auto dragStart=viewport->rect().center(),dragEnd=dragStart+QPoint(80,50);
        QTest::mousePress(viewport,Qt::MiddleButton,Qt::NoModifier,dragStart);
        QMouseEvent orbit(QEvent::MouseMove,dragEnd,viewport->mapToGlobal(dragEnd),Qt::NoButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&orbit);QTest::mouseRelease(viewport,Qt::MiddleButton,Qt::NoModifier,dragEnd);
        QVERIFY(previousDirection.Angle(viewport->m_graphics.view()->Camera()->Direction())>0.01);
        depth->findChild<QLineEdit*>()->setText("--");
        QVERIFY(!operation->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY(operation->findChild<QLabel*>("operationStatus")->text().contains("valid positive depth"));
        depth->findChild<QLineEdit*>()->setText("35 mm");QVERIFY(!viewport->m_previewPresentation.IsNull());
        QTest::mouseClick(operation->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Cancel),Qt::LeftButton);
        QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());QCOMPARE(window.document().features().size(),std::size_t(1));
        QCOMPARE(window.findChild<QDockWidget*>("historyDock")->windowTitle(),QString("FeatureManager"));
        QCOMPARE(viewport->m_sketch->entities.size(),active.entities.size());QCOMPARE(viewport->m_sketch->relations.size(),active.relations.size());
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrude");
        operation=window.findChild<QWidget*>("operationPanel");operation->findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);
        operation->findChild<QDoubleSpinBox*>("featureLength")->setValue(25);
        QTest::mouseClick(operation->findChild<QPushButton*>("featureReverseDirection"),Qt::LeftButton);
        QCOMPARE(operation->findChild<QComboBox*>("featureDirection")->currentIndex(),1);QVERIFY(!viewport->m_previewPresentation.IsNull());
        operation->findChild<QComboBox*>("featureDirection")->setCurrentIndex(0);confirmOperation(window);
        QCOMPARE(window.document().features().size(),std::size_t(2));QCOMPARE(window.document().features()[0].sketch.relations.size(),active.relations.size());
        const double solidVolume=volume(window.document().shape());
        createSketch(window);command(window,"actionCircle");
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{20,15}));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,15}));
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrudedCut");
        operation=window.findChild<QWidget*>("operationPanel");QVERIFY(operation);
        QCOMPARE(operation->findChild<QComboBox*>("operationSketch")->currentData().toString().toStdString(),window.document().features()[2].id);
        operation->findChild<QCheckBox*>("featureThroughAll")->setChecked(true);
        operation->findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);
        command(window,"actionCancelFeature");QCOMPARE(window.document().features().size(),std::size_t(3));
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrudedCut");
        operation=window.findChild<QWidget*>("operationPanel");operation->findChild<QCheckBox*>("featureThroughAll")->setChecked(true);
        operation->findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);confirmOperation(window);
        QCOMPARE(window.document().features().size(),std::size_t(4));QVERIFY(volume(window.document().shape())<solidVolume);
        QVERIFY(window.document().features()[3].throughAll);
        window.path=directory.filePath("active-sketch.thepart");command(window,"actionSave");command(window,"actionNew");command(window,"actionOpen");
        QCOMPARE(window.document().features().size(),std::size_t(4));QCOMPARE(window.document().features()[0].sketch.relations.size(),active.relations.size());
        QVERIFY(BRepCheck_Analyzer(window.document().shape()).IsValid());QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void startupFileLoadsBeforeViewportPaintAndDisplaysSavedSolid() {
        QTemporaryDir directory;const auto path=directory.filePath("startup solid.thepart");auto part=document::PartDocument::createEmpty();modeling::Sketch sketch;sketch.entities={{modeling::EntityKind::Rectangle,{10,20},{60,50},1,"profile"}};const auto profile=part.addSketch(sketch);part.addExtrude(profile,{"profile"},25,modeling::Direction::Positive);part.save(path);
        WorkflowWindow window;auto* viewport=window.findChild<CadViewport*>();QVERIFY(!viewport->isReady());QVERIFY(window.openPartFile(path));QCOMPARE(window.document().features().size(),std::size_t(2));QCOMPARE(window.currentPath(),path);QVERIFY(!window.isWindowModified());
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(viewport->isReady());QVERIFY(!viewport->m_presentation.IsNull());QVERIFY(viewport->m_graphics.context()->IsDisplayed(viewport->m_presentation));QVERIFY(std::abs(volume(viewport->m_presentation->Shape())-50*30*25)<1e-6);QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void referenceGeometryXYZPickingSketchRegenerationAndPersistence() {
        using namespace modeling;WorkflowWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());
        const auto project=[&](const gp_Pnt& p){int x,y;viewport->m_graphics.view()->Convert(p.X(),p.Y(),p.Z(),x,y);return QPoint(qRound(x/viewport->devicePixelRatioF()),qRound(y/viewport->devicePixelRatioF()));};
        const auto confirm=[&]{auto* panel=window.findChild<QWidget*>("referenceGeometryPanel");QVERIFY(panel);auto* buttons=panel->findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!window.findChild<QWidget*>("referenceGeometryPanel"));};
        for(const auto p:{gp_Pnt(-20,-10,10),gp_Pnt(20,-10,10),gp_Pnt(0,20,25)}){const auto before=window.document().features().size();command(window,"actionReferencePoint");auto* panel=window.findChild<QWidget*>("referenceGeometryPanel");QVERIFY(panel);panel->findChild<QDoubleSpinBox*>("datumPositionX")->setValue(p.X());panel->findChild<QDoubleSpinBox*>("datumPositionY")->setValue(p.Y());panel->findChild<QDoubleSpinBox*>("datumPositionZ")->setValue(p.Z());QCOMPARE(window.document().features().size(),before);QVERIFY(viewport->m_datums.size()>before);confirm();QVERIFY(!QTest::currentTestFailed());}
        QCOMPARE(window.document().features().size(),std::size_t(3));QVERIFY(window.document().shape().IsNull());const auto firstPoint=window.document().features()[0].id;
        command(window,"actionReferenceAxis");auto* panel=window.findChild<QWidget*>("referenceGeometryPanel");auto* method=panel->findChild<QComboBox*>("datumMethod");method->setCurrentIndex(method->findData(int(DatumMethod::AxisTwoPoints)));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,project({-20,-10,10}));QVERIFY(panel->findChild<QComboBox*>("datumReference1")->currentIndex()>=0);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,project({20,-10,10}));QVERIFY(panel->findChild<QComboBox*>("datumReference2")->currentIndex()>=0);confirm();QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(window.document().features().size(),std::size_t(4));const auto axisId=window.document().features().back().id;
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(window.document().features().size(),std::size_t(3));QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(window.document().features().back().id,axisId);
        command(window,"actionReferencePlane");panel=window.findChild<QWidget*>("referenceGeometryPanel");method=panel->findChild<QComboBox*>("datumMethod");method->setCurrentIndex(method->findData(int(DatumMethod::PlaneThreePoints)));
        for(const auto p:{gp_Pnt(-20,-10,10),gp_Pnt(20,-10,10),gp_Pnt(0,20,25)})QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,project(p));confirm();QVERIFY(!QTest::currentTestFailed());const auto planeId=window.document().features().back().id;
        command(window,"actionCreateSketch");QVERIFY(viewport->m_sketchEditing);QCOMPARE(viewport->m_sketch->referencePlaneId,planeId);QVERIFY(std::abs(sketchAxes(*viewport->m_sketch).Direction().Y())>0.1);
        command(window,"actionRectangle");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{15,12}));command(window,"actionExtrude");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{7,5}));confirmOperation(window);QVERIFY(!QTest::currentTestFailed());QVERIFY(BRepCheck_Analyzer(window.document().shape()).IsValid());const double originalVolume=volume(window.document().shape());
        auto* tree=window.findChild<QTreeWidget*>("featureTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==firstPoint){tree->setCurrentItem(*it);break;}
        command(window,"actionEditFeature");panel=window.findChild<QWidget*>("referenceGeometryPanel");QVERIFY(panel);panel->findChild<QDoubleSpinBox*>("datumPositionZ")->setValue(18);confirm();QVERIFY(!QTest::currentTestFailed());QCOMPARE(window.document().features()[0].reference.position.Z(),18.0);QVERIFY(std::abs(volume(window.document().shape())-originalVolume)<1e-5);
        QTest::keyClick(viewport,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(window.document().features()[0].reference.position.Z(),10.0);QVERIFY(std::abs(volume(viewport->displayedShape())-originalVolume)<1e-5);QTest::keyClick(viewport,Qt::Key_Y,Qt::ControlModifier);QCOMPARE(window.document().features()[0].reference.position.Z(),18.0);
        QTemporaryDir directory;window.path=directory.filePath("reference-workflow.thepart");command(window,"actionSaveAs");const auto count=window.document().features().size();command(window,"actionNew");command(window,"actionOpen");QCOMPARE(window.document().features().size(),count);QCOMPARE(window.document().features()[0].id,firstPoint);QCOMPARE(viewport->m_datums.size(),std::size_t(5));QVERIFY(std::abs(volume(window.document().shape())-originalVolume)<1e-5);
        QVERIFY(!window.isWindowModified());command(window,"actionReferencePoint");panel=window.findChild<QWidget*>("referenceGeometryPanel");panel->findChild<QDoubleSpinBox*>("datumPositionX")->setValue(99);QTest::keyClick(viewport,Qt::Key_Escape);QVERIFY(!window.isWindowModified());QCOMPARE(window.document().features().size(),count);QVERIFY(!window.findChild<QAction*>("actionUndo")->isEnabled());QVERIFY(std::abs(volume(viewport->displayedShape())-originalVolume)<1e-5);QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void referencePickingPreservesDatumDependencyAtOrigin() {
        using namespace modeling;WorkflowWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());
        for(const double x:{0.0,20.0}){command(window,"actionReferencePoint");auto* panel=window.findChild<QWidget*>("referenceGeometryPanel");panel->findChild<QDoubleSpinBox*>("datumPositionX")->setValue(x);QTest::mouseClick(panel->findChild<QDialogButtonBox*>("referenceGeometryButtons")->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
        const auto first=window.document().features()[0].id,second=window.document().features()[1].id;command(window,"actionReferenceAxis");auto* panel=window.findChild<QWidget*>("referenceGeometryPanel");auto* method=panel->findChild<QComboBox*>("datumMethod");method->setCurrentIndex(method->findData(int(DatumMethod::AxisTwoPoints)));
        for(const double x:{0.0,20.0}){int px,py;viewport->m_graphics.view()->Convert(x,0,0,px,py);const QPoint pixel(qRound(px/viewport->devicePixelRatioF()),qRound(py/viewport->devicePixelRatioF()));const int option=viewport->referenceCandidateAt(pixel);QVERIFY(option>=0);QCOMPARE(viewport->m_referenceCandidates[std::size_t(option)].reference.source,ReferenceSource::Datum);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel);}
        auto* okay=panel->findChild<QDialogButtonBox*>("referenceGeometryButtons")->button(QDialogButtonBox::Ok);QVERIFY(okay->isEnabled());QTest::mouseClick(okay,Qt::LeftButton);QCOMPARE(window.document().features().back().dependencies,(std::vector<std::string>{first,second}));QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void referenceGeometryInvalidInputAndCancelAreTransactional() {
        using namespace modeling;WorkflowWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());
        command(window,"actionReferenceAxis");auto* panel=window.findChild<QWidget*>("referenceGeometryPanel");QVERIFY(panel);auto* buttons=panel->findChild<QDialogButtonBox*>("referenceGeometryButtons");auto* initialMethod=panel->findChild<QComboBox*>("datumMethod");initialMethod->setCurrentIndex(initialMethod->findData(int(DatumMethod::AxisXYZ)));auto* z=panel->findChild<QDoubleSpinBox*>("datumDirectionZ");z->setValue(0);QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::keyClick(z,Qt::Key_Return);QVERIFY(window.document().features().empty());QVERIFY(window.findChild<QWidget*>("referenceGeometryPanel"));z->setValue(1);QVERIFY2(buttons->button(QDialogButtonBox::Ok)->isEnabled(),qPrintable(panel->findChild<QLabel*>("operationStatus")->text()));QTest::keyClick(z,Qt::Key_Escape);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(window.document().features().empty());QVERIFY(viewport->m_datums.empty());QVERIFY(!window.findChild<QWidget*>("referenceGeometryPanel"));
        command(window,"actionReferencePlane");panel=window.findChild<QWidget*>("referenceGeometryPanel");initialMethod=panel->findChild<QComboBox*>("datumMethod");initialMethod->setCurrentIndex(initialMethod->findData(int(DatumMethod::PlaneXYZ)));panel->findChild<QDoubleSpinBox*>("datumPositionZ")->setValue(30);auto* normal=panel->findChild<QDoubleSpinBox*>("datumDirectionZ");normal->setValue(0);buttons=panel->findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());normal->setValue(1);QTest::keyClick(normal,Qt::Key_Return);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCOMPARE(window.document().features().size(),std::size_t(1));QCOMPARE(window.document().features()[0].datum.frame.Location().Z(),30.0);
        command(window,"actionReferencePlane");panel=window.findChild<QWidget*>("referenceGeometryPanel");auto* method=panel->findChild<QComboBox*>("datumMethod");QCOMPARE(method->currentData().toInt(),int(DatumMethod::PlaneOffset));QVERIFY(panel->findChild<QComboBox*>("datumReference1")->currentIndex()>=0);panel->findChild<QDoubleSpinBox*>("datumValue")->setValue(10);QTest::keyClick(viewport,Qt::Key_Escape);QCOMPARE(window.document().features().size(),std::size_t(1));
        command(window,"actionReferencePoint");panel=window.findChild<QWidget*>("referenceGeometryPanel");method=panel->findChild<QComboBox*>("datumMethod");method->setCurrentIndex(method->findData(int(DatumMethod::PointAxisPlane)));buttons=panel->findChild<QDialogButtonBox*>("referenceGeometryButtons");QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());QTest::keyClick(viewport,Qt::Key_Escape);QCOMPARE(window.document().features().size(),std::size_t(1));QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void featureDepthTypingContourFeedbackAndMaterialPreview() {
        WorkflowWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        command(window,"actionRectangle");
        for(const auto p:{modeling::Point2{-40,-15},modeling::Point2{-10,15},modeling::Point2{10,-15},modeling::Point2{40,15}})QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,p));
        command(window,"actionExtrude");auto* panel=window.findChild<QWidget*>("operationPanel");QVERIFY(panel);
        auto* depth=panel->findChild<QDoubleSpinBox*>("featureLength");auto* editor=depth->findChild<QLineEdit*>();QVERIFY(editor);
        QTRY_VERIFY(depth->hasFocus()||editor->hasFocus());QVERIFY(editor->hasSelectedText());
        QTest::keyClicks(QApplication::focusWidget(),"23.5");QCOMPARE(depth->value(),23.5);
        auto* contours=panel->findChild<QListWidget*>("operationContour");QCOMPARE(contours->selectedItems().size(),0);
        const auto profileArea=[&]{double area=0;for(const auto& p:viewport->m_selectedContourPresentations){GProp_GProps props;BRepGProp::SurfaceProperties(p->Shape(),props);area+=props.Mass();}return area;};
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-25,0}));
        QCOMPARE(contours->selectedItems().size(),1);QCOMPARE(viewport->m_selectedContourPresentations.size(),std::size_t(1));
        QCOMPARE(viewport->m_materialPreviewPresentations.size(),std::size_t(1));
        const double firstArea=profileArea();
        QVERIFY(std::abs(volume(viewport->m_materialPreviewPresentations[0]->Shape())-firstArea*23.5)<1e-5);
        QTRY_VERIFY(depth->hasFocus()||editor->hasFocus());QVERIFY(editor->hasSelectedText());
        const auto direction=viewport->m_graphics.view()->Camera()->Direction();const auto center=viewport->m_graphics.view()->Camera()->Center();const auto scale=viewport->m_graphics.view()->Camera()->Scale();
        QTest::keyClicks(QApplication::focusWidget(),"17.25");QCOMPARE(depth->value(),17.25);
        QVERIFY(direction.Angle(viewport->m_graphics.view()->Camera()->Direction())<1e-10);QVERIFY(center.Distance(viewport->m_graphics.view()->Camera()->Center())<1e-10);QCOMPARE(viewport->m_graphics.view()->Camera()->Scale(),scale);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,0}));QCOMPARE(contours->selectedItems().size(),2);QCOMPARE(viewport->m_selectedContourPresentations.size(),std::size_t(2));
        QVERIFY(panel->findChild<QLabel*>("operationSelectionCount")->text().startsWith("2 regions"));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{-25,0}));QCOMPARE(contours->selectedItems().size(),1);QCOMPARE(viewport->m_selectedContourPresentations.size(),std::size_t(1));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,0}));QVERIFY(contours->selectedItems().isEmpty());QVERIFY(viewport->m_selectedContourPresentations.empty());QVERIFY(viewport->m_previewPresentation.IsNull());QVERIFY(viewport->m_materialPreviewPresentations.empty());
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,0}));
        // Viewport navigation may own focus; numeric input must still start depth entry.
        viewport->setFocus();QTest::keyClick(viewport,Qt::Key_1);QTest::keyClick(QApplication::focusWidget(),Qt::Key_2);QCOMPARE(depth->value(),12.0);
        editor->selectAll();QTest::keyClicks(editor,"0");QTest::keyClick(editor,Qt::Key_Return);
        QVERIFY(window.findChild<QWidget*>("operationPanel"));QCOMPARE(window.document().features().size(),std::size_t(1));QVERIFY(viewport->m_previewPresentation.IsNull());
        const double confirmedArea=profileArea();depth->selectAll();QTest::keyClicks(QApplication::focusWidget(),"18");QCOMPARE(depth->value(),18.0);QTest::keyClick(QApplication::focusWidget(),Qt::Key_Return);
        QCOMPARE(window.document().features().size(),std::size_t(2));QVERIFY(viewport->m_selectedContourPresentations.empty());QVERIFY(viewport->m_materialPreviewPresentations.empty());
        QVERIFY(std::abs(volume(window.document().shape())-confirmedArea*18)<1e-5);
        createSketch(window);command(window,"actionCircle");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,0}));QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{29,0}));
        command(window,"actionExtrudedCut");panel=window.findChild<QWidget*>("operationPanel");depth=panel->findChild<QDoubleSpinBox*>("featureLength");editor=depth->findChild<QLineEdit*>();
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,0}));QTest::keyClicks(QApplication::focusWidget(),"5");QCOMPARE(depth->value(),5.0);
        QCOMPARE(viewport->m_materialPreviewPresentations.size(),std::size_t(1));GProp_GProps cutProperties;BRepGProp::SurfaceProperties(viewport->m_selectedContourPresentations[0]->Shape(),cutProperties);QVERIFY(std::abs(volume(viewport->m_materialPreviewPresentations[0]->Shape())-cutProperties.Mass()*5)<1e-4);
        QTest::keyClick(QApplication::focusWidget(),Qt::Key_Escape);QCOMPARE(window.document().features().size(),std::size_t(3));QVERIFY(viewport->m_sketchEditing);QVERIFY(viewport->m_selectedContourPresentations.empty());QVERIFY(viewport->m_materialPreviewPresentations.empty());
        QVERIFY(window.errors.isEmpty());QVERIFY(window.close());
    }
    void invalidActiveSketchStaysEditable() {
        WorkflowWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        command(window,"actionExtrude");QCOMPARE(window.errors.size(),1);QVERIFY(window.errors[0].contains("closed contour"));
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());QVERIFY(!window.findChild<QWidget*>("operationPanel"));
        QCOMPARE(window.document().features().size(),std::size_t(1));
        command(window,"actionLine");QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{0,0}));
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{20,10}));
        command(window,"actionExtrude");QCOMPARE(window.errors.size(),2);QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());QVERIFY(window.close());
    }
    void rectangleExtrudeCircleCutEditSaveReopen()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkflowWindow window;
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.document().shape().IsNull());
        QCOMPARE(window.document().features().size(), std::size_t(0));
        createSketch(window);
        command(window, "actionRectangle");
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {-30, -20}));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {30, 20}));
        auto* entities = window.findChild<QListWidget*>("sketchEntities");
        QCOMPARE(entities->count(), 4);
        entities->setCurrentRow(0);
        editEntity(window, modeling::EntityKind::Line, {-30, -20}, {30, -20});
        entities->setCurrentRow(2);
        editEntity(window, modeling::EntityKind::Line, {30, 20}, {-30, 20});
        // A second independent region proves the feature does not consume the entire sketch.
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {45, -10}));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {60, 10}));
        QCOMPARE(entities->count(), 8);
        command(window, "actionFinishSketch");
        QCOMPARE(window.document().features().front().sketch.entities.size(), std::size_t(8));
        command(window, "actionExtrude");
        auto* operation = window.findChild<QWidget*>("operationPanel");
        QVERIFY(operation);
        QVERIFY(operation->findChild<QCheckBox*>("featureThroughAll")->isHidden());
        auto* contours = operation->findChild<QListWidget*>("operationContour");
        QCOMPARE(contours->count(), 2);
        QCOMPARE(contours->currentRow(), -1);
        QVERIFY(!operation->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->isEnabled());
        operation->findChild<QDoubleSpinBox*>("featureLength")->setValue(30);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {0, 0}));
        QVERIFY(contours->currentRow() >= 0);
        QVERIFY(!viewport->m_previewPresentation.IsNull());
        QVERIFY(window.document().shape().IsNull()); // Preview is not a committed feature.
        confirmOperation(window);
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(window.document().features().size(), std::size_t(2));
        QVERIFY(BRepCheck_Analyzer(window.document().shape()).IsValid());
        QVERIFY(std::abs(volume(window.document().shape()) - 60 * 40 * 30) < 1e-5);
        createSketch(window);
        command(window, "actionCircle");
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {0, 0}));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {8, 0}));
        QCOMPARE(entities->count(), 1);
        entities->setCurrentRow(0);
        editEntity(window, modeling::EntityKind::Circle, {0, 0}, {}, 8);
        command(window, "actionFinishSketch");
        command(window, "actionExtrudedCut");
        operation = window.findChild<QWidget*>("operationPanel");
        QVERIFY(operation);
        QVERIFY(!operation->findChild<QCheckBox*>("featureThroughAll")->isHidden());
        operation->findChild<QCheckBox*>("featureThroughAll")->setChecked(true);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {0, 0}));
        confirmOperation(window);
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(window.document().features().size(), std::size_t(4));
        const double area = 60 * 40 - std::acos(-1.0) * 8 * 8;
        QVERIFY(std::abs(volume(window.document().shape()) - area * 30) < 1e-4);
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QCOMPARE(tree->topLevelItem(0)->childCount(), 6);
        tree->setCurrentItem(tree->topLevelItem(0)->child(4));
        command(window, "actionEditFeature");
        operation = window.findChild<QWidget*>("operationPanel");
        QVERIFY(operation);
        QCOMPARE(operation->findChild<QListWidget*>("operationContour")->currentRow(), 0);
        operation->findChild<QDoubleSpinBox*>("featureLength")->setValue(45);
        confirmOperation(window);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(BRepCheck_Analyzer(window.document().shape()).IsValid());
        QVERIFY(std::abs(volume(window.document().shape()) - area * 45) < 1e-4);
        QCOMPARE(window.document().features().at(1).length, 45.0);
        QVERIFY(window.document().features().at(3).throughAll);
        window.path = directory.filePath("workflow.thepart");
        command(window, "actionSave");
        QVERIFY(QFileInfo::exists(window.path));
        QVERIFY(!window.isWindowModified());
        QVERIFY(window.close());
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        command(window, "actionNew");
        QVERIFY(window.document().features().empty());
        command(window, "actionOpen");
        QCOMPARE(tree->topLevelItem(0)->childCount(), 6);
        QCOMPARE(window.document().features().at(1).length, 45.0);
        QCOMPARE(window.document().features().at(2).sketch.entities.front().radius, 8.0);
        QVERIFY(window.document().features().at(3).throughAll);
        QVERIFY(BRepCheck_Analyzer(window.document().shape()).IsValid());
        QVERIFY(std::abs(volume(window.document().shape()) - area * 45) < 1e-4);
        command(window, "actionIsometric");
        QApplication::processEvents();
        QVERIFY(window.errors.isEmpty());
        const QString artifactDirectory = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if (!artifactDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(artifactDirectory));
            window.document().save(artifactDirectory + "/rectangle-hole.thepart");
            QVERIFY(tree->grab().save(artifactDirectory + "/part-tree.png"));
            viewport->m_graphics.view()->Redraw();
            QVERIFY(viewport->m_graphics.view()->Dump((artifactDirectory + "/rectangle-hole.png").toUtf8().constData()));
        }
        QVERIFY(window.close());
    }
};
}
QTEST_MAIN(thecad::ui::ModelingWorkflowTest)
#include "ModelingWorkflowTests.moc"
