#include "ui/MainWindow.h"
#include <QStackedWidget>
#include "ui/CadViewport.h"
#include "document/PartDocument.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QFile>
#include <QFileInfo>
#include <QMenuBar>
#include <QTabWidget>
#include <QToolBar>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTest>

namespace thecad::ui {

class TestWindow final : public MainWindow {
public:
    using Choice = UnsavedChoice;
    QString openPath;
    QString savePath;
    Choice choice = Choice::Discard;
    int saveDialogCount = 0;
    int promptCount = 0;
    QStringList errors;

    void trigger(const char* name)
    {
        auto* action = findChild<QAction*>(name);
        Q_ASSERT(action);
        action->trigger();
        QApplication::processEvents();
    }

protected:
    QString chooseOpenPath() override { return openPath; }
    QString chooseSavePath() override { ++saveDialogCount; return savePath; }
    UnsavedChoice confirmUnsaved() override { ++promptCount; return choice; }
    void reportError(const QString& message) override { errors.append(message); }
};

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void consumedSketchIsSharedByEveryFeatureAndRemainsEditable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto part = document::PartDocument::createEmpty();
        modeling::Sketch sketch;
        sketch.entities = {
            {modeling::EntityKind::Circle, {0, 0}, {}, 5, "circle-a"},
            {modeling::EntityKind::Circle, {30, 0}, {}, 4, "circle-b"}};
        const auto sketchId = part.addSketch(sketch);
        const auto path = directory.filePath("shared-sketch.thepart");
        part.save(path);
        TestWindow window;
        window.openPath = path;
        window.trigger("actionOpen");
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 5);
        const auto sourceId = QString::fromStdString(sketchId);
        QCOMPARE(tree->topLevelItem(0)->child(4)->data(0, Qt::UserRole).toString(), sourceId);

        tree->setCurrentItem(tree->topLevelItem(0)->child(4));
        window.trigger("actionExtrude");
        auto* panel = window.findChild<QWidget*>("operationPanel");
        QVERIFY(panel);
        QCOMPARE(panel->findChild<QComboBox*>("operationSketch")->currentData().toString(), sourceId);
        panel->findChild<QListWidget*>("operationContour")->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
        panel->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->click();
        QVERIFY(window.errors.isEmpty());
        QCOMPARE(window.document().features().size(), std::size_t(2));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 5);
        QCOMPARE(tree->topLevelItem(0)->child(4)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->child(4)->child(0)->data(0, Qt::UserRole).toString(), sourceId);

        // Undo exposes the unused sketch again; redo nests the same history entry.
        window.trigger("actionUndo");
        QCOMPARE(tree->topLevelItem(0)->child(4)->data(0, Qt::UserRole).toString(), sourceId);
        QCOMPARE(tree->topLevelItem(0)->child(4)->childCount(), 0);
        window.trigger("actionRedo");
        QCOMPARE(tree->topLevelItem(0)->child(4)->childCount(), 1);

        tree->setCurrentItem(tree->topLevelItem(0)->child(4)->child(0));
        window.trigger("actionExtrude");
        panel = window.findChild<QWidget*>("operationPanel");
        QVERIFY(panel);
        QCOMPARE(panel->findChild<QComboBox*>("operationSketch")->currentData().toString(), sourceId);
        panel->findChild<QListWidget*>("operationContour")->setCurrentRow(1, QItemSelectionModel::ClearAndSelect);
        tree->topLevelItem(0)->child(4)->setExpanded(false);
        panel->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->click();
        QVERIFY(window.errors.isEmpty());
        QCOMPARE(window.document().features().size(), std::size_t(3));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 6);
        QVERIFY(!tree->topLevelItem(0)->child(4)->isExpanded());
        QCOMPARE(tree->topLevelItem(0)->child(5)->child(0)->data(0, Qt::UserRole).toString(), sourceId);

        // A cut can reuse that sketch too, without creating a copied sketch.
        tree->setCurrentItem(tree->topLevelItem(0)->child(5)->child(0));
        window.trigger("actionExtrudedCut");
        panel = window.findChild<QWidget*>("operationPanel");
        QVERIFY(panel);
        QCOMPARE(panel->findChild<QComboBox*>("operationSketch")->currentData().toString(), sourceId);
        panel->findChild<QDoubleSpinBox*>("featureLength")->setValue(2);
        panel->findChild<QListWidget*>("operationContour")->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
        panel->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->click();
        QVERIFY(window.errors.isEmpty());
        QCOMPARE(window.document().features().size(), std::size_t(4));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 7);
        for (int i = 4; i < 7; ++i) {
            auto* feature = tree->topLevelItem(0)->child(i);
            QCOMPARE(feature->childCount(), 1);
            QCOMPARE(feature->child(0)->data(0, Qt::UserRole).toString(), sourceId);
        }

        // Edit through the second occurrence; all three dependent solids regenerate.
        tree->setCurrentItem(tree->topLevelItem(0)->child(5)->child(0));
        window.trigger("actionEditFeature");
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        window.findChild<QListWidget*>("sketchEntities")->setCurrentRow(0);
        window.findChild<QDoubleSpinBox*>("propertyRadius")->setValue(6);
        window.findChild<QPushButton*>("applyEntityProperties")->click();
        window.trigger("actionFinishSketch");
        QVERIFY(window.errors.isEmpty());
        QCOMPARE(window.document().features().front().sketch.entities.front().radius, 6.0);
        const double expectedVolume = std::acos(-1.0) * (36 * 8 + 16 * 10);
        GProp_GProps properties;
        BRepGProp::VolumeProperties(window.document().shape(), properties);
        QVERIFY(std::abs(properties.Mass() - expectedVolume) < 1e-5);
        QCOMPARE(tree->currentItem(), tree->topLevelItem(0)->child(5)->child(0));

        window.trigger("actionSave");
        window.trigger("actionNew");
        window.trigger("actionOpen");
        QCOMPARE(tree->topLevelItem(0)->childCount(), 7);
        for (int i = 4; i < 7; ++i)
            QCOMPARE(tree->topLevelItem(0)->child(i)->child(0)->data(0, Qt::UserRole).toString(), sourceId);
        QCOMPARE(window.document().features().size(), std::size_t(4));
        BRepGProp::VolumeProperties(window.document().shape(), properties);
        QVERIFY(std::abs(properties.Mass() - expectedVolume) < 1e-5);
        QVERIFY(window.errors.isEmpty());
    }

    void fileMenuAndShortcuts()
    {
        TestWindow window;
        QVERIFY(window.isWindowModified());
        QVERIFY(window.currentPath().isEmpty());
        QCOMPARE(window.menuBar()->actions().size(), 7);
        const QList<QPair<QString, QKeySequence>> commands = {
            {"actionNew", QKeySequence::New}, {"actionOpen", QKeySequence::Open},
            {"actionSave", QKeySequence::Save}, {"actionSaveAs", QKeySequence::SaveAs},
            {"actionExit", QKeySequence(Qt::ALT | Qt::Key_F4)}};
        for (const auto& command : commands) {
            auto* action = window.findChild<QAction*>(command.first);
            QVERIFY(action);
            QVERIFY(action->isEnabled());
            QCOMPARE(action->shortcut(), command.second);
        }
    }

    void commandManagerShowsFunctionalAndFutureTools()
    {
        TestWindow window;
        const QStringList menuNames = {"&File", "&Edit", "&View", "&Insert", "&Tools", "&Window", "&Help"};
        const auto menus = window.menuBar()->actions();
        QCOMPARE(menus.size(), menuNames.size());
        for (int i = 0; i < menus.size(); ++i) QCOMPARE(menus[i]->text(), menuNames[i]);
        auto* tabs = window.findChild<QTabWidget*>("modelingTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(0), QString("Features"));
        QCOMPARE(tabs->tabText(1), QString("Sketch"));
        QCOMPARE(tabs->tabText(2), QString("Evaluate"));
        QCOMPARE(tabs->tabPosition(), QTabWidget::South);
        QCOMPARE(QApplication::style()->objectName(), QString("fusion"));
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QVERIFY(tree->palette().color(QPalette::Text).lightness() < 128);
        QVERIFY(tree->palette().color(QPalette::Base).lightness() > 200);
        auto* commands = window.findChild<QToolBar*>("modelingToolbar");
        QVERIFY(commands && !commands->isMovable() && !commands->isFloatable());
        for (const auto* name : {"actionArc", "actionSpline",
                "actionTrimEntities", "actionConvertEntities", "actionOffsetEntities",
                "actionMirrorEntities", "actionLinearSketchPattern", "actionMoveEntities",
                "actionRepairSketch", "actionShadedSketchContours",
                "actionMeasure", "actionMassProperties", "actionCheckGeometry"}) {
            auto* tool = window.findChild<QAction*>(name);
            QVERIFY2(tool, name);
            QVERIFY(!tool->isEnabled());
            QVERIFY(tool->toolTip().contains("not implemented"));
            QVERIFY(!tool->icon().isNull());
        }
        QVERIFY(window.findChild<QGroupBox*>("entityProperties"));
        QVERIFY(window.findChild<QDoubleSpinBox*>("propertyFirstX"));
        QVERIFY(window.findChild<QPushButton*>("applyEntityProperties"));
    }
    void saveReusesPathAndSaveAsSwitchesPath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.savePath = directory.filePath(QString::fromUtf8("díl.thepart"));
        window.trigger("actionSave");
        QCOMPARE(window.errors.size(), 0);
        QVERIFY(!window.isWindowModified());
        QCOMPARE(window.currentPath(), window.savePath);
        QCOMPARE(window.saveDialogCount, 1);
        QVERIFY(QFileInfo::exists(window.savePath));
        const auto original = document::PartDocument::load(window.savePath);
        QVERIFY(original.features().empty());
        QVERIFY(original.shape().IsNull());

        const QString firstPath = window.currentPath();
        window.savePath = directory.filePath("second.thepart");
        window.setWindowModified(true);
        window.trigger("actionSave");
        QCOMPARE(window.currentPath(), firstPath);
        QCOMPARE(window.saveDialogCount, 1);
        QVERIFY(!window.isWindowModified());
        QVERIFY(!QFileInfo::exists(window.savePath));

        window.trigger("actionSaveAs");
        QCOMPARE(window.currentPath(), window.savePath);
        QCOMPARE(window.saveDialogCount, 2);
        QVERIFY(QFileInfo::exists(firstPath));
        QVERIFY(QFileInfo::exists(window.savePath));
        const auto second = document::PartDocument::load(window.savePath);
        QVERIFY(second.features().empty());
        window.trigger("actionFitAll");
        window.trigger("actionIsometric");
        QVERIFY(!window.isWindowModified());
    }

    void openRestoresParametersThenNewReplacesDocument()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString fixture = directory.filePath("custom.thepart");
        document::PartDocument::createBox({12.5, 34.0, 56.0}).save(fixture);
        TestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>();
        QVERIFY(viewport);
        QVERIFY2(viewport->isReady(), qPrintable(viewport->initializationError()));
        window.openPath = fixture;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 0);
        QCOMPARE(window.promptCount, 1);
        QCOMPARE(window.currentPath(), fixture);
        QVERIFY(!window.isWindowModified());
        QCOMPARE(window.document().parameters().width, 12.5);
        QCOMPARE(window.document().parameters().depth, 34.0);
        QCOMPARE(window.document().parameters().height, 56.0);
        QVERIFY(!window.document().shape().IsNull());
        window.trigger("actionNew");
        QCOMPARE(window.promptCount, 1);
        QVERIFY(window.currentPath().isEmpty());
        QVERIFY(window.isWindowModified());
        QVERIFY(window.document().features().empty());
        QVERIFY(window.document().shape().IsNull());
    }

    void dirtySamePathOpenSavesCurrentModelBeforeReload()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("same-part.thepart");
        document::PartDocument::createBox({17.0, 19.0, 23.0}).save(path);
        TestWindow window;
        window.openPath = path;
        window.trigger("actionOpen");
        QCOMPARE(window.document().parameters().width, 17.0);

        // Distinguish the in-memory document from the pre-prompt disk snapshot.
        document::PartDocument::createBox({3.0, 4.0, 5.0}).save(path);
        window.setWindowModified(true);
        window.choice = TestWindow::Choice::Save;
        const int prompts = window.promptCount;
        const int saveDialogs = window.saveDialogCount;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 0);
        QCOMPARE(window.promptCount, prompts + 1);
        QCOMPARE(window.saveDialogCount, saveDialogs);
        QCOMPARE(window.currentPath(), path);
        QVERIFY(!window.isWindowModified());
        QCOMPARE(window.document().parameters().width, 17.0);
        QCOMPARE(window.document().parameters().depth, 19.0);
        QCOMPARE(window.document().parameters().height, 23.0);
        const auto saved = document::PartDocument::load(path);
        QCOMPARE(saved.parameters().width, 17.0);
        QCOMPARE(saved.parameters().depth, 19.0);
        QCOMPARE(saved.parameters().height, 23.0);
    }

    void failedSaveAsPreservesExistingPathAndDocument()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.savePath = directory.filePath("current.thepart");
        window.trigger("actionSave");
        QCOMPARE(window.errors.size(), 0);
        const QString originalPath = window.currentPath();
        const auto originalShape = window.document().shape();
        QFile originalFile(originalPath);
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        const auto originalBytes = originalFile.readAll();
        originalFile.close();

        window.setWindowModified(true);
        window.savePath = directory.filePath("missing/subdirectory/failed.thepart");
        window.trigger("actionSaveAs");
        QCOMPARE(window.errors.size(), 1);
        QCOMPARE(window.currentPath(), originalPath);
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.document().shape().IsNull(), originalShape.IsNull());
        QVERIFY(window.document().features().empty());
        QVERIFY(window.document().shape().IsNull());
        QVERIFY(!QFileInfo::exists(window.savePath));
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        QCOMPARE(originalFile.readAll(), originalBytes);
    }

    void cancelOpenOrSaveAsPreservesCurrentDocument()
    {
        TestWindow window;
        window.trigger("actionOpen");
        window.trigger("actionSaveAs");
        QCOMPARE(window.promptCount, 0);
        QVERIFY(window.currentPath().isEmpty());
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.errors.size(), 0);
    }

    void invalidOpenDoesNotPromptOrDestroyCurrentDocument()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString good = directory.filePath("good.thepart");
        document::PartDocument::createBox({17.0, 19.0, 23.0}).save(good);
        TestWindow window;
        window.openPath = good;
        window.trigger("actionOpen");
        window.setWindowModified(true);
        const int prompts = window.promptCount;
        const auto previousShape = window.document().shape();
        const QString invalid = directory.filePath("invalid.thepart");
        QFile file(invalid);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a part document");
        file.close();
        window.openPath = invalid;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 1);
        QCOMPARE(window.promptCount, prompts);
        QCOMPARE(window.currentPath(), good);
        QCOMPARE(window.document().parameters().width, 17.0);
        QVERIFY(window.document().shape().IsSame(previousShape));
        QVERIFY(window.isWindowModified());
    }

    void unsavedCancelBlocksNewAndOpen()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString fixture = directory.filePath("other.thepart");
        document::PartDocument::createBox({3.0, 4.0, 5.0}).save(fixture);
        TestWindow window;
        window.choice = TestWindow::Choice::Cancel;
        window.trigger("actionNew");
        window.openPath = fixture;
        window.trigger("actionOpen");
        QCOMPARE(window.promptCount, 2);
        QVERIFY(window.currentPath().isEmpty());
        QVERIFY(window.document().features().empty());
        QVERIFY(window.document().shape().IsNull());
        QVERIFY(window.isWindowModified());
    }

    void unsavedSaveCreatesFileBeforeReplacement()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.choice = TestWindow::Choice::Save;
        window.savePath = directory.filePath("saved.thepart");
        window.trigger("actionNew");
        QCOMPARE(window.promptCount, 1);
        QCOMPARE(window.errors.size(), 0);
        QVERIFY(QFileInfo::exists(window.savePath));
        QVERIFY(window.isWindowModified());
        QVERIFY(window.currentPath().isEmpty());
    }

    void canceledOrFailedSaveBlocksReplacement()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.choice = TestWindow::Choice::Save;
        const auto previousShape = window.document().shape();
        window.trigger("actionNew");
        QCOMPARE(window.document().shape().IsNull(), previousShape.IsNull());
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.errors.size(), 0);
        window.savePath = directory.filePath("missing/part.thepart");
        window.trigger("actionNew");
        QCOMPARE(window.document().shape().IsNull(), previousShape.IsNull());
        QVERIFY(window.isWindowModified());
        QVERIFY(window.currentPath().isEmpty());
        QCOMPARE(window.errors.size(), 1);
    }

    void saveExtensionValidation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.savePath = directory.filePath("part");
        window.trigger("actionSaveAs");
        QCOMPARE(window.currentPath(), window.savePath + ".thepart");
        QVERIFY(!window.isWindowModified());
        const QString previousPath = window.currentPath();
        window.savePath = directory.filePath("unsupported.theasmb");
        window.trigger("actionSaveAs");
        QCOMPARE(window.errors.size(), 1);
        QCOMPARE(window.currentPath(), previousPath);
        QVERIFY(!QFileInfo::exists(window.savePath));
        window.openPath = window.savePath;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 2);
        QCOMPARE(window.currentPath(), previousPath);
    }

    void emptyPartAndContextualModelingCommands()
    {
        TestWindow window;
        QVERIFY(window.document().features().empty());
        QVERIFY(window.document().shape().IsNull());
        QVERIFY(window.findChild<QAction*>("actionCreateSketch")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("actionExtrude")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("actionExtrudedCut")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("actionLine")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("actionFinishSketch")->isEnabled());
    }

    void canceledFeatureRestoresCleanSketchEdit() {
        QTemporaryDir directory;auto part=document::PartDocument::createEmpty();
        modeling::Sketch sketch;sketch.entities.push_back({modeling::EntityKind::Rectangle,{0,0},{20,15},1});part.addSketch(sketch);
        const auto path=directory.filePath("clean-sketch.thepart");part.save(path);
        TestWindow window;window.openPath=path;window.trigger("actionOpen");QVERIFY(!window.isWindowModified());
        auto* tree=window.findChild<QTreeWidget*>("featureTree");tree->setCurrentItem(tree->topLevelItem(0)->child(4));
        window.trigger("actionEditFeature");QVERIFY(!window.isWindowModified());
        window.trigger("actionExtrude");QVERIFY(window.findChild<QWidget*>("operationPanel"));
        window.trigger("actionCancelFeature");QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        QVERIFY(!window.isWindowModified());QCOMPARE(window.document().features().size(),std::size_t(1));
        window.trigger("actionExtrude");auto* panel=window.findChild<QWidget*>("operationPanel");
        panel->findChild<QListWidget*>("operationContour")->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);
        panel->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(window.document().features().size(),std::size_t(2));QVERIFY(window.isWindowModified());
        QVERIFY(!window.findChild<QAction*>("actionFinishSketch")->isEnabled());QVERIFY(window.errors.isEmpty());
    }
    void operationRequiresExplicitSketchAndContour()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto part = document::PartDocument::createEmpty();
        modeling::Sketch rectangle;
        rectangle.entities.push_back({modeling::EntityKind::Rectangle, {0, 0}, {20, 10}, 1});
        part.addSketch(rectangle);
        modeling::Sketch open;
        open.entities.push_back({modeling::EntityKind::Line, {0, 0}, {10, 0}, 1});
        part.addSketch(open);
        const QString path = directory.filePath("profiles.thepart");
        part.save(path);
        TestWindow window;
        window.openPath = path;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 0);
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QVERIFY(tree);
        tree->setCurrentItem(tree->topLevelItem(0));
        QVERIFY(window.findChild<QAction*>("actionExtrude")->isEnabled());
        window.trigger("actionExtrude");
        auto* dialog = window.findChild<QWidget*>("operationPanel");
        QVERIFY(dialog);
        QCOMPARE(window.findChild<QStackedWidget*>("managerStack")->currentWidget(), dialog);
        QVERIFY(!dialog->isWindow());
        QVERIFY(window.findChild<QDialog*>("operationDialog") == nullptr);
        auto* source = dialog->findChild<QComboBox*>("operationSketch");
        auto* contours = dialog->findChild<QListWidget*>("operationContour");
        auto* buttons = dialog->findChild<QDialogButtonBox*>("operationButtons");
        auto* status = dialog->findChild<QLabel*>("operationStatus");
        QVERIFY(source && contours && buttons && status);
        QCOMPARE(source->currentIndex(), -1);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY(!status->text().isEmpty());
        source->setCurrentIndex(0);
        QCOMPARE(contours->count(), 1);
        QCOMPARE(contours->currentRow(), -1);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        source->setCurrentIndex(1);
        QCOMPARE(contours->count(), 0);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY2(status->text().contains("no valid closed contour"), qPrintable(status->text()));
        QTest::mouseClick(dialog->findChild<QDialogButtonBox*>("operationButtons")->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        QCOMPARE(window.document().features().size(), std::size_t(2));
        QVERIFY(window.document().shape().IsNull());
        QVERIFY(!window.isWindowModified());
    }
    void graphicalSketchFirstDoesNotMutateBeforeSelectionAndCancelRestoresUi()
    {
        TestWindow window;
        window.setWindowModified(false);
        auto* viewport = window.findChild<CadViewport*>();
        QVERIFY(viewport);
        QCOMPARE(window.findChild<QAction*>("actionCreateSketch")->text(), QString("Sketch"));
        window.trigger("actionCreateSketch");
        QVERIFY(window.document().features().empty());
        QVERIFY(!window.isWindowModified());
        QVERIFY(!window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("actionOpen")->isEnabled());
        QVERIFY(window.findChild<QDialog*>("sketchPlaneDialog") == nullptr);
        // The viewport handles Esc; its cancellation signal must not create data.
        viewport->cancelPlaneSelection();
        QVERIFY(window.document().features().empty());
        QVERIFY(!window.isWindowModified());
        QVERIFY(window.findChild<QAction*>("actionCreateSketch")->isEnabled());
        QVERIFY(window.findChild<QAction*>("actionOpen")->isEnabled());
        window.trigger("actionCreateSketch");
        viewport->referencePlaneSelected(modeling::SketchPlane::XZ);
        QCOMPARE(window.document().features().size(), std::size_t(1));
        QCOMPARE(window.document().features().front().sketch.plane, modeling::SketchPlane::XZ);
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        QVERIFY(window.findChild<QAction*>("actionLine")->isEnabled());
        QVERIFY(window.isWindowModified());
        window.trigger("actionFinishSketch");
        QCOMPARE(window.errors.size(), 0);
    }

    void propertyPanelPreservesCoincidentAndOriginRelations()
    {
        TestWindow window;
        auto* viewport = window.findChild<CadViewport*>();
        window.trigger("actionCreateSketch");
        viewport->referencePlaneSelected(modeling::SketchPlane::XY);
        modeling::Sketch sketch;
        sketch.entities = {
            {modeling::EntityKind::Line, {0, 0}, {10, 0}, 1, "edge-a"},
            {modeling::EntityKind::Line, {10, 0}, {10, 10}, 1, "edge-b"}};
        using modeling::PointRole;
        using modeling::RelationKind;
        sketch.relations = {
            {"at-origin", RelationKind::Coincident, {"edge-a", PointRole::First}, {{}, PointRole::Origin}},
            {"join", RelationKind::Coincident, {"edge-a", PointRole::Second}, {"edge-b", PointRole::First}},
            {"horizontal", RelationKind::Horizontal, {"edge-a", PointRole::First}, {"edge-a", PointRole::Second}},
            {"vertical", RelationKind::Vertical, {"edge-b", PointRole::First}, {"edge-b", PointRole::Second}}};
        viewport->sketchChanged(sketch);
        viewport->sketchEntitySelected("edge-a");
        auto* entities = window.findChild<QListWidget*>("sketchEntities");
        QCOMPARE(entities->currentRow(), 0);
        auto* firstX = window.findChild<QDoubleSpinBox*>("propertyFirstX");
        auto* secondX = window.findChild<QDoubleSpinBox*>("propertySecondX");
        auto* apply = window.findChild<QPushButton*>("applyEntityProperties");
        QVERIFY(firstX && secondX && apply && apply->isEnabled());
        secondX->setValue(20);
        apply->click();
        QVERIFY(window.errors.empty());
        firstX->setValue(1);
        apply->click();
        QCOMPARE(window.errors.size(), 1);
        window.trigger("actionFinishSketch");
        const auto& saved = window.document().features().front().sketch;
        QCOMPARE(saved.entities[0].first.x, 0.0);
        QCOMPARE(saved.entities[0].second.x, 20.0);
        QCOMPARE(saved.entities[1].first.x, 20.0);
        QCOMPARE(saved.entities[1].second.x, 20.0);
        QCOMPARE(saved.relations.size(), std::size_t(4));
        modeling::validateSketchRelations(saved);
    }
    void referenceTreePlaneFirstStartsImmediatelyAndPersistsAllPlanes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.setWindowModified(false);
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Untitled.thepart"));
        QCOMPARE(tree->topLevelItem(0)->child(3)->text(0), QString("Origin"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 4);
        QCOMPARE(tree->topLevelItem(0)->child(3)->childCount(), 0);
        const QStringList names{"Front Plane", "Top Plane", "Right Plane"};
        const modeling::SketchPlane planes[] = {modeling::SketchPlane::XY,
            modeling::SketchPlane::XZ, modeling::SketchPlane::YZ};
        for (int index = 0; index < 3; ++index) {
            auto* reference = tree->topLevelItem(0)->child(index);
            QCOMPARE(reference->text(0), names[index]);
            const auto before = window.document().features().size();
            tree->setCurrentItem(reference);
            QCOMPARE(window.document().features().size(), before);
            if (index == 0) QVERIFY(!window.isWindowModified());
            window.trigger("actionCreateSketch");
            QCOMPARE(window.document().features().size(), before + 1);
            QCOMPARE(window.document().features().back().sketch.plane, planes[index]);
            QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
            QVERIFY(window.findChild<QDialog*>("sketchPlaneDialog") == nullptr);
            window.trigger("actionFinishSketch");
        }
        QCOMPARE(tree->topLevelItem(0)->childCount(), 7);
        window.savePath = directory.filePath("three-planes.thepart");
        window.trigger("actionSave");
        QVERIFY(!window.isWindowModified());
        window.trigger("actionNew");
        QVERIFY(window.document().features().empty());
        window.openPath = window.savePath;
        window.trigger("actionOpen");
        QCOMPARE(window.document().features().size(), std::size_t(3));
        for (int index = 0; index < 3; ++index) {
            QCOMPARE(window.document().features().at(index).sketch.plane, planes[index]);
        }
        QCOMPARE(tree->topLevelItem(0)->childCount(), 7);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(window.errors.size(), 0);
    }

    void viewportPreselectionDoesNotCreateUntilSketchCommand()
    {
        TestWindow window;
        window.setWindowModified(false);
        auto* viewport = window.findChild<CadViewport*>();
        QVERIFY(viewport);
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        tree->setCurrentItem(tree->topLevelItem(0));
        viewport->referencePlaneSelected(modeling::SketchPlane::YZ);
        QVERIFY(window.document().features().empty());
        QVERIFY(!window.isWindowModified());
        QCOMPARE(tree->currentItem()->text(0), QString("Right Plane"));
        window.trigger("actionCreateSketch");
        QCOMPARE(window.document().features().size(), std::size_t(1));
        QCOMPARE(window.document().features().front().sketch.plane, modeling::SketchPlane::YZ);
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
    }
    void legacyBoxConversionDoesNotOverwriteOriginal()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString legacy = directory.filePath("legacy.tcpart");
        QFile file(legacy);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original = R"({"format":"TheCAD.Part","version":1,"units":"mm","features":[{"id":"box-old","type":"box","parameters":{"width":11,"depth":22,"height":33},"dependencies":[]}],"resultFeature":"box-old"})";
        QCOMPARE(file.write(original), original.size());
        file.close();
        TestWindow window;
        window.openPath = legacy;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 0);
        QCOMPARE(window.currentPath(), legacy);
        QCOMPARE(window.document().parameters().width, 11.0);
        window.savePath = directory.filePath("converted.thepart");
        window.trigger("actionSave");
        QCOMPARE(window.errors.size(), 0);
        QCOMPARE(window.saveDialogCount, 1);
        QCOMPARE(window.currentPath(), window.savePath);
        QCOMPARE(document::PartDocument::load(window.savePath).parameters().height, 33.0);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        file.close();
        window.savePath = legacy;
        window.trigger("actionSaveAs");
        QCOMPARE(window.errors.size(), 1);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
    void closeWarningCancelDiscardAndSave()
    {
        TestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.choice = TestWindow::Choice::Cancel;
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QCOMPARE(window.promptCount, 1);
        window.choice = TestWindow::Choice::Discard;
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow saved;
        saved.show();
        QVERIFY(QTest::qWaitForWindowExposed(&saved));
        saved.choice = TestWindow::Choice::Save;
        saved.savePath = directory.filePath("closed.thepart");
        saved.trigger("actionExit");
        QVERIFY(!saved.isVisible());
        QVERIFY(QFileInfo::exists(saved.savePath));
        QVERIFY(!saved.isWindowModified());
        QCOMPARE(saved.errors.size(), 0);
    }
};

} // namespace thecad::ui

QTEST_MAIN(thecad::ui::MainWindowTest)
#include "MainWindowTests.moc"
