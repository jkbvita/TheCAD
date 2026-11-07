#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/features/SolidFeaturePanel.h"
#include "ui/PropertyManagerPage.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Graphic3d_Camera.hxx>
#include <TopoDS.hxx>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
namespace thecad::ui
{
class SolidTestWindow : public MainWindow
{
public:
    QStringList errors;

protected:
    void reportError(const QString& value) override { errors.append(value); }
    UnsavedChoice confirmUnsaved() override { return UnsavedChoice::Discard; }
};
class SolidFeaturesWorkflowTest final : public QObject
{
    Q_OBJECT
    void command(SolidTestWindow& window, const char* name)
    {
        auto* action = window.findChild<QAction*>(name);
        QVERIFY2(action, name);
        QVERIFY2(action->isEnabled(), name);
        action->trigger();
        QApplication::processEvents();
    }
    void select(SolidTestWindow& window, const std::string& id)
    {
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        QVERIFY(tree);
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString().toStdString() == id)
            {
                tree->setCurrentItem(*it, 0, QItemSelectionModel::ClearAndSelect);
                QApplication::processEvents();
                return;
            }
        QFAIL("Feature not found");
    }
    QPoint project(CadViewport* viewport, const gp_Pnt& p)
    {
        int x, y;
        viewport->m_graphics.view()->Convert(p.X(), p.Y(), p.Z(), x, y);
        return {qRound(x / viewport->devicePixelRatioF()),
                qRound(y / viewport->devicePixelRatioF())};
    }
    double volume(const TopoDS_Shape& shape)
    {
        GProp_GProps p;
        BRepGProp::VolumeProperties(shape, p);
        return p.Mass();
    }
    void sameCamera(const Handle(Graphic3d_Camera) & a, const Handle(Graphic3d_Camera) & b)
    {
        QVERIFY(a->Eye().IsEqual(b->Eye(), 1e-10));
        QVERIFY(a->Center().IsEqual(b->Center(), 1e-10));
        QVERIFY(a->Up().IsEqual(b->Up(), 1e-10));
        QCOMPARE(a->Scale(), b->Scale());
        QCOMPARE(a->ProjectionType(), b->ProjectionType());
    }
private slots:
    void revolveCreateEditUndoCancel()
    {
        QTemporaryDir temp;
        auto doc = document::PartDocument::createEmpty();
        modeling::Sketch sketch;
        sketch.entities.push_back(
            {modeling::EntityKind::Rectangle, {10, 0}, {20, 30}, 1, "profile"});
        sketch.entities.push_back({modeling::EntityKind::Line, {0, 0}, {0, 40}, 1, "axis", true});
        const auto sid = doc.addSketch(sketch);
        const auto path = temp.filePath("revolve.thepart");
        doc.save(path);
        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path));
        select(window, sid);
        Handle(Graphic3d_Camera) camera =
            new Graphic3d_Camera(viewport->m_graphics.view()->Camera());
        command(window, "actionRevolve");
        auto* panel = window.findChild<SolidFeaturePanel*>();
        QVERIFY(panel);
        QVERIFY(!window.findChild<QAction*>("actionSaveAs")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("actionFillet")->isEnabled());
        auto* axis = panel->findChild<QComboBox*>("revolveAxis");
        QVERIFY(axis);
        const int row = axis->findText("Sketch1 / axis");
        QVERIFY(row >= 0);
        // Exercise the real viewport reference-picker dispatch.
        panel->findChild<QPushButton*>("revolvePickAxis")->click();
        const auto axisPixel = project(viewport, {0, 35, 0});
        const int pickedAxis = viewport->referenceCandidateAt(axisPixel);
        QVERIFY(pickedAxis >= 0);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, axisPixel);
        QCOMPARE(axis->currentIndex(), pickedAxis);
        // The sketch line overlaps global Y here. Choose it explicitly to test its association.
        axis->setCurrentIndex(row);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, project(viewport, {15, 15, 0}));
        QVERIFY(panel->candidate());
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        auto* angle = panel->findChild<QDoubleSpinBox*>("revolveAngle");
        QVERIFY(angle);
        angle->setValue(180);
        QVERIFY(panel->candidate());
        QCOMPARE(window.document().features().size(), std::size_t(1));
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        QCOMPARE(window.document().features().size(), std::size_t(2));
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::Revolve);
        const auto id = window.document().features().back().id;
        const double half = volume(window.document().shape());
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        command(window, "actionUndo");
        QCOMPARE(window.document().features().size(), std::size_t(1));
        QVERIFY(window.document().shape().IsNull());
        command(window, "actionRedo");
        QCOMPARE(window.document().features().size(), std::size_t(2));
        select(window, id);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel);
        panel->findChild<QDoubleSpinBox*>("revolveAngle")->setValue(360);
        QVERIFY(panel->candidate());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        QVERIFY(std::abs(volume(window.document().shape()) - 2 * half) < 1e-5);
        command(window, "actionUndo");
        QVERIFY(std::abs(volume(viewport->displayedShape()) - half) < 1e-5);
        select(window, id);
        const auto before = window.document().shape();
        command(window, "actionEditFeature");
        command(window, "actionCancelFeature");
        QVERIFY(window.document().shape().IsSame(before));
        QVERIFY(window.findChild<QAction*>("actionSaveAs")->isEnabled());
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
    }
    void revolvedCutFromSketch()
    {
        QTemporaryDir temp;
        auto doc = document::PartDocument::createEmpty();
        modeling::Sketch disk;
        disk.entities.push_back({modeling::EntityKind::Circle, {0, 0}, {}, 25, "disk"});
        const auto diskId = doc.addSketch(disk);
        doc.addExtrude(diskId, {"disk"}, 30, modeling::Direction::Positive);
        modeling::Sketch sketch;
        sketch.plane = modeling::SketchPlane::XZ;
        sketch.entities.push_back(
            {modeling::EntityKind::Rectangle, {10, 0}, {20, 30}, 1, "profile"});
        sketch.entities.push_back({modeling::EntityKind::Line, {0, 0}, {0, 40}, 1, "axis", true});
        const auto sid = doc.addSketch(sketch);
        const double original = volume(doc.shape());
        const auto path = temp.filePath("revolved-cut.thepart");
        doc.save(path);
        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path));
        select(window, sid);
        command(window, "actionEditFeature");
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        command(window, "actionRevolvedCut");
        command(window, "actionCancelFeature");
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        QCOMPARE(window.document().features().size(), std::size_t(3));
        command(window, "actionRevolvedCut");
        auto* panel = window.findChild<SolidFeaturePanel*>();
        QVERIFY(panel);
        auto* axis = panel->findChild<QComboBox*>("revolveAxis");
        const int index = axis->findText("Sketch2 / axis");
        QVERIFY(index >= 0);
        axis->setCurrentIndex(index);
        auto* regions = panel->findChild<QListWidget*>("revolveContours");
        QCOMPARE(regions->count(), 1);
        regions->item(0)->setSelected(true);
        panel->parameter()->setValue(180);
        panel->findChild<QComboBox*>("revolveDirection")->setCurrentIndex(1);
        QVERIFY(panel->candidate());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        QCOMPARE(window.document().features().size(), std::size_t(4));
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::RevolvedCut);
        QVERIFY(std::abs(volume(window.document().shape()) -
                         (original - std::acos(-1.0) * 300 * 15)) < 1e-5);
        QVERIFY(!window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        command(window, "actionUndo");
        QCOMPARE(window.document().features().size(), std::size_t(3));
        QVERIFY(window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        command(window, "actionRedo");
        QCOMPARE(window.document().features().size(), std::size_t(4));
        QVERIFY(!window.findChild<QAction*>("actionFinishSketch")->isEnabled());
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
    }
    void patternAndMirror()
    {
        QTemporaryDir temp;
        auto doc = document::PartDocument::createBox({80, 40, 10});
        modeling::Sketch hole;
        hole.entities.push_back({modeling::EntityKind::Circle, {10, 12}, {}, 3, "hole"});
        const auto sketchId = doc.addSketch(hole);
        const auto cutId = doc.addCut(sketchId, {"hole"}, 10, true, modeling::Direction::Positive);
        const auto path = temp.filePath("pattern-mirror.thepart");
        doc.save(path);
        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path));
        const Handle(Graphic3d_Camera) camera =
            new Graphic3d_Camera(viewport->m_graphics.view()->Camera());
        select(window, cutId);
        command(window, "actionLinearPattern");
        auto* panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel);
        QCOMPARE(panel->findChild<QListWidget*>("patternSeeds")->selectedItems().size(), 1);
        QVERIFY(!panel->candidate());
        auto* direction = panel->findChild<QComboBox*>("patternReference");
        const int xAxis = direction->findText("X axis");
        QVERIFY(xAxis >= 0);
        direction->setCurrentIndex(xAxis);
        panel->parameter()->setValue(20);
        panel->findChild<QSpinBox*>("patternInstances")->setValue(3);
        QVERIFY(panel->candidate());
        const double holeVolume = std::acos(-1.0) * 90;
        const double patternedVolume = 32000 - 3 * holeVolume;
        QVERIFY(std::abs(volume(panel->candidate()->document.shape()) - patternedVolume) < 1e-5);
        QCOMPARE(window.document().features().size(), std::size_t(3));
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        const auto patternId = window.document().features().back().id;
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::LinearPattern);
        command(window, "actionUndo");
        QCOMPARE(window.document().features().back().id, cutId);
        command(window, "actionRedo");
        QCOMPARE(window.document().features().back().id, patternId);
        select(window, patternId);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel && panel->candidate());
        QCOMPARE(panel->parameter()->value(), 20.);
        command(window, "actionCancelFeature");
        QVERIFY(std::abs(volume(window.document().shape()) - patternedVolume) < 1e-5);

        const bool planesWereVisible = viewport->referencePlanesVisible();
        command(window, "actionMirror");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel);
        QVERIFY(viewport->referencePlanesVisible());
        panel->findChild<QComboBox*>("patternScope")->setCurrentIndex(1);
        select(window, "origin:YZ");
        QVERIFY(panel->candidate());
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        QTest::keyClick(viewport, Qt::Key_3);
        QVERIFY(panel->candidate());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(viewport->referencePlanesVisible(), planesWereVisible);
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::Mirror);
        QVERIFY(std::abs(volume(window.document().shape()) - 2 * patternedVolume) < 1e-5);
        window.document().save(path);
        QVERIFY(window.openPartFile(path));
        QVERIFY(std::abs(volume(window.document().shape()) - 2 * patternedVolume) < 1e-5);
        select(window, window.document().features().back().id);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel && panel->candidate());
        QCOMPARE(panel->findChild<QComboBox*>("patternReference")->currentText(),
                 QString("Right plane"));
        command(window, "actionCancelFeature");
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
    }
    void edgeTreatments_data()
    {
        QTest::addColumn<bool>("chamfer");
        QTest::newRow("fillet") << false;
        QTest::newRow("chamfer") << true;
    }
    void edgeTreatments()
    {
        QFETCH(bool, chamfer);
        QTemporaryDir temp;
        auto doc = document::PartDocument::createBox({40, 30, 20});
        const auto path = temp.filePath("edges.thepart");
        doc.save(path);
        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path));
        Handle(Graphic3d_Camera) camera =
            new Graphic3d_Camera(viewport->m_graphics.view()->Camera());
        command(window, chamfer ? "actionChamfer" : "actionFillet");
        auto* panel = window.findChild<SolidFeaturePanel*>();
        QVERIFY(panel);
        auto* edges = panel->findChild<QListWidget*>("treatmentEdges");
        QVERIFY(edges);
        QVERIFY(edges->count() > 0);
        // Select a visible edge through its real screen-space candidate.
        bool picked = false;
        for (int i = 0; i < int(panel->options().size()); ++i)
        {
            const auto& option = panel->options()[std::size_t(i)];
            BRepAdaptor_Curve curve(TopoDS::Edge(option.selectionShape));
            const auto pixel = project(
                viewport, curve.Value((curve.FirstParameter() + curve.LastParameter()) * 0.5));
            if (viewport->referenceCandidateAt(pixel) == i)
            {
                QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pixel);
                picked = true;
                break;
            }
        }
        QVERIFY(picked);
        QVERIFY(panel->candidate());
        QCOMPARE(edges->selectedItems().size(), 1);
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        panel->parameter()->setValue(1e6);
        QVERIFY(!panel->candidate());
        QVERIFY(!panel->page()->buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QCOMPARE(window.document().features().size(), std::size_t(1));
        panel->parameter()->setValue(1);
        QVERIFY(panel->candidate());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        QCOMPARE(window.document().features().size(), std::size_t(2));
        QVERIFY(volume(window.document().shape()) < 24000);
        const double original = volume(window.document().shape());
        const auto id = window.document().features().back().id;
        select(window, id);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel);
        QVERIFY(panel->candidate());
        panel->parameter()->setValue(2);
        QVERIFY(panel->candidate());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        command(window, "actionUndo");
        QVERIFY(std::abs(volume(window.document().shape()) - original) < 1e-6);
        QVERIFY(std::abs(volume(viewport->displayedShape()) - original) < 1e-6);
        command(window, "actionRedo");
        QVERIFY(volume(window.document().shape()) < original);
        window.document().save(path);
        QVERIFY(document::PartDocument::load(path).features().back().kind ==
                (chamfer ? document::FeatureKind::Chamfer : document::FeatureKind::Fillet));
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
    }
};
} // namespace thecad::ui
QTEST_MAIN(thecad::ui::SolidFeaturesWorkflowTest)
#include "SolidFeaturesWorkflowTests.moc"
