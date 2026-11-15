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
class FeatureCommandBarTests : public QObject
{
    Q_OBJECT
private slots:
    void commandLayout_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("dark") << true;
        QTest::newRow("light") << false;
    }
    void commandLayout()
    {
        QFETCH(bool, dark);
        ThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light, false);
        QWidget page;
        auto* layout = new QHBoxLayout(&page);
        layout->setContentsMargins(0, 0, 0, 0);
        const auto action = [&](const char* name, const QString& text)
        {
            auto* a = new QAction(commandIcon(text), text, &page);
            a->setObjectName(name);
            return a;
        };
        FeatureCommandActions a{action("actionExtrude", "Extruded Boss/Base"),
                                action("actionRevolve", "Revolved Boss/Base"),
                                action("actionSweep", "Swept Boss/Base"),
                                action("actionExtrudedCut", "Extruded Cut"),
                                action("actionRevolvedCut", "Revolved Cut"),
                                action("actionSweptCut", "Swept Cut"),
                                action("actionFillet", "Fillet"),
                                action("actionChamfer", "Chamfer"),
                                action("actionLinearPattern", "Linear Pattern"),
                                action("actionCircularPattern", "Circular Pattern"),
                                action("actionMirror", "Mirror"),
                                action("actionReferencePlane", "Reference Plane"),
                                action("actionReferenceAxis", "Reference Axis"),
                                action("actionReferencePoint", "Reference Point")};
        populateFeatureCommands(layout, a);
        page.resize(960, 84);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto* extrude = page.findChild<QToolButton*>("actionExtrudeButton");
        QVERIFY(extrude);
        QCOMPARE(extrude->defaultAction(), a.extrude);
        QVERIFY(extrude->text().contains('\n'));
        QCOMPARE(extrude->size(), QSize(62, 78));
        QCOMPARE(extrude->iconSize(), QSize(20, 20));
        const auto button = [&](const char* name) { return page.findChild<QToolButton*>(name); };
        const auto position = [&](const char* name)
        { return button(name)->mapTo(&page, QPoint()); };
        QVERIFY(position("actionRevolveButton").x() < position("actionSweepButton").x());
        QCOMPARE(position("actionSweepButton").x(), position("actionLoftedBossBaseButton").x());
        QCOMPARE(position("actionSweepButton").y() + 26,
                 position("actionLoftedBossBaseButton").y());
        QCOMPARE(position("actionSweepButton").y() + 52,
                 position("actionBoundaryBossBaseButton").y());
        QCOMPARE(position("actionRevolvedCutButton").y(), position("actionExtrudedCutButton").y());
        QVERIFY(position("actionRevolvedCutButton").x() < position("actionSweptCutButton").x());
        QCOMPARE(position("actionMirrorButton").y(), position("actionShellButton").y());
        QCOMPARE(page.findChildren<QWidget*>("featureCommandSeparator").size(), 3);
        int future = 0;
        for (auto* candidate : page.findChildren<QAction*>())
            if (candidate->property("futureFeature").toBool())
            {
                ++future;
                QVERIFY(!candidate->isEnabled());
                QVERIFY(candidate->toolTip().contains("not implemented"));
                auto* tool = page.findChild<QToolButton*>(candidate->objectName() + "Button");
                QVERIFY(tool);
                QVERIFY(!tool->isEnabled());
                QSignalSpy triggered(candidate, &QAction::triggered);
                QTest::mouseClick(tool, Qt::LeftButton);
                QCOMPARE(triggered.count(), 0);
            }
        QCOMPARE(future, 11);
        const auto artifacts = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        QTest::mouseMove(&page, QPoint(page.width() - 2, page.height() - 2));
        if (!artifacts.isEmpty())
        {
            QDir().mkpath(artifacts);
            QVERIFY(page.grab().save(
                QDir(artifacts).filePath(dark ? "features-dark.png" : "features-light.png")));
        }
        QSignalSpy extruded(a.extrude, &QAction::triggered);
        QTest::mouseClick(extrude, Qt::LeftButton);
        QCOMPARE(extruded.count(), 1);
        auto* fillet = page.findChild<QToolButton*>("actionFilletButton");
        QVERIFY(fillet->menu()->actions().contains(a.chamfer));
        auto* filletArrow = page.findChild<QToolButton*>("actionFilletButtonFlyout");
        QVERIFY(filletArrow);
        QCOMPARE(filletArrow->menu(), fillet->menu());
        QCOMPARE(filletArrow->height(), 14);
        QCOMPARE(filletArrow->mapTo(&page, QPoint()).y(),
                 fillet->mapTo(&page, QPoint(0, fillet->height())).y());
        QSignalSpy filleted(a.fillet, &QAction::triggered);
        QTest::mouseClick(fillet, Qt::LeftButton);
        QCOMPARE(filleted.count(), 1);
        QSignalSpy chamfered(a.chamfer, &QAction::triggered);
        QTimer::singleShot(0, &page,
                           [&]
                           {
                               QTest::mouseClick(
                                   fillet->menu(), Qt::LeftButton, Qt::NoModifier,
                                   fillet->menu()->actionGeometry(a.chamfer).center());
                           });
        QTest::mouseClick(filletArrow, Qt::LeftButton);
        QCOMPARE(chamfered.count(), 1);
        QCOMPARE(fillet->defaultAction(), a.chamfer);
        QTest::mouseClick(fillet, Qt::LeftButton);
        QCOMPARE(chamfered.count(), 2);
        auto* pattern = page.findChild<QToolButton*>("actionLinearPatternButton");
        QVERIFY(pattern->menu()->actions().contains(a.circularPattern));
        QSignalSpy circular(a.circularPattern, &QAction::triggered);
        QTimer::singleShot(0, &page,
                           [&]
                           {
                               QTest::mouseClick(
                                   pattern->menu(), Qt::LeftButton, Qt::NoModifier,
                                   pattern->menu()->actionGeometry(a.circularPattern).center());
                           });
        QTest::mouseClick(page.findChild<QToolButton*>("actionLinearPatternButtonFlyout"),
                          Qt::LeftButton);
        QCOMPARE(circular.count(), 1);
        QCOMPARE(pattern->defaultAction(), a.circularPattern);
        auto* reference = page.findChild<QToolButton*>("featureReferenceGeometry");
        QVERIFY(reference);
        QCOMPARE(reference->popupMode(), QToolButton::InstantPopup);
        QSignalSpy axis(a.axis, &QAction::triggered);
        QTimer::singleShot(0, &page,
                           [&]
                           {
                               QTest::mouseClick(
                                   reference->menu(), Qt::LeftButton, Qt::NoModifier,
                                   reference->menu()->actionGeometry(a.axis).center());
                           });
        QTest::mouseClick(reference, Qt::LeftButton);
        QCOMPARE(axis.count(), 1);
        auto* referenceArrow = page.findChild<QToolButton*>("featureReferenceGeometryFlyout");
        QVERIFY(referenceArrow);
        QTimer::singleShot(0, &page, [&] { reference->menu()->close(); });
        QTest::mouseClick(referenceArrow, Qt::LeftButton);
        for (auto* ref : {a.plane, a.axis, a.point})
            ref->setEnabled(false);
        QVERIFY(!reference->isEnabled());
        QVERIFY(!referenceArrow->isEnabled());
        a.axis->setEnabled(true);
        QVERIFY(reference->isEnabled());
        QVERIFY(referenceArrow->isEnabled());
        a.chamfer->setEnabled(false);
        QVERIFY(!fillet->isEnabled());
        QVERIFY(filletArrow->isEnabled());
        QTimer::singleShot(0, &page,
                           [&]
                           {
                               QTest::mouseClick(fillet->menu(), Qt::LeftButton, Qt::NoModifier,
                                                 fillet->menu()->actionGeometry(a.fillet).center());
                           });
        QTest::mouseClick(filletArrow, Qt::LeftButton);
        QCOMPARE(fillet->defaultAction(), a.fillet);
        QVERIFY(fillet->isEnabled());
        // Menu access by keyboard still uses Qt's normal toolbutton behavior.
        fillet->setFocus();
        bool opened = false;
        QTimer::singleShot(0, &page,
                           [&]
                           {
                               opened = fillet->menu()->isVisible();
                               fillet->menu()->close();
                           });
        QTest::keyClick(fillet, Qt::Key_Down);
        QVERIFY(opened);
        page.resize(600, 98);
        QCoreApplication::processEvents();
        auto* scroll = page.findChild<QScrollArea*>("featureCommandsScroll");
        QVERIFY(scroll->horizontalScrollBar()->maximum() > 0);
    }
    void assemblyButtonsKeepTheirSize()
    {
        QWidget parent;
        QAction action("Insert Components", &parent);
        auto* button = commandManagerButton(&action, &parent);
        QCOMPARE(button->size(), QSize(78, 76));
        QCOMPARE(button->iconSize(), QSize(28, 28));
    }
};
QTEST_MAIN(FeatureCommandBarTests)
#include "FeatureCommandBarTests.moc"
