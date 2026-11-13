#include "presentation/SelectionAppearance.h"
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/features/SolidFeaturePanel.h"
#include "ui/PropertyManagerPage.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Graphic3d_Camera.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QSignalSpy>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cmath>
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
    void solidCommandLeavesFlatView_data()
    {
        QTest::addColumn<QString>("action");
        for (const auto* action : {"actionRevolve", "actionRevolvedCut", "actionSweep", "actionSweptCut",
                                  "actionFillet", "actionChamfer", "actionLinearPattern", "actionMirror", "actionCircularPattern"})
            QTest::newRow(action) << QString(action);
    }
    void solidCommandLeavesFlatView()
    {
        QFETCH(QString, action);
        QTemporaryDir temp; auto document=document::PartDocument::createBox({40,30,20});
        modeling::Sketch sketch; sketch.entities.push_back({modeling::EntityKind::Rectangle,{5,5},{15,15},1,"profile"});
        const auto sketchId=document.addSketch(sketch); const auto path=temp.filePath("camera.thepart"); document.save(path);
        SolidTestWindow window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport=window.findChild<CadViewport*>(); QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path)); select(window,sketchId);
        viewport->setStandardView(CadViewport::StandardView::Front);
        const auto camera=viewport->m_graphics.view()->Camera();
        const auto center=camera->Center(); const auto scale=camera->Scale(); const auto projection=camera->ProjectionType();
        command(window,action.toUtf8().constData());
        auto* panel=window.findChild<SolidFeaturePanel*>(); QVERIFY(panel);
        QVERIFY(std::abs(camera->Direction().Z())>0.3 && std::abs(camera->Direction().Z())<0.8);
        QVERIFY(camera->Center().Distance(center)<1e-10); QCOMPARE(camera->Scale(),scale); QCOMPARE(camera->ProjectionType(),projection);
        const Handle(Graphic3d_Camera) featureCamera=new Graphic3d_Camera(camera);
        if (panel->parameter()) panel->parameter()->setValue(2);
        panel->refreshPreview(); QApplication::processEvents(); sameCamera(featureCamera,camera);
        command(window,"actionCancelFeature"); QVERIFY(window.errors.isEmpty());
    }

    void savedDraftJunctionEdit()
    {
        const auto path = qEnvironmentVariable("THECAD_TEST_DRAFT_JUNCTION_PART");
        if (path.isEmpty()) QSKIP("Set THECAD_TEST_DRAFT_JUNCTION_PART to run the saved-part edit.");
        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path));
        const auto features = window.document().features();
        select(window, "fillet-1");
        QElapsedTimer timer;
        timer.start();
        command(window, "actionEditFeature");
        auto* panel = window.findChild<SolidFeaturePanel*>();
        QVERIFY(panel);
        QTRY_VERIFY_WITH_TIMEOUT(panel->candidate().has_value(), 2500);
        qInfo() << "Draft fillet edit preview with downstream history:" << timer.elapsed() << "ms";
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(window.document().features().size(), features.size());
        const auto& edited = window.document().features();
        const auto fillet = std::find_if(edited.begin(), edited.end(),
            [](const auto& feature) { return feature.id == "fillet-1"; });
        QVERIFY(fillet != edited.end());
        QCOMPARE(fillet->edgeTermination, modeling::EdgeTermination::PreserveDraftJunction);
        QCOMPARE(edited.back().id, features.back().id);
        QVERIFY(window.errors.isEmpty());
    }
    void savedFilletPreviewPerformance()
    {
        const auto path = qEnvironmentVariable("THECAD_TEST_FILLET_PART");
        if (path.isEmpty()) QSKIP("Set THECAD_TEST_FILLET_PART to run the saved-part performance regression.");
        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(path));
        QElapsedTimer timer;
        const bool chamfer = qEnvironmentVariable("THECAD_TEST_EDGE_TOOL") == "chamfer";
        command(window, chamfer ? "actionChamfer" : "actionFillet");
        auto* panel = window.findChild<SolidFeaturePanel*>();
        QVERIFY(panel);
        const auto originalCount = window.document().features().size();
        const int requested = qEnvironmentVariable("THECAD_TEST_FILLET_EDGE", "1").toInt();
        const int budgetMs = qEnvironmentVariable("THECAD_TEST_FILLET_BUDGET_MS", "1500").toInt();
        for (int i = 0; i < int(panel->options().size()); ++i) {
            const auto& option = panel->options()[std::size_t(i)];
            if (option.reference.index != requested) continue;
            panel->parameter()->setValue(qEnvironmentVariable("THECAD_TEST_FILLET_SIZE", "2").toDouble());
            const BRepAdaptor_Curve curve(TopoDS::Edge(option.selectionShape));
            const auto midpoint = curve.Value((curve.FirstParameter() + curve.LastParameter()) * 0.5);
            const auto pixel = project(viewport, midpoint);
            QCOMPARE(viewport->referenceCandidateAt(pixel), i);
            QSignalSpy changed(panel, &SolidFeaturePanel::previewChanged);
            timer.start();
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pixel);
            const auto dispatch = timer.elapsed();
            QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 2, budgetMs);
            const auto preview = timer.elapsed();
            QVERIFY2(panel->candidate().has_value(), qPrintable(panel->page()->status->text()));
            QVERIFY(!viewport->m_previewPresentation.IsNull());
            const auto& highlight = panel->candidate()->changes;
            QVERIFY(!highlight.removed.IsNull() || !highlight.added.IsNull());
            timer.restart();
            panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
            const auto confirm = timer.elapsed();
            qInfo() << "Edge" << requested << "click" << dispatch << "ms, preview" << preview
                    << "ms, confirmation" << confirm << "ms";
            QVERIFY2(preview < budgetMs, "The treatment preview missed its latency budget.");
            QCOMPARE(window.document().features().size(), originalCount + 1);
            QCOMPARE(window.document().features().back().kind,
                     chamfer ? document::FeatureKind::Chamfer : document::FeatureKind::Fillet);
            QVERIFY(window.errors.isEmpty());
            return;
        }
        QFAIL("Edge unavailable");
    }
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
    void sweptBossAndCut()
    {
        QTemporaryDir temp;
        auto part = document::PartDocument::createEmpty();
        modeling::Sketch profile;
        profile.entities.push_back({modeling::EntityKind::Circle, {}, {}, 5, "profile"});
        const auto bossProfile = part.addSketch(profile);
        modeling::Sketch path;
        path.plane = modeling::SketchPlane::XZ;
        path.entities.push_back({modeling::EntityKind::Line, {0, 0}, {0, 20}, 1, "path"});
        const auto pathId = part.addSketch(path);
        profile.entities.front().radius = 2;
        const auto cutProfile = part.addSketch(profile);
        const auto filename = temp.filePath("sweep.thepart");
        part.save(filename);

        SolidTestWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>("cadViewport");
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(window.openPartFile(filename));
        QVERIFY(!window.findChild<QAction*>("actionSweptCut")->isEnabled());
        const Handle(Graphic3d_Camera) camera =
            new Graphic3d_Camera(viewport->m_graphics.view()->Camera());
        select(window, bossProfile);
        command(window, "actionSweep");
        auto* panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel && !panel->candidate());
        QVERIFY(!panel->page()->buttons->button(QDialogButtonBox::Ok)->isEnabled());
        panel->findChild<QPushButton*>("sweepPickPath")->click();
        const auto pathPixel = project(viewport, {0, 0, 10});
        QVERIFY(viewport->referenceCandidateAt(pathPixel) >= 0);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pathPixel);
        QVERIFY(panel->candidate());
        QVERIFY(window.document().shape().IsNull());
        const double pi = std::acos(-1.0);
        QVERIFY(std::abs(volume(panel->candidate()->document.shape()) - pi * 25 * 20) < 1e-5);
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::Sweep);
        command(window, "actionUndo");
        QVERIFY(window.document().shape().IsNull());
        command(window, "actionRedo");
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::Sweep);

        select(window, cutProfile);
        command(window, "actionSweptCut");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel);
        select(window, pathId);
        QVERIFY(panel->candidate());
        const double tubeVolume = pi * 21 * 20;
        QVERIFY(std::abs(volume(panel->candidate()->document.shape()) - tubeVolume) < 1e-5);
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::SweptCut);
        window.document().save(filename);
        QVERIFY(window.openPartFile(filename));
        QVERIFY(std::abs(volume(window.document().shape()) - tubeVolume) < 1e-5);
        select(window, window.document().features().back().id);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel && panel->candidate());
        QCOMPARE(panel->findChild<QComboBox*>("sweepPath")->currentData().toString().toStdString(),
                 pathId);
        panel->findChild<QComboBox*>("sweepPath")->setCurrentIndex(-1);
        QVERIFY(!panel->candidate());
        command(window, "actionCancelFeature");
        QVERIFY(std::abs(volume(window.document().shape()) - tubeVolume) < 1e-5);
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
    void circularPatternAroundBodyRim()
    {
        QTemporaryDir temp;
        auto doc = document::PartDocument::createEmpty();
        modeling::Sketch disk;
        disk.entities.push_back({modeling::EntityKind::Circle, {30, 30}, {}, 25, "disk"});
        const auto diskId = doc.addSketch(disk);
        doc.addExtrude(diskId, {"disk"}, 10, modeling::Direction::Positive);
        modeling::Sketch hole;
        hole.entities.push_back({modeling::EntityKind::Circle, {40, 30}, {}, 3, "hole"});
        const auto sketchId = doc.addSketch(hole);
        const auto cutId = doc.addCut(sketchId, {"hole"}, 10, true, modeling::Direction::Positive);
        const auto path = temp.filePath("circular-pattern.thepart");
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
        command(window, "actionCircularPattern");
        auto* panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel && !panel->candidate());
        auto* instances = panel->findChild<QSpinBox*>("patternInstances");
        instances->setValue(4);
        auto* angle = panel->findChild<QDoubleSpinBox*>("patternAngle");
        QCOMPARE(angle->value(), 360.0);
        QVERIFY(panel->findChild<QCheckBox*>("patternEqualSpacing")->isChecked());

        bool picked = false;
        for (int i = 0; i < int(panel->options().size()) && !picked; ++i)
        {
            const auto& option = panel->options()[std::size_t(i)];
            if (option.reference.source != modeling::ReferenceSource::BodyCircleAxis)
                continue;
            BRepAdaptor_Curve rim(TopoDS::Edge(option.selectionShape));
            if (std::abs(rim.Circle().Radius() - 25) > 1e-8)
                continue;
            for (const double fraction : {0.1, 0.3, 0.5, 0.7, 0.9})
            {
                const auto pixel = project(viewport, rim.Value(rim.FirstParameter() +
                    (rim.LastParameter() - rim.FirstParameter()) * fraction));
                if (viewport->rect().contains(pixel) && viewport->referenceCandidateAt(pixel) == i)
                {
                    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, pixel);
                    picked = true;
                    break;
                }
            }
        }
        QVERIFY(picked);
        QVERIFY(panel->candidate());
        const double expected = 10 * std::acos(-1.0) * (625 - 36);
        QVERIFY(std::abs(volume(panel->candidate()->document.shape()) - expected) < 1e-5);
        QCOMPARE(viewport->m_materialPreviewPresentations.size(), std::size_t(1));
        Quantity_Color removed;
        viewport->m_materialPreviewPresentations.front()->Color(removed);
        QVERIFY(removed.Distance(presentation::removedMaterialYellow()) < 1e-8);
        sameCamera(camera, viewport->m_graphics.view()->Camera());

        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        const auto patternId = window.document().features().back().id;
        QCOMPARE(window.document().features().back().kind, document::FeatureKind::CircularPattern);
        command(window, "actionUndo");
        QCOMPARE(window.document().features().back().id, cutId);
        command(window, "actionRedo");
        QCOMPARE(window.document().features().back().id, patternId);

        window.document().save(path);
        QVERIFY(window.openPartFile(path));
        select(window, patternId);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel && panel->candidate());
        QCOMPARE(panel->findChild<QSpinBox*>("patternInstances")->value(), 4);
        panel->findChild<QDoubleSpinBox*>("patternAngle")->setValue(180);
        QVERIFY(panel->candidate());
        command(window, "actionCancelFeature");
        QCOMPARE(window.document().features().back().pattern.angle, 360.0);
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
        QVERIFY(!panel->candidate()); // Selection returns before the kernel computes the preview.
        QTRY_VERIFY(panel->candidate());
        QCOMPARE(edges->selectedItems().size(), 1);
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        panel->parameter()->setValue(1e6);
        QVERIFY(!panel->candidate());
        QVERIFY(!panel->page()->buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QCOMPARE(window.document().features().size(), std::size_t(1));
        panel->parameter()->setValue(1);
        QTRY_VERIFY(panel->candidate());
        panel->page()->buttons->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
        QCOMPARE(window.document().features().size(), std::size_t(2));
        QVERIFY(volume(window.document().shape()) < 24000);
        if (!chamfer)
            QVERIFY(window.document().edgeOptions(true).size() < window.document().edgeOptions().size());
        const double original = volume(window.document().shape());
        const auto id = window.document().features().back().id;
        select(window, id);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>("solidFeaturePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->candidate());
        panel->parameter()->setValue(0.5);
        QTRY_VERIFY(panel->candidate());
        QVERIFY(panel->candidate()->changes.added.IsNull());
        QCOMPARE(viewport->m_materialPreviewPresentations.size(), std::size_t(1));
        Quantity_Color removed;
        viewport->m_materialPreviewPresentations.front()->Color(removed);
        QVERIFY(removed.Distance(presentation::removedMaterialYellow()) < 1e-8);
        const auto treatment = panel->candidate()->changes.removed;
        QVERIFY(TopExp_Explorer(treatment, TopAbs_FACE).More());
        QVERIFY(!TopExp_Explorer(treatment, TopAbs_SOLID).More());
        QVERIFY(volume(panel->candidate()->previewShape) < 24000);
        panel->parameter()->setValue(1.5);
        panel->parameter()->setValue(2);
        QVERIFY(!panel->candidate());
        QTest::keyClick(panel->parameter(), Qt::Key_Return);
        QTRY_VERIFY(window.findChild<SolidFeaturePanel*>() == nullptr);
        QCOMPARE(window.document().features().back().size, 2.0);
        command(window, "actionUndo");
        QVERIFY(std::abs(volume(window.document().shape()) - original) < 1e-6);
        QVERIFY(std::abs(volume(viewport->displayedShape()) - original) < 1e-6);
        command(window, "actionRedo");
        QVERIFY(volume(window.document().shape()) < original);
        window.document().save(path);
        QVERIFY(document::PartDocument::load(path).features().back().kind ==
                (chamfer ? document::FeatureKind::Chamfer : document::FeatureKind::Fillet));
        select(window, id);
        command(window, "actionEditFeature");
        panel = window.findChild<SolidFeaturePanel*>();
        QVERIFY(panel);
        panel->parameter()->setValue(3);
        command(window, "actionCancelFeature");
        QVERIFY(!window.findChild<SolidFeaturePanel*>());
        QCOMPARE(window.document().features().back().size, 2.0);
        sameCamera(camera, viewport->m_graphics.view()->Camera());
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
    }
};
} // namespace thecad::ui
QTEST_MAIN(thecad::ui::SolidFeaturesWorkflowTest)
#include "SolidFeaturesWorkflowTests.moc"
