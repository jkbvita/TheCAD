#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/AssemblyViewport.h"
#include "ui/assembly/AssemblyMateEditor.h"
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
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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
    void reference(AssemblyWindow& window, const std::string& component, bool axis, int index)
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
private slots:
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
        command(window, "actionRotateComponent");
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        QApplication::sendEvent(viewport, &move);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(window.document().component(b).placement.GetRotation().GetRotationAngle() > 0.01);
        unchanged(camera, viewport);
        command(window, "actionUndo");
        QVERIFY(window.document().component(b).placement.GetRotation().GetRotationAngle() < 1e-8);
        command(window, "actionMoveComponent");
        const auto beforeCancel = window.document().stateKey();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        QApplication::sendEvent(viewport, &move);
        QTest::keyClick(viewport, Qt::Key_Escape);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(window.document().stateKey(), beforeCancel);
        unchanged(camera, viewport);
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
