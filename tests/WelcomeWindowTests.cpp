#include "ui/startup/WelcomeWindow.h"
#include "ui/startup/LoadingScreen.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>
using namespace thecad;
class WelcomeWindowTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Windows' offscreen platform has no native font database. Use the existing
        // system font for background rendering; the application keeps its native font.
        const int font = QFontDatabase::addApplicationFont(qEnvironmentVariable("SystemRoot") + "/Fonts/segoeui.ttf");
        QVERIFY(font >= 0); QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
    }
    void homeAndRecent() {
        QTemporaryDir folder; QVERIFY(folder.isValid()); QSettings settings(folder.filePath("settings.ini"),QSettings::IniFormat);
        const auto part=folder.filePath("Bracket.thepart"), assembly=folder.filePath("Bracket assembly.theasmb");
        for(const auto& path : {part,assembly}) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); }
        application::RecentDocuments recent(settings); recent.recordDocument(part); recent.recordDocument(assembly); recent.addFolder(folder.path());
        ui::ThemeManager::instance(); ui::WelcomeWindow window(settings); window.show(); QApplication::processEvents();
        QSignalSpy newDocument(&window,&ui::WelcomeWindow::newDocumentRequested), openDocument(&window,&ui::WelcomeWindow::openDocumentRequested);
        QVERIFY(!window.findChild<QPushButton*>("welcomeNewDrawing")->isEnabled());
        QVERIFY(window.findChild<QPushButton*>("welcomeNewDrawing")->height() >= 68);
        window.findChild<QPushButton*>("welcomeNewPart")->click(); QCOMPARE(newDocument.count(),1);
        QCOMPARE(qvariant_cast<application::LaunchDocument>(newDocument.takeFirst().at(0)),application::LaunchDocument::Part);
        window.findChild<QPushButton*>("welcomeNewAssembly")->click(); QCOMPARE(newDocument.count(),1);
        QCOMPARE(qvariant_cast<application::LaunchDocument>(newDocument.takeFirst().at(0)),application::LaunchDocument::Assembly);
        auto* home=window.findChild<QTreeWidget*>("welcomeHomeDocuments"); QCOMPARE(home->topLevelItemCount(),2);
        emit home->itemClicked(home->topLevelItem(0),0); QCOMPARE(openDocument.count(),1); QCOMPARE(openDocument.takeFirst().at(0).toString(),assembly);
        QVERIFY(QFile::remove(assembly)); emit home->itemClicked(home->topLevelItem(0),0); QCOMPARE(openDocument.count(),0);
        QVERIFY(window.findChild<QLabel*>("welcomeStatus")->text().contains("unavailable"));
        window.showRecent(); auto* documents=window.findChild<QTreeWidget*>("welcomeRecentDocuments");
        window.findChild<QComboBox*>("welcomeTypeFilter")->setCurrentIndex(1); QCOMPARE(documents->topLevelItemCount(),1);
        window.findChild<QLineEdit*>("welcomeSearch")->setText("no match"); QCOMPARE(documents->topLevelItemCount(),0);
        window.findChild<QLineEdit*>("welcomeSearch")->clear(); QCOMPARE(documents->topLevelItemCount(),1);
        window.findChild<QComboBox*>("welcomeTypeFilter")->setCurrentIndex(0);
        window.findChild<QTabWidget*>("welcomeTabs")->setCurrentIndex(0);
        const auto* folders = window.findChild<QTreeWidget*>("welcomeHomeFolders");
        QVERIFY(home->geometry().bottom() < folders->geometry().top());
        const auto artifacts=qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if(!artifacts.isEmpty()) {
            QDir().mkpath(artifacts); QVERIFY(window.grab().save(artifacts+"/welcome.png"));
        }
    }
    void loadingWaitsForPresentation() {
        QWidget workspace; auto* loading=new ui::LoadingScreen("Preparing your workspace…");
        loading->show(); loading->cover(&workspace); workspace.show(); QApplication::processEvents();
        QCOMPARE(workspace.windowOpacity(),0.0); QVERIFY(loading->isVisible());
        // Showing/layout alone must not remove the cover. The renderer completes it.
        loading->finish(); QCOMPARE(workspace.windowOpacity(),1.0); QVERIFY(!loading->isVisible());
        QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
};
QTEST_MAIN(WelcomeWindowTest)
#include "WelcomeWindowTests.moc"
