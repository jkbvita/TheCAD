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
        viewport->m_view->Convert(world.X(), world.Y(), world.Z(), x, y);
        return {qRound(x / viewport->devicePixelRatioF()), qRound(y / viewport->devicePixelRatioF())};
    }
    void createSketch(WorkflowWindow& window)
    {
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
    void sketchToolActionsFollowMode() {
        WorkflowWindow window;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());createSketch(window);
        const auto checked=[&](const char* name){return window.findChild<QAction*>(name)->isChecked();};
        QVERIFY(checked("actionLine"));
        command(window,"actionSmartDimension");QVERIFY(checked("actionSmartDimension"));QVERIFY(!checked("actionLine"));QVERIFY(viewport->m_smartDimensionPicking);
        command(window,"actionCircle");QVERIFY(checked("actionCircle"));QVERIFY(!checked("actionSmartDimension"));QVERIFY(!viewport->m_smartDimensionPicking);
        QTest::keyClick(viewport,Qt::Key_Escape);QVERIFY(!checked("actionCircle"));QVERIFY(checked("actionSelectSketchEntity"));
        command(window,"actionSmartDimension");QTest::keyClick(viewport,Qt::Key_Escape);QVERIFY(!checked("actionSmartDimension"));QVERIFY(!viewport->m_smartDimensionPicking);
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
        QCOMPARE(viewport->m_sketch->dimensions.size(),std::size_t(1));QCOMPARE(viewport->m_sketch->dimensions[0].value,60.0);QVERIFY(viewport->m_smartDimensionPicking);QVERIFY(window.findChild<QAction*>("actionSmartDimension")->isChecked());const auto id=viewport->m_sketch->dimensions[0].id;
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
        auto* tree=window.findChild<QTreeWidget*>("featureTree");QTreeWidgetItem* item=nullptr;for(int i=0;i<tree->topLevelItemCount();++i){auto* root=tree->topLevelItem(i);for(int n=0;n<root->childCount();++n)if(root->child(n)->data(0,Qt::UserRole).toString()=="sketch-1")item=root->child(n);}QVERIFY(item);tree->setCurrentItem(item);command(window,"actionEditFeature");
        command(window,"actionSelectSketchEntity");QTest::mouseDClick(viewport,Qt::LeftButton,Qt::NoModifier,point(viewport,{25,-10}));auto* value=window.findChild<QDoubleSpinBox*>("smartDimensionValue");QVERIFY(value);value->setValue(80);QTest::mouseClick(window.findChild<QDialogButtonBox*>("smartDimensionButtons")->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(std::abs(volume(viewport->m_shape)-24000)<1e-6);command(window,"actionFinishSketch");QVERIFY(std::abs(volume(window.document().shape())-24000)<1e-6);QCOMPARE(window.document().features()[0].sketch.dimensions[0].value,80.0);QVERIFY(window.errors.isEmpty());window.close();
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
        const auto previousDirection=viewport->m_view->Camera()->Direction();
        const auto dragStart=viewport->rect().center(),dragEnd=dragStart+QPoint(80,50);
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,dragStart);
        QMouseEvent orbit(QEvent::MouseMove,dragEnd,viewport->mapToGlobal(dragEnd),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(viewport,&orbit);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,dragEnd);
        QVERIFY(previousDirection.Angle(viewport->m_view->Camera()->Direction())>0.01);
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
        QCOMPARE(tree->topLevelItem(0)->childCount(), 4);
        tree->setCurrentItem(tree->topLevelItem(0)->child(1));
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
        QCOMPARE(tree->topLevelItem(0)->childCount(), 4);
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
            viewport->m_view->Redraw();
            QVERIFY(viewport->m_view->Dump((artifactDirectory + "/rectangle-hole.png").toUtf8().constData()));
        }
        QVERIFY(window.close());
    }
};
}
QTEST_MAIN(thecad::ui::ModelingWorkflowTest)
#include "ModelingWorkflowTests.moc"
