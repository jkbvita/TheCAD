#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "document/PartDocument.h"

#include <QAction>
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
        window.savePath = directory.filePath(QString::fromUtf8("díl.tcpart"));
        window.trigger("actionSave");
        QCOMPARE(window.errors.size(), 0);
        QVERIFY(!window.isWindowModified());
        QCOMPARE(window.currentPath(), window.savePath);
        QCOMPARE(window.saveDialogCount, 1);
        QVERIFY(QFileInfo::exists(window.savePath));
        const auto original = document::PartDocument::load(window.savePath);
        QCOMPARE(original.parameters().width, 100.0);

        const QString firstPath = window.currentPath();
        window.savePath = directory.filePath("second.tcpart");
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
        QCOMPARE(second.parameters().depth, 70.0);
        window.trigger("actionFitAll");
        window.trigger("actionIsometric");
        QVERIFY(!window.isWindowModified());
    }

    void openRestoresParametersThenNewReplacesDocument()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString fixture = directory.filePath("custom.tcpart");
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
        QCOMPARE(window.document().parameters().width, 100.0);
    }

    void dirtySamePathOpenSavesCurrentModelBeforeReload()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("same-part.tcpart");
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
        window.savePath = directory.filePath("current.tcpart");
        window.trigger("actionSave");
        QCOMPARE(window.errors.size(), 0);
        const QString originalPath = window.currentPath();
        const auto originalShape = window.document().shape();
        QFile originalFile(originalPath);
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        const auto originalBytes = originalFile.readAll();
        originalFile.close();

        window.setWindowModified(true);
        window.savePath = directory.filePath("missing/subdirectory/failed.tcpart");
        window.trigger("actionSaveAs");
        QCOMPARE(window.errors.size(), 1);
        QCOMPARE(window.currentPath(), originalPath);
        QVERIFY(window.isWindowModified());
        QVERIFY(window.document().shape().IsSame(originalShape));
        QCOMPARE(window.document().parameters().width, 100.0);
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
        const QString good = directory.filePath("good.tcpart");
        document::PartDocument::createBox({17.0, 19.0, 23.0}).save(good);
        TestWindow window;
        window.openPath = good;
        window.trigger("actionOpen");
        window.setWindowModified(true);
        const int prompts = window.promptCount;
        const auto previousShape = window.document().shape();
        const QString invalid = directory.filePath("invalid.tcpart");
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
        const QString fixture = directory.filePath("other.tcpart");
        document::PartDocument::createBox({3.0, 4.0, 5.0}).save(fixture);
        TestWindow window;
        window.choice = TestWindow::Choice::Cancel;
        window.trigger("actionNew");
        window.openPath = fixture;
        window.trigger("actionOpen");
        QCOMPARE(window.promptCount, 2);
        QVERIFY(window.currentPath().isEmpty());
        QCOMPARE(window.document().parameters().width, 100.0);
        QVERIFY(window.isWindowModified());
    }

    void unsavedSaveCreatesFileBeforeReplacement()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestWindow window;
        window.choice = TestWindow::Choice::Save;
        window.savePath = directory.filePath("saved.tcpart");
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
        QVERIFY(window.document().shape().IsSame(previousShape));
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.errors.size(), 0);
        window.savePath = directory.filePath("missing/part.tcpart");
        window.trigger("actionNew");
        QVERIFY(window.document().shape().IsSame(previousShape));
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
        QCOMPARE(window.currentPath(), window.savePath + ".tcpart");
        QVERIFY(!window.isWindowModified());
        const QString previousPath = window.currentPath();
        window.savePath = directory.filePath("unsupported.tcasmb");
        window.trigger("actionSaveAs");
        QCOMPARE(window.errors.size(), 1);
        QCOMPARE(window.currentPath(), previousPath);
        QVERIFY(!QFileInfo::exists(window.savePath));
        window.openPath = window.savePath;
        window.trigger("actionOpen");
        QCOMPARE(window.errors.size(), 2);
        QCOMPARE(window.currentPath(), previousPath);
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
        saved.savePath = directory.filePath("closed.tcpart");
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
