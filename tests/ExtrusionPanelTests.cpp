#include "ui/PropertyManagerPage.h"
#include "ui/Theme.h"
#include "ui/features/ExtrusionOptionsPanel.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QVBoxLayout>

using namespace thecad;
using namespace thecad::ui;
class ExtrusionPanelTests : public QObject {
    Q_OBJECT
private slots:
    void compactPanel_data()
    {
        QTest::addColumn<bool>("cut"); QTest::addColumn<bool>("dark");
        QTest::newRow("boss-dark") << false << true;
        QTest::newRow("cut-light") << true << false;
    }
    void compactPanel()
    {
        QFETCH(bool, cut); QFETCH(bool, dark);
        ThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light, false);
        const auto document = document::PartDocument::createBox({20,20,20});
        document::Feature feature; feature.kind = cut ? document::FeatureKind::Cut : document::FeatureKind::Extrude;
        PropertyManagerPage page(cut ? "Cut-Extrude" : "Boss-Extrude", "Description");
        auto* preview = page.buttons->addButton("Preview", QDialogButtonBox::ActionRole);
        preview->setCheckable(true); preview->setChecked(true); page.useCompactFeatureHeader(preview);
        std::vector<int> candidates;
        ExtrusionOptionsPanel options(page.settings, &page, feature, document.referenceOptions(false,true),
                                     [] {}, [&](const auto& indexes) { candidates=indexes; });
        page.settings->addStretch(); page.resize(310,670); page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto* confirm=page.buttons->button(QDialogButtonBox::Ok);
        auto* cancel=page.buttons->button(QDialogButtonBox::Cancel);
        QVERIFY(confirm->text().isEmpty()); QVERIFY(cancel->text().isEmpty()); QVERIFY(preview->text().isEmpty());
        QVERIFY(confirm->x() < cancel->x()); QVERIFY(cancel->x() < preview->x());
        QVERIFY(confirm->width() <= 30);
        auto* depth=page.findChild<QDoubleSpinBox*>("featureLength");
        QCOMPARE(depth->cleanText(), QString("10.00"));
        depth->setValue(12.345678); QCOMPARE(options.definition().first.depth, 12.345678);
        QVERIFY(depth->cleanText().endsWith("345678"));
        depth->setValue(10);
        QVERIFY(!page.findChild<QComboBox*>("featureDirection"));
        auto* reverse=page.findChild<QPushButton*>("featureReverseDirection");
        QTest::mouseClick(reverse, Qt::LeftButton);
        QCOMPARE(options.direction(), modeling::Direction::Negative); QVERIFY(reverse->isChecked());
        auto* from=page.findChild<QComboBox*>("operationStartingReference");
        from->setCurrentText("Surface/Face/Plane"); QVERIFY(!candidates.empty());
        auto* start=page.findChild<QComboBox*>("extrudeStartReference");
        QVERIFY(start->property("picking").toBool());
        const int picked=candidates.front(); options.picked(picked);
        QCOMPARE(start->currentData().toInt(), picked); QVERIFY(candidates.empty());
        QVERIFY(!start->property("picking").toBool());
        QTest::mouseClick(start,Qt::LeftButton); QVERIFY(!candidates.empty());
        depth->setFocus(); QCoreApplication::processEvents(); QVERIFY(candidates.empty());
        QTest::keyClick(start,Qt::Key_Delete); QCOMPARE(start->currentIndex(),0);
        from->setCurrentText("Sketch Plane"); QVERIFY(!start->isVisible());
        auto* end=page.findChild<QComboBox*>("operationEndCondition");
        QVERIFY((end->findText("Through All") >= 0) == cut);
        auto* second=page.findChild<QCheckBox*>("extrudeDirection2");
        QVERIFY(!page.findChild<QDoubleSpinBox*>("extrudeDepth2")->isVisible());
        second->setChecked(true); QCoreApplication::processEvents();
        QVERIFY(page.findChild<QDoubleSpinBox*>("extrudeDepth2")->isVisible());
        QVERIFY(options.definition().second.has_value());
        second->setChecked(false);
        auto* thin=page.findChild<QCheckBox*>("extrudeThinFeature");
        thin->setChecked(true); QCoreApplication::processEvents(); QVERIFY(options.definition().thin.has_value());
        thin->setChecked(false);
        QCoreApplication::processEvents();
        QVERIFY(!page.findChild<QDoubleSpinBox*>("extrudeThinThickness")->isVisible());
        const auto artifacts=qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            QVERIFY(page.grab().save(QDir(artifacts).filePath(cut?"cut-propertymanager.png":"boss-propertymanager.png")));
        }
    }
    void selectedContoursMembership()
    {
        PropertySelectionList field; field.placeholder="Select contours";
        field.addItem("Region 1"); field.addItem("Region 2"); field.refreshMembers();
        field.resize(260,100); field.show(); QVERIFY(QTest::qWaitForWindowExposed(&field));
        QVERIFY(field.item(0)->isHidden()); QVERIFY(field.item(1)->isHidden());
        field.item(1)->setSelected(true); QVERIFY(!field.item(1)->isHidden());
        int picks=0; field.pick=[&]{++picks;};
        QTest::mouseClick(field.viewport(),Qt::LeftButton,Qt::NoModifier,field.visualItemRect(field.item(1)).center());
        QCOMPARE(picks,1); QVERIFY(field.item(1)->isSelected());
        QTest::keyClick(&field,Qt::Key_Delete); QVERIFY(field.selectedItems().isEmpty());
        QVERIFY(field.item(1)->isHidden()); QCOMPARE(field.count(),2);
    }
};
QTEST_MAIN(ExtrusionPanelTests)
#include "ExtrusionPanelTests.moc"
