#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "document/PartDocument.h"

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
    void fileMenuAndShortcuts()
    {
        TestWindow window;
        QVERIFY(window.isWindowModified());
        QVERIFY(window.currentPath().isEmpty());
        QCOMPARE(window.menuBar()->actions().size(), 1);
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
        auto* dialog = window.findChild<QDialog*>("operationDialog");
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        auto* source = dialog->findChild<QComboBox*>("operationSketch");
        auto* contours = dialog->findChild<QComboBox*>("operationContour");
        auto* buttons = dialog->findChild<QDialogButtonBox*>("operationButtons");
        auto* status = dialog->findChild<QLabel*>("operationStatus");
        QVERIFY(source && contours && buttons && status);
        QCOMPARE(source->currentIndex(), -1);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY(!status->text().isEmpty());
        source->setCurrentIndex(0);
        QCOMPARE(contours->count(), 1);
        QCOMPARE(contours->currentIndex(), -1);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        source->setCurrentIndex(1);
        QCOMPARE(contours->count(), 0);
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY2(status->text().contains("no valid closed contour"), qPrintable(status->text()));
        dialog->reject();
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

    void referenceTreePlaneFirstStartsImmediatelyAndPersistsAllPlanes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.setWindowModified(false);
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Part"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QString("Origin"));
        const QStringList names{"Front", "Top", "Right"};
        const modeling::SketchPlane planes[] = {modeling::SketchPlane::XY,
            modeling::SketchPlane::XZ, modeling::SketchPlane::YZ};
        for (int index = 0; index < 3; ++index) {
            auto* reference = tree->topLevelItem(1)->child(index);
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
        QCOMPARE(tree->topLevelItem(0)->childCount(), 3);
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
        QCOMPARE(tree->topLevelItem(0)->childCount(), 3);
        QCOMPARE(tree->topLevelItem(1)->childCount(), 3);
        QCOMPARE(window.errors.size(), 0);
    }

    void viewportPreselectionDoesNotCreateUntilSketchCommand()
    {
        TestWindow window;
        window.setWindowModified(false);
        auto* viewport = window.findChild<CadViewport*>();
        QVERIFY(viewport);
        viewport->referencePlaneSelected(modeling::SketchPlane::YZ);
        QVERIFY(window.document().features().empty());
        QVERIFY(!window.isWindowModified());
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QCOMPARE(tree->currentItem()->text(0), QString("Right"));
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
