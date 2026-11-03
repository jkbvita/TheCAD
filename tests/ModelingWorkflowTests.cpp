#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <BRepCheck_Analyzer.hxx>
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
        QVERIFY(!viewport->m_firstPoint);QCOMPARE(operation->findChild<QComboBox*>("operationContour")->currentIndex(),-1);
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
        operation=window.findChild<QWidget*>("operationPanel");operation->findChild<QComboBox*>("operationContour")->setCurrentIndex(0);
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
        operation->findChild<QComboBox*>("operationContour")->setCurrentIndex(0);
        command(window,"actionCancelFeature");QCOMPARE(window.document().features().size(),std::size_t(3));
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));
        window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(0);command(window,"actionExtrudedCut");
        operation=window.findChild<QWidget*>("operationPanel");operation->findChild<QCheckBox*>("featureThroughAll")->setChecked(true);
        operation->findChild<QComboBox*>("operationContour")->setCurrentIndex(0);confirmOperation(window);
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
        auto* contours = operation->findChild<QComboBox*>("operationContour");
        QCOMPARE(contours->count(), 2);
        QCOMPARE(contours->currentIndex(), -1);
        QVERIFY(!operation->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->isEnabled());
        operation->findChild<QDoubleSpinBox*>("featureLength")->setValue(30);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point(viewport, {0, 0}));
        QVERIFY(contours->currentIndex() >= 0);
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
        QCOMPARE(operation->findChild<QComboBox*>("operationContour")->currentIndex(), 0);
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
