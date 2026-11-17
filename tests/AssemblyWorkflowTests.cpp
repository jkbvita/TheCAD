#include "ui/assembly/AssemblyWindow.h"
#include "ui/Theme.h"
#include "presentation/SelectionAppearance.h"
#include <Prs3d_TextAspect.hxx>
#include "ui/assembly/AssemblyViewport.h"
#include "ui/assembly/AssemblyMateEditor.h"
#include "ui/assembly/AssemblyPropertyEditor.h"
#include "ui/assembly/AssemblyBeltEditor.h"
#include "ui/assembly/AssemblyReferenceWorkflowEditors.h"
#include "ui/widgets/PropertyManagerControls.h"
#include "ui/PropertyManagerPage.h"
#include <Graphic3d_Camera.hxx>
#include <gp_Quaternion.hxx>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTabBar>
#include <cmath>

namespace thecad::ui
{
class AssemblyTestWindow : public AssemblyWindow
{
public:
    QString componentPath, savePath;
    QStringList errors, dependentMates;
    bool allowDeletion = false;

protected:
    QString chooseComponentPath() override { return componentPath; }
    QString chooseAssemblyPath(bool) override { return savePath; }
    bool confirmDiscard() override { return true; }
    bool confirmComponentDeletion(const QStringList&, const QStringList& mates) override
    {
        dependentMates = mates;
        return allowDeletion;
    }
    void reportError(const QString& message) override { errors << message; }
};
class AssemblyWorkflowTest final : public QObject
{
    Q_OBJECT
    void command(AssemblyWindow& window, const char* name)
    {
        auto* action = window.findChild<QAction*>(name);
        QVERIFY2(action && action->isEnabled(), name);
        action->trigger();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QTreeWidgetItem* item(AssemblyWindow& window, const std::string& id)
    {
        for (QTreeWidgetItemIterator it(window.m_tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString() == QString::fromStdString(id))
                return *it;
        return nullptr;
    }
    void reference(AssemblyWindow& window, const std::string& component, bool axis, int index,
                   bool add = false)
    {
        for (QTreeWidgetItemIterator it(window.m_tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole + 1).toString() == "reference")
            {
                const auto& ref = window.m_treeReferences
                                      .at(std::size_t((*it)->data(0, Qt::UserRole + 2).toInt()))
                                      .reference;
                if (ref.componentId == component &&
                    ref.geometry.source == (axis ? modeling::ReferenceSource::BaseAxis
                                                 : modeling::ReferenceSource::BasePlane) &&
                    ref.geometry.index == index)
                {
                    if (add)
                    {
                        (*it)->parent()->setExpanded(true);
                        window.m_tree->scrollToItem(*it);
                        QTest::mouseClick(window.m_tree->viewport(), Qt::LeftButton,
                                          Qt::ControlModifier,
                                          window.m_tree->visualItemRect(*it).center());
                    }
                    else
                        window.m_tree->setCurrentItem(*it);
                    return;
                }
            }
        QFAIL("Mate reference missing from component tree");
    }
    void confirmMate(AssemblyWindow& window)
    {
        QVERIFY(window.m_mate);
        auto* ok = window.m_mate->page()->buttons->button(QDialogButtonBox::Ok);
        QVERIFY2(ok->isEnabled(), qPrintable(window.m_mate->page()->status->text()));
        QTest::mouseClick(ok, Qt::LeftButton);
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void unchanged(const Handle(Graphic3d_Camera) & expected, AssemblyViewport* viewport)
    {
        const auto actual = viewport->camera();
        QVERIFY(expected->Eye().Distance(actual->Eye()) < 1e-10);
        QVERIFY(expected->Center().Distance(actual->Center()) < 1e-10);
        QVERIFY(expected->Up().IsEqual(actual->Up(), 1e-10));
        QCOMPARE(expected->Scale(), actual->Scale());
        QCOMPARE(expected->ProjectionType(), actual->ProjectionType());
    }
    void confirmWorkflow(AssemblyWindow& window)
    {
        QVERIFY(window.m_workflow);
        auto* ok = window.m_workflow->page()->buttons->button(QDialogButtonBox::Ok);
        QVERIFY2(ok->isEnabled(), qPrintable(window.m_workflow->page()->status->text()));
        QTest::mouseClick(ok, Qt::LeftButton);
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
private slots:
    void assemblyUsesPartWorkspaceAppearance()
    {
        QTemporaryDir folder;const auto path=folder.filePath("part.thepart");document::PartDocument::createBox({20,30,40}).save(path);
        document::AssemblyDocument doc;const auto id=doc.insertComponent(path);AssemblyTestWindow window;window.m_session.commit(doc);window.refresh();window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));auto* viewport=window.m_viewport;QTRY_VERIFY(viewport->isReady());
        auto& theme=ThemeManager::instance();const auto original=theme.mode();const auto view=viewport->m_graphics.view();const Handle(Graphic3d_Camera) before=new Graphic3d_Camera(view->Camera());
        for(const bool dark:{true,false}){
            theme.setMode(dark?ThemeMode::Dark:ThemeMode::Light,false);
            Quantity_Color top,bottom;view->GradientBackgroundColors(top,bottom);
            const Quantity_Color expectedTop=dark?Quantity_Color(0.12549,0.15686,0.20,Quantity_TOC_sRGB):Quantity_Color(0.90,0.92,0.95,Quantity_TOC_RGB);
            const Quantity_Color expectedBottom=dark?Quantity_Color(0.20784,0.26275,0.32549,Quantity_TOC_sRGB):Quantity_Color(0.99,0.99,0.99,Quantity_TOC_RGB);
            QVERIFY(top.Distance(expectedTop)<1e-7);QVERIFY(bottom.Distance(expectedBottom)<1e-7);
            QCOMPARE(view->Trihedron()->Scale(),0.08);QVERIFY(view->Trihedron()->LabelAspect(V3d_X)->Aspect()->Color().Distance(Quantity_Color(dark?Quantity_NOC_GRAY80:Quantity_NOC_GRAY30))<1e-7);
            Quantity_Color color;viewport->m_presentations.at(id)->Color(color);QVERIFY(color.Distance(presentation::modelGray())<1e-7);QCOMPARE(viewport->m_presentations.at(id)->Transparency(),0.0);
            unchanged(before,viewport);
        }
        theme.setMode(original,false);
    }
    void visibilityCheckboxAndAutomaticDrag()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("part.thepart");
        document::PartDocument::createBox({20, 20, 20}).save(path);
        document::AssemblyDocument doc;
        const auto id = doc.insertComponent(path);
        doc.setFixed(id, false);
        const auto owned = doc.createInPlacePart(document::PartDocument::createBox({2, 2, 2}),
                                                 {{}, {modeling::ReferenceSource::BasePlane}});
        AssemblyTestWindow window;
        window.m_session.commit(doc);
        window.refresh();
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.m_viewport;
        QTRY_VERIFY(viewport->isReady());
        viewport->standardView(0);
        viewport->fitAll();
        const Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(viewport->camera());
        for (const auto& component : {id, owned})
            for (int i = 0; i < 4; ++i)
            {
                auto* row = item(window, component);
                const bool visible = window.document().component(component).visible;
                row->setCheckState(0, visible ? Qt::Unchecked : Qt::Checked);
                QVERIFY(item(window, component) ==
                        row); // Model notification must finish before item destruction.
                QApplication::processEvents();
                QVERIFY(window.document().component(component).visible != visible);
                QVERIFY(!window.m_workflow);
                unchanged(camera, viewport);
                command(window, "actionUndo");
                QCOMPARE(window.document().component(component).visible, visible);
                command(window, "actionRedo");
                QVERIFY(window.document().component(component).visible != visible);
            }
        QVERIFY(window.document().component(id).visible);
        const auto start = viewport->project({16, 10, 20}),
                   center = viewport->project({10, 10, 20});
        const auto radius = std::hypot(start.x() - center.x(), start.y() - center.y());
        QVERIFY(radius > 10);
        const auto before = window.document().stateKey();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        for (int i = 1; i <= 40; ++i)
        {
            const double angle = i * 0.1;
            const QPoint p(center.x() + qRound(radius * std::cos(angle)),
                           center.y() + qRound(radius * std::sin(angle)));
            QMouseEvent move(QEvent::MouseMove, p, viewport->mapToGlobal(p), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
        }
        QTest::mouseRelease(viewport, Qt::LeftButton);
        QVERIFY(window.document().component(id).placement.GetRotation().GetRotationAngle() > 0.3);
        unchanged(camera, viewport);
        command(window, "actionUndo");
        QCOMPARE(window.document().stateKey(), before);
        QVERIFY(window.errors.isEmpty());
    }
    void tangentCamFaceSelection()
    {
        QTemporaryDir temp;
        modeling::Sketch profile;
        profile.entities = {
            {modeling::EntityKind::Line, {-5, -2}, {5, -2}, 1, "bottom"},
            {modeling::EntityKind::Arc, {5, -2}, {5, 2}, 2, "right", false, {5, 0}},
            {modeling::EntityKind::Line, {5, 2}, {-5, 2}, 1, "top"},
            {modeling::EntityKind::Arc, {-5, 2}, {-5, -2}, 2, "left", false, {-5, 0}}};
        auto part = document::PartDocument::createEmpty();
        const auto sketch = part.addSketch(profile);
        part.addExtrude(sketch, {"bottom", "right", "top", "left"}, 5,
                        modeling::Direction::Positive);
        const auto camPath = temp.filePath("cam.thepart");
        part.save(camPath);
        auto roller = document::PartDocument::createEmpty();
        profile.entities = {{modeling::EntityKind::Circle, {0, 0}, {}, 1, "roller"}};
        const auto rollerSketch = roller.addSketch(profile);
        roller.addExtrude(rollerSketch, {"roller"}, 5, modeling::Direction::Positive);
        const auto rollerPath = temp.filePath("roller.thepart");
        roller.save(rollerPath);
        document::AssemblyDocument doc;
        const auto cam = doc.insertComponent(camPath);
        gp_Trsf placement;
        placement.SetTranslationPart({8, 0, 0});
        const auto follower = doc.insertComponent(rollerPath, placement);
        AssemblyTestWindow window;
        window.m_session.commit(doc);
        window.refresh();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.m_viewport->isReady());
        const Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(window.m_viewport->camera());
        command(window, "actionMate");
        window.m_mate->findChild<QTabBar*>("assemblyMateFamily")->setCurrentIndex(2);
        auto* type = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        type->setCurrentIndex(type->findData(int(document::AssemblyMateKind::Cam)));
        const auto cylinder = [&](const std::string& id)
        {
            const auto options = doc.referenceOptions(id);
            for (const auto& option : options)
                if (option.reference.geometry.source == modeling::ReferenceSource::BodyFace &&
                    option.mateGeometry.kind == modeling::assembly::MateGeometryKind::Cylinder)
                    return option.reference;
            return document::AssemblyReference{};
        };
        window.m_mate->pick(cylinder(cam));
        window.m_mate->pick(cylinder(follower));
        QVERIFY2(window.m_mate->candidate(), qPrintable(window.m_mate->page()->status->text()));
        confirmMate(window);
        QCOMPARE(window.document().mates().size(), std::size_t(1));
        QCOMPARE(window.document().mates()[0].definition->groups[0].references.size(),
                 std::size_t(4));
        unchanged(camera, window.m_viewport);
    }
    void generatedPropertyManagers()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("template.thepart");
        auto part = document::PartDocument::createBox({20, 10, 5});
        modeling::DatumDefinition point;
        point.method = modeling::DatumMethod::PointXYZ;
        point.position = {20, 5, 0};
        const auto pointId = part.addReferenceGeometry(point);
        part.save(path);
        document::AssemblyDocument doc;
        const auto a = doc.insertComponent(path);
        gp_Trsf pose;
        pose.SetTranslationPart({35, 0, 0});
        const auto b = doc.insertComponent(path, pose);
        AssemblyTestWindow window;
        window.m_session.commit(doc);
        window.refresh();
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.m_viewport->isReady());
        window.m_viewport->fitAll();
        QApplication::processEvents();
        const Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(window.m_viewport->camera());
        const auto enterBelt = [&]
        {
            command(window, "actionBeltChain");
            QVERIFY(window.m_workflow);
            reference(window, a, true, 2);
            window.m_workflow->findChild<QDoubleSpinBox*>("beltPulleyDiameter")->setValue(10);
            reference(window, b, true, 2);
            window.m_workflow->findChild<QDoubleSpinBox*>("beltPulleyDiameter")->setValue(10);
            window.m_workflow->findChild<QCheckBox*>("beltCreatePart")->setChecked(true);
        };
        enterBelt();
        QVERIFY2(window.m_workflow->candidate(),
                 qPrintable(window.m_workflow->page()->status->text()));
        window.finishWorkflow(false);
        QVERIFY(window.document().belts().empty());
        QCOMPARE(window.document().components().size(), std::size_t(2));
        unchanged(camera, window.m_viewport);
        enterBelt();
        confirmWorkflow(window);
        QCOMPARE(window.document().belts().size(), std::size_t(1));
        QCOMPARE(window.document().components().size(), std::size_t(3));
        const auto beltId = window.document().belts()[0].id;
        QVERIFY(item(window, beltId));
        unchanged(camera, window.m_viewport);
        window.startWorkflow(AssemblyWindow::Workflow::Belt, beltId);
        window.m_workflow->findChild<QCheckBox*>("beltEngaged")->setChecked(false);
        confirmWorkflow(window);
        QVERIFY(!window.document().belts()[0].engaged);
        command(window, "actionUndo");
        QVERIFY(window.document().belts()[0].engaged);
        command(window, "actionRedo");
        QVERIFY(!window.document().belts()[0].engaged);
        command(window, "actionAssemblyGroundPlane");
        reference(window, {}, false, 0);
        confirmWorkflow(window);
        QVERIFY(window.document().groundPlane());
        unchanged(camera, window.m_viewport);
        command(window, "actionAssetPublisher");
        modeling::GeometryReference p;
        p.source = modeling::ReferenceSource::Datum;
        p.featureId = pointId;
        window.m_workflow->pick({a, p});
        window.m_workflow->pick(
            {a,
             {modeling::ReferenceSource::BaseAxis, {}, {}, {}, {}, modeling::PointRole::First, 0}});
        auto* ground = window.m_workflow->findChild<QComboBox*>("assetGroundPlane");
        static_cast<PropertyReference*>(ground)->pick();
        window.m_workflow->pick({a,
                                 {modeling::ReferenceSource::BasePlane,
                                  {},
                                  {},
                                  {},
                                  {},
                                  modeling::PointRole::First,
                                  0}});
        confirmWorkflow(window);
        QCOMPARE(window.document().assets().size(), std::size_t(1));
        QCOMPARE(window.document().assets()[0].connectors.size(), std::size_t(1));
        unchanged(camera, window.m_viewport);
        command(window, "actionNewInContextPart");
        auto* context = dynamic_cast<AssemblyInPlaceEditor*>(window.m_workflow);
        QVERIFY(context);
        QVERIFY(context->setTemplateFile(path));
        window.m_workflow->pick({a,
                                 {modeling::ReferenceSource::BasePlane,
                                  {},
                                  {},
                                  {},
                                  {},
                                  modeling::PointRole::First,
                                  0}});
        window.m_workflow->findChild<QDoubleSpinBox*>("inContextOffsetZ")->setValue(12);
        confirmWorkflow(window);
        QCOMPARE(window.document().inPlaceParts().size(), std::size_t(1));
        const auto owned = window.document().inPlaceParts()[0].componentId;
        QVERIFY(std::abs(window.document().component(owned).placement.TranslationPart().Z() - 12) <
                1e-7);
        unchanged(camera, window.m_viewport);
        window.allowDeletion = true;
        window.m_tree->setCurrentItem(item(window, beltId));
        command(window, "actionDeleteAssemblySelection");
        QVERIFY(window.document().belts().empty());
        QCOMPARE(window.document().components().size(), std::size_t(3));
        command(window, "actionUndo");
        QCOMPARE(window.document().belts().size(), std::size_t(1));
        window.savePath = temp.filePath("workflows.theasmb");
        command(window, "actionSaveAssemblyAs");
        QVERIFY(window.openAssemblyFile(window.savePath));
        QCOMPARE(window.document().belts().size(), std::size_t(1));
        QCOMPARE(window.document().inPlaceParts().size(), std::size_t(1));
        QCOMPARE(window.document().assets().size(), std::size_t(1));
        QVERIFY(window.errors.isEmpty());
    }
    void typedEntityPickingAndMultipleMates()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("block.thepart");
        document::PartDocument::createBox({20, 20, 20}).save(path);
        document::AssemblyDocument doc;
        const auto a = doc.insertComponent(path), b = doc.insertComponent(path),
                   c = doc.insertComponent(path);
        AssemblyTestWindow window;
        window.m_session.commit(doc);
        window.refresh();
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.m_viewport;
        QTRY_VERIFY(viewport->isReady());
        viewport->standardView(0);
        viewport->fitAll();
        QApplication::processEvents();
        Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(viewport->camera());
        const auto vertex = viewport->hit(viewport->project({0, 0, 20}));
        QCOMPARE(vertex.kind, TopAbs_VERTEX);
        QVERIFY(vertex.second > 0);
        const auto edge = viewport->hit(viewport->project({10, 0, 20}));
        QCOMPARE(edge.kind, TopAbs_EDGE);
        QVERIFY(edge.second > 0);
        window.pickedEntity(QString::fromStdString(vertex.first), vertex.kind, vertex.second, {});
        QCOMPARE(window.m_preselection.size(), std::size_t(1));
        QCOMPARE(window.m_preselection[0].mateGeometry.kind,
                 modeling::assembly::MateGeometryKind::Point);
        unchanged(camera, viewport);
        reference(window, a, false, 0);
        reference(window, b, false, 0, true);
        reference(window, c, false, 0, true);
        command(window, "actionMate");
        QVERIFY(window.m_mate);
        QVERIFY(window.m_mate->findChild<QCheckBox*>("assemblyMateMultiple")->isChecked());
        confirmMate(window);
        QCOMPARE(window.document().mates().size(), std::size_t(2));
        unchanged(camera, viewport);
        reference(window, a, false, 1);
        reference(window, b, false, 1, true);
        command(window, "actionMate");
        auto* kind = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        kind->setCurrentIndex(kind->findData(int(modeling::assembly::MateKind::Parallel)));
        window.m_mate->findChild<QCheckBox*>("assemblyMateKeepOpen")->setChecked(true);
        confirmMate(window);
        QVERIFY(window.m_mate);
        QCOMPARE(window.document().mates().size(), std::size_t(3));
        window.finishMate(false);
        unchanged(camera, viewport);
        QVERIFY(window.errors.isEmpty());
    }
    void preselectedFacesAndPlanes()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("block.thepart");
        document::PartDocument::createBox({20, 20, 20}).save(path);
        document::AssemblyDocument doc;
        const auto a = doc.insertComponent(path);
        gp_Trsf pose;
        pose.SetTranslationPart({35, 0, 0});
        const auto b = doc.insertComponent(path, pose);
        AssemblyTestWindow window;
        window.m_session.commit(doc);
        window.refresh();
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.m_viewport;
        QTRY_VERIFY(viewport->isReady());
        viewport->standardView(0);
        viewport->fitAll();
        QApplication::processEvents();
        const Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(viewport->camera());
        const auto first = viewport->project({10, 10, 20}),
                   second = viewport->project({45, 10, 20});
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, first);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::ControlModifier, second);
        QCOMPARE(window.m_preselection.size(), std::size_t(2));
        QCOMPARE(viewport->m_references.size(), std::size_t(2));
        QVERIFY(viewport->m_referenceVisuals[0].filled && viewport->m_referenceVisuals[1].filled);
        QVERIFY(!window.m_dragCandidate);
        unchanged(camera, viewport);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::ControlModifier, second);
        QCOMPARE(window.m_preselection.size(), std::size_t(1));
        QCOMPARE(viewport->m_selected, a);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::ShiftModifier, second);
        QCOMPARE(window.m_preselection.size(), std::size_t(2));
        command(window, "actionMate");
        QVERIFY(window.m_mate);
        QCOMPARE(window.m_mate->references().size(), std::size_t(2));
        QCOMPARE(window.m_mate->references()[0].componentId, a);
        QCOMPARE(window.m_mate->references()[1].componentId, b);
        auto* kind = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        QCOMPARE(kind->currentData().toInt(), int(document::AssemblyMateKind::Coincident));
        QVERIFY(window.m_mate->candidate());
        QVERIFY(window.document().mates().empty());
        kind->setCurrentIndex(kind->findData(int(document::AssemblyMateKind::Distance)));
        window.m_mate->findChild<QDoubleSpinBox*>("assemblyMateValue")->setValue(7);
        confirmMate(window);
        QCOMPARE(window.document().mates().size(), std::size_t(1));
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().Z() - 7) <
                1e-5);
        unchanged(camera, viewport);
        command(window, "actionUndo");
        QVERIFY(window.document().mates().empty());
        command(window, "actionRedo");
        QCOMPARE(window.document().mates().size(), std::size_t(1));
        command(window, "actionUndo");
        reference(window, a, false, 0);
        reference(window, b, false, 0, true);
        QCOMPARE(window.m_preselection.size(), std::size_t(2));
        command(window, "actionMate");
        QCOMPARE(window.m_mate->references()[0].geometry.source,
                 modeling::ReferenceSource::BasePlane);
        QCOMPARE(window.m_mate->references()[1].geometry.source,
                 modeling::ReferenceSource::BasePlane);
        QVERIFY(window.m_mate->candidate());
        QTest::keyClick(viewport, Qt::Key_Escape);
        QVERIFY(window.document().mates().empty());
        reference(window, a, true, 2);
        reference(window, b, true, 2, true);
        command(window, "actionMate");
        kind = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        QCOMPARE(kind->currentData().toInt(), int(document::AssemblyMateKind::Concentric));
        QVERIFY(window.m_mate->candidate());
        QTest::keyClick(viewport, Qt::Key_Escape);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QVERIFY(window.m_preselection.empty());
        command(window, "actionMate");
        QVERIFY(window.m_mate->references().empty());
        QTest::keyClick(viewport, Qt::Key_Escape);
        unchanged(camera, viewport);
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
        QVERIFY(window.close());
    }
    void referenceAndPreviewPerformance()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("curved-part.thepart");
        auto part = document::PartDocument::createEmpty();
        modeling::Sketch sketch;
        std::vector<std::string> contours;
        for (int i = 0; i < 8; ++i)
        {
            const auto id = "circle-" + std::to_string(i);
            sketch.entities.push_back(
                {modeling::EntityKind::Circle, {double(i * 14), 0}, {}, 4, id});
            contours.push_back(id);
        }
        const auto profile = part.addSketch(sketch);
        modeling::RegionReferences regions;
        for (const auto& id : contours)
            regions.push_back({id});
        part.addExtrudeRegions(profile, regions, 15, modeling::Direction::Positive);
        part.save(path);
        document::AssemblyDocument document;
        std::vector<std::string> ids;
        for (int i = 0; i < 6; ++i)
        {
            gp_Trsf pose;
            pose.SetTranslationPart({0, double(i * 25), 0});
            ids.push_back(document.insertComponent(path, pose));
        }
        AssemblyTestWindow window;
        window.m_session.commit(document);
        window.refresh();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.m_viewport->isReady());
        QElapsedTimer timer;
        timer.start();
        for (const auto& id : ids)
            QVERIFY(!document.referenceOptions(id).empty());
        const auto options = timer.nsecsElapsed();
        timer.restart();
        window.refresh();
        const auto refresh = timer.nsecsElapsed();
        timer.restart();
        command(window, "actionMate");
        const auto open = timer.nsecsElapsed();
        reference(window, ids[0], false, 0);
        reference(window, ids[1], false, 0);
        auto* kind = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        kind->setCurrentIndex(kind->findData(int(document::AssemblyMateKind::Distance)));
        auto* value = window.m_mate->findChild<QDoubleSpinBox*>("assemblyMateValue");
        timer.restart();
        for (int i = 11; i <= 15; ++i)
            value->setValue(i);
        const auto previews = timer.nsecsElapsed();
        QVERIFY(window.m_mate->candidate());
        const auto highlights = window.m_viewport->m_references;
        const auto bodies = window.m_viewport->m_presentations;
        QSignalSpy preview(window.m_mate, &AssemblyMateEditor::previewChanged);
        window.m_mate->refreshPreview();
        QCOMPARE(preview.count(), 0);
        timer.restart();
        for (int i = 0; i < 10; ++i)
            window.render(*window.m_mate->candidate());
        const auto render = timer.nsecsElapsed();
        QCOMPARE(window.m_viewport->m_references.size(), highlights.size());
        for (std::size_t i = 0; i < highlights.size(); ++i)
            QVERIFY(highlights[i] == window.m_viewport->m_references[i]);
        for (const auto& [id, item] : bodies)
            QVERIFY(item == window.m_viewport->m_presentations.at(id));
        qInfo().nospace() << "assembly fixture (6 instances, 8 cylinders/source), ms: options="
                          << options / 1e6 << ", tree/scene=" << refresh / 1e6
                          << ", open Mate=" << open / 1e6 << ", 5 value previews=" << previews / 1e6
                          << ", 10 unchanged scenes=" << render / 1e6;
        QTest::keyClick(window.m_viewport, Qt::Key_Escape);
        QVERIFY(window.document().mates().empty());
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
        QVERIFY(window.close());
    }
    void insertPlaceMateHistoryAndReopen()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("block.thepart");
        document::PartDocument::createBox({20, 20, 20}).save(path);
        AssemblyTestWindow window;
        window.componentPath = path;
        window.savePath = temp.filePath("mechanism.theasmb");
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.m_viewport;
        QTRY_VERIFY2(viewport->isReady(), qPrintable(viewport->initializationError()));
        command(window, "actionInsertComponent");
        QVERIFY(window.m_insertPage);
        for (auto* field : window.m_insertCoordinates)
            field->setValue(0);
        QTest::keyClick(viewport, Qt::Key_Return);
        QCOMPARE(window.document().components().size(), std::size_t(1));
        const auto a = window.document().components()[0].id;
        QVERIFY(window.document().component(a).fixed);
        command(window, "actionInsertComponent");
        for (auto* field : window.m_insertCoordinates)
            field->setValue(0);
        window.m_insertCoordinates[0]->setValue(35);
        QTest::keyClick(viewport, Qt::Key_Return);
        QCOMPARE(window.document().components().size(), std::size_t(2));
        const auto b = window.document().components()[1].id;
        QVERIFY(!window.document().component(b).fixed);
        viewport->standardView(0);
        viewport->fitAll();
        QApplication::processEvents();
        const Handle(Graphic3d_Camera) camera = new Graphic3d_Camera(viewport->camera());
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          viewport->project({10, 10, 20}));
        QCOMPARE(window.m_selected, a);
        QVERIFY(viewport->m_selectedFace > 0);
        unchanged(camera, viewport);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          viewport->project({45, 10, 20}));
        QCOMPARE(window.m_selected, b);
        QVERIFY(viewport->m_selectedFace > 0);
        unchanged(camera, viewport);
        window.findChild<QDoubleSpinBox*>("componentX")->setValue(50);
        QTest::mouseClick(window.findChild<QPushButton*>("applyComponentPlacement"),
                          Qt::LeftButton);
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().X() - 50) <
                1e-6);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          viewport->project({60, 10, 20}));
        QCOMPARE(window.m_selected, b);
        QVERIFY(viewport->m_selectedFace > 0);
        unchanged(camera, viewport);
        // A body drag makes one undoable placement, preserving the camera.
        const auto start = viewport->project({60, 10, 20}), end = start + QPoint(30, -20);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().X() - 50) >
                0.1);
        unchanged(camera, viewport);
        command(window, "actionUndo");
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().X() - 50) <
                1e-6);
        command(window, "actionRedo");
        command(window, "actionUndo");
        QTest::mousePress(viewport, Qt::RightButton, Qt::NoModifier, start);
        QMouseEvent turn(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                         Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &turn);
        QTest::mouseRelease(viewport, Qt::RightButton, Qt::NoModifier, end);
        QVERIFY(window.document().component(b).placement.GetRotation().GetRotationAngle() > 0.01);
        unchanged(camera, viewport);
        command(window, "actionUndo");
        QVERIFY(window.document().component(b).placement.GetRotation().GetRotationAngle() < 1e-8);
        const auto beforeCancel = window.document().stateKey();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        QApplication::sendEvent(viewport, &move);
        QTest::keyClick(viewport, Qt::Key_Escape);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(window.document().stateKey(), beforeCancel);
        unchanged(camera, viewport);
        window.selectComponent({});
        command(window, "actionMate");
        QVERIFY(window.m_mate);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          viewport->project({10, 10, 20}));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          viewport->project({60, 10, 20}));
        auto* kind = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        kind->setCurrentIndex(kind->findData(int(document::AssemblyMateKind::Distance)));
        window.m_mate->findChild<QDoubleSpinBox*>("assemblyMateValue")->setValue(10);
        confirmMate(window);
        QCOMPARE(window.document().mates().size(), std::size_t(1));
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().Z() - 10) <
                1e-5);
        unchanged(camera, viewport);
        auto* mateItem = item(window, window.document().mates()[0].id);
        QVERIFY(mateItem);
        window.m_tree->scrollToItem(mateItem);
        QTest::mouseClick(window.m_tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          window.m_tree->visualItemRect(mateItem).center());
        QTest::mouseDClick(window.m_tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                           window.m_tree->visualItemRect(mateItem).center());
        QVERIFY(window.m_mate);
        window.m_mate->findChild<QDoubleSpinBox*>("assemblyMateValue")->setValue(12);
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().Z() - 10) <
                1e-5);
        confirmMate(window);
        QCOMPARE(window.document().mates().size(), std::size_t(1));
        QVERIFY(std::abs(window.document().component(b).placement.TranslationPart().Z() - 12) <
                1e-5);
        command(window, "actionUndo");
        command(window, "actionMate");
        reference(window, a, true, 2);
        reference(window, b, true, 2);
        confirmMate(window);
        QCOMPARE(window.document().solution().degreesOfFreedom, 1);
        unchanged(camera, viewport);
        command(window, "actionMate");
        reference(window, a, true, 0);
        reference(window, b, true, 0);
        kind = window.m_mate->findChild<QComboBox*>("assemblyMateType");
        kind->setCurrentIndex(kind->findData(int(document::AssemblyMateKind::Angle)));
        window.m_mate->findChild<QDoubleSpinBox*>("assemblyMateValue")->setValue(47);
        confirmMate(window);
        QCOMPARE(window.document().mates().size(), std::size_t(3));
        QCOMPARE(window.document().solution().degreesOfFreedom, 0);
        unchanged(camera, viewport);
        window.m_tree->setCurrentItem(item(window, b));
        command(window, "actionDeleteAssemblySelection");
        QCOMPARE(window.dependentMates.size(), 3);
        QCOMPARE(window.document().components().size(), std::size_t(2));
        window.allowDeletion = true;
        command(window, "actionDeleteAssemblySelection");
        QCOMPARE(window.document().components().size(), std::size_t(1));
        QVERIFY(window.document().mates().empty());
        QVERIFY(QFileInfo::exists(path));
        command(window, "actionUndo");
        QCOMPARE(window.document().mates().size(), std::size_t(3));
        command(window, "actionSaveAssemblyAs");
        QVERIFY(QFileInfo::exists(window.savePath));
        QVERIFY(!window.isWindowModified());
        QVERIFY(window.openAssemblyFile(window.savePath));
        QCOMPARE(window.document().components().size(), std::size_t(2));
        QCOMPARE(window.document().mates().size(), std::size_t(3));
        QCOMPARE(window.document().solution().degreesOfFreedom, 0);
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
        QVERIFY(window.close());
    }
    void cancelInsertionAndMateAndProtectOrigin()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("block.thepart");
        document::PartDocument::createBox().save(path);
        AssemblyTestWindow window;
        window.componentPath = path;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.m_viewport->isReady());
        command(window, "actionInsertComponent");
        QTest::keyClick(window.m_viewport, Qt::Key_Escape);
        QVERIFY(window.document().components().empty());
        QVERIFY(!window.m_session.canUndo());
        command(window, "actionInsertComponent");
        QTest::keyClick(window.m_viewport, Qt::Key_Return);
        const auto id = window.document().components()[0].id;
        command(window, "actionMate");
        reference(window, {}, false, 0);
        reference(window, id, false, 0);
        QVERIFY(window.m_mate->candidate());
        QTest::keyClick(window.m_viewport, Qt::Key_Escape);
        QVERIFY(window.document().mates().empty());
        reference(window, {}, false, 0);
        QVERIFY(!window.findChild<QAction*>("actionDeleteAssemblySelection")->isEnabled());
        window.m_tree->setCurrentItem(item(window, id));
        command(window, "actionFixFloatComponent");
        QVERIFY(!window.document().component(id).fixed);
        command(window, "actionUndo");
        QVERIFY(window.document().component(id).fixed);
        command(window, "actionHideShowComponent");
        QVERIFY(!window.document().component(id).visible);
        command(window, "actionUndo");
        QVERIFY(window.document().component(id).visible);
        QVERIFY2(window.errors.isEmpty(), qPrintable(window.errors.join('\n')));
        QVERIFY(window.close());
    }
};
} // namespace thecad::ui
QTEST_MAIN(thecad::ui::AssemblyWorkflowTest)
#include "AssemblyWorkflowTests.moc"
