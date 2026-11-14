#include "ui/widgets/FeatureCommandBar.h"
#include "ui/widgets/CommandBar.h"
#include "ui/Theme.h"
#include <QAction>
#include <QDir>
#include <QHBoxLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QToolButton>

using namespace thecad::ui;
class FeatureCommandBarTests:public QObject {
    Q_OBJECT
private slots:
    void commandLayout_data(){QTest::addColumn<bool>("dark");QTest::newRow("dark")<<true;QTest::newRow("light")<<false;}
    void commandLayout() {
        QFETCH(bool,dark); ThemeManager::instance().setMode(dark?ThemeMode::Dark:ThemeMode::Light,false);
        QWidget page; auto* layout=new QHBoxLayout(&page); layout->setContentsMargins(0,0,0,0);
        const auto action=[&](const char* name,const QString& text){auto* a=new QAction(commandIcon(text),text,&page);a->setObjectName(name);return a;};
        FeatureCommandActions a{action("actionExtrude","Extruded Boss/Base"),action("actionRevolve","Revolved Boss/Base"),action("actionSweep","Swept Boss/Base"),
            action("actionExtrudedCut","Extruded Cut"),action("actionRevolvedCut","Revolved Cut"),action("actionSweptCut","Swept Cut"),
            action("actionFillet","Fillet"),action("actionChamfer","Chamfer"),action("actionLinearPattern","Linear Pattern"),action("actionCircularPattern","Circular Pattern"),
            action("actionMirror","Mirror"),action("actionReferencePlane","Reference Plane"),action("actionReferenceAxis","Reference Axis"),action("actionReferencePoint","Reference Point")};
        populateFeatureCommands(layout,a); page.resize(1000,98); page.show(); QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto* extrude=page.findChild<QToolButton*>("actionExtrudeButton"); QVERIFY(extrude);
        QCOMPARE(extrude->defaultAction(),a.extrude); QVERIFY(extrude->text().contains('\n')); QCOMPARE(extrude->height(),76);
        QSignalSpy extruded(a.extrude,&QAction::triggered); QTest::mouseClick(extrude,Qt::LeftButton); QCOMPARE(extruded.count(),1);
        auto* fillet=page.findChild<QToolButton*>("actionFilletButton"); QVERIFY(fillet->menu()->actions().contains(a.chamfer));
        QSignalSpy chamfered(a.chamfer,&QAction::triggered);
        QTimer::singleShot(0,&page,[&]{QTest::mouseClick(fillet->menu(),Qt::LeftButton,Qt::NoModifier,fillet->menu()->actionGeometry(a.chamfer).center());});
        QTest::mouseClick(fillet,Qt::LeftButton,Qt::NoModifier,QPoint(fillet->width()-3,fillet->height()/2));
        QCOMPARE(chamfered.count(),1); QCOMPARE(fillet->defaultAction(),a.chamfer);
        auto* pattern=page.findChild<QToolButton*>("actionLinearPatternButton"); QVERIFY(pattern->menu()->actions().contains(a.circularPattern));
        QSignalSpy circular(a.circularPattern,&QAction::triggered);
        QTimer::singleShot(0,&page,[&]{QTest::mouseClick(pattern->menu(),Qt::LeftButton,Qt::NoModifier,pattern->menu()->actionGeometry(a.circularPattern).center());});
        QTest::mouseClick(pattern,Qt::LeftButton,Qt::NoModifier,QPoint(pattern->width()-3,pattern->height()/2));
        QCOMPARE(circular.count(),1); QCOMPARE(pattern->defaultAction(),a.circularPattern);
        auto* reference=page.findChild<QToolButton*>("featureReferenceGeometry"); QVERIFY(reference);
        QCOMPARE(reference->popupMode(),QToolButton::InstantPopup);
        QSignalSpy axis(a.axis,&QAction::triggered);
        QTimer::singleShot(0,&page,[&]{QTest::mouseClick(reference->menu(),Qt::LeftButton,Qt::NoModifier,reference->menu()->actionGeometry(a.axis).center());});
        QTest::mouseClick(reference,Qt::LeftButton); QCOMPARE(axis.count(),1);
        for(auto* ref:{a.plane,a.axis,a.point})ref->setEnabled(false);
        QVERIFY(!reference->isEnabled()); a.axis->setEnabled(true); QVERIFY(reference->isEnabled());
        const auto artifacts=qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if(!artifacts.isEmpty()){QDir().mkpath(artifacts);QVERIFY(page.grab().save(QDir(artifacts).filePath(dark?"features-dark.png":"features-light.png")));}
        page.resize(600,98); QCoreApplication::processEvents();
        auto* scroll=page.findChild<QScrollArea*>("featureCommandsScroll"); QVERIFY(scroll->horizontalScrollBar()->maximum()>0);
    }
};
QTEST_MAIN(FeatureCommandBarTests)
#include "FeatureCommandBarTests.moc"
