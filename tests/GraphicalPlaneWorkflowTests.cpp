#include "presentation/SelectionAppearance.h"
#include <QListWidget>
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <Graphic3d_Camera.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cmath>

namespace thecad::ui {
using namespace presentation;
class PlaneTestWindow : public MainWindow {
public:
    QString path;
    QStringList errors;
protected:
    QString chooseSavePath(bool) override { return path; }
    QString chooseOpenPath() override { return path; }
    UnsavedChoice confirmUnsaved() override { return UnsavedChoice::Discard; }
    void reportError(const QString& message) override { errors.append(message); }
};

class GraphicalPlaneWorkflowTest final : public QObject {
    Q_OBJECT
private:
    QPoint project(CadViewport* viewport, const gp_Pnt& point)
    {
        int x, y;
        viewport->m_graphics.view()->Convert(point.X(), point.Y(), point.Z(), x, y);
        return {qRound(x / viewport->devicePixelRatioF()), qRound(y / viewport->devicePixelRatioF())};
    }
    std::optional<QPoint> pickPoint(CadViewport* viewport, modeling::SketchPlane plane)
    {
        for (const double u : {-0.85, -0.45, 0.2, 0.65, 0.85}) {
            for (const double v : {-0.85, -0.45, 0.2, 0.65, 0.85}) {
                const auto pixel = project(viewport, modeling::sketchPoint(plane,
                    {u * viewport->m_planeHalfExtent, v * viewport->m_planeHalfExtent}));
                if (!viewport->rect().contains(pixel)) continue;
                const auto hit = viewport->planeAt(pixel);
                if (hit && hit->plane == plane) return pixel;
            }
        }
        return {};
    }
    void command(PlaneTestWindow& window, const char* name)
    {
        auto* action = window.findChild<QAction*>(name);
        QVERIFY(action && action->isEnabled());
        if (qstrcmp(name, "actionCreateSketch") == 0)
            window.findChild<QTabWidget*>("modelingTabs")->setCurrentIndex(1);
        for (auto* button : window.findChildren<QToolButton*>()) {
            if (button->defaultAction() == action && button->isVisible()) {
                QTest::mouseClick(button, Qt::LeftButton);
                QApplication::processEvents();
                return;
            }
        }
        action->trigger();
        QApplication::processEvents();
    }
    void drag(CadViewport* viewport, QPoint first, QPoint second)
    {
        QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, first);
        QMouseEvent move(QEvent::MouseMove, second, viewport->mapToGlobal(second), Qt::NoButton,
                         Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::NoModifier, second);
        QApplication::processEvents();
    }
private slots:
    void startupPlaneClickStartsOnlyTheFirstSketch()
    {
        PlaneTestWindow window;
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>();
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(viewport->referencePlanesVisible());
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QVERIFY(!viewport->m_sketchEditing);
        QVERIFY(window.document().features().empty());

        const auto plane = modeling::SketchPlane::XZ;
        const auto pixel = pickPoint(viewport, plane);
        QVERIFY(pixel);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *pixel);
        QVERIFY(viewport->m_sketchEditing);
        QCOMPARE(window.document().features().size(), std::size_t(1));
        QCOMPARE(window.document().features().front().sketch.plane, plane);

        command(window, "actionUndo");
        QVERIFY(!viewport->m_sketchEditing);
        QVERIFY(window.document().features().empty());
        command(window, "actionIsometric");
        viewport->showReferencePlanes(true);
        const auto laterClick = pickPoint(viewport, plane);
        QVERIFY(laterClick);
        const auto eye = viewport->m_graphics.view()->Camera()->Eye();
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *laterClick);
        QVERIFY(!viewport->m_sketchEditing);
        QVERIFY(window.document().features().empty());
        QCOMPARE(viewport->m_selectedPlane, std::optional(plane));
        QVERIFY(eye.IsEqual(viewport->m_graphics.view()->Camera()->Eye(), 1e-10));
        QVERIFY(window.errors.isEmpty());
    }
    void planarFaceFirstAndSketchFirst()
    {
        QTemporaryDir temporary;
        auto document=document::PartDocument::createEmpty();modeling::Sketch profile;
        profile.entities={{modeling::EntityKind::Rectangle,{-20,-15},{20,15},1,"rectangle"}};
        const auto source=document.addSketch(profile);document.addExtrude(source,{"rectangle"},20,modeling::Direction::Positive);
        const auto path=temporary.filePath("block.thepart");document.save(path);
        for(bool sketchFirst:{false,true}) {
            PlaneTestWindow window;window.path=path;window.showMaximized();QVERIFY(QTest::qWaitForWindowExposed(&window));
            command(window,"actionOpen");auto* viewport=window.findChild<CadViewport*>();QTRY_VERIFY(viewport->isReady());
            TopTools_IndexedMapOfShape faces;TopExp::MapShapes(document.shape(),TopAbs_FACE,faces);
            int selected=0;modeling::Sketch support;
            for(int i=1;i<=faces.Extent();++i) {
                auto candidate=document.sketchOnFace(i);const auto normal=modeling::sketchAxes(candidate).Direction();
                if((!sketchFirst && normal.Z()>0.99)||(sketchFirst && normal.X()>0.99)){selected=i;support=candidate;break;}
            }
            QVERIFY(selected>0);
            if(sketchFirst)command(window,"actionCreateSketch");
            const auto axes=modeling::sketchAxes(support);
            viewport->m_graphics.view()->SetAt(axes.Location().X(),axes.Location().Y(),axes.Location().Z());
            viewport->m_graphics.view()->SetProj(axes.Direction().X(),axes.Direction().Y(),axes.Direction().Z());
            viewport->m_graphics.view()->SetUp(axes.YDirection().X(),axes.YDirection().Y(),axes.YDirection().Z());viewport->m_graphics.view()->Camera()->SetScale(90);
            viewport->m_graphics.view()->Redraw();const auto pixel=project(viewport,axes.Location());
            QCOMPARE(viewport->faceAt(pixel),selected);
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel);
            if(!sketchFirst){
                QCOMPARE(viewport->selectedSolidFace(),selected);QCOMPARE(viewport->m_graphics.context()->NbSelected(),1);
                for(const auto type:{Prs3d_TypeOfHighlight_Selected,Prs3d_TypeOfHighlight_LocalSelected}){
                    const auto style=viewport->m_graphics.context()->HighlightStyle(type);
                    QVERIFY(style->Color().Distance(selectionBlue())<1e-6);
                    QCOMPARE(style->DisplayMode(),int(AIS_Shaded));QVERIFY(std::abs(style->Transparency()-0.0)<1e-6);
                }
                const auto featureCount=window.document().features().size();const bool dirty=window.isWindowModified();
                QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel);
                QCOMPARE(viewport->selectedSolidFace(),selected);QCOMPARE(viewport->m_graphics.context()->NbSelected(),1);
                std::optional<QPoint> blank;
                for(const auto p:{QPoint(5,5),QPoint(viewport->width()-5,5),QPoint(5,viewport->height()-5)})if(!viewport->faceAt(p)&&!viewport->planeAt(p)){blank=p;break;}
                QVERIFY(blank);QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,*blank);
                QCOMPARE(viewport->selectedSolidFace(),0);QCOMPARE(viewport->m_graphics.context()->NbSelected(),0);
                QCOMPARE(window.document().features().size(),featureCount);QCOMPARE(window.isWindowModified(),dirty);
                QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel);
                QCOMPARE(viewport->selectedSolidFace(),selected);command(window,"actionCreateSketch");
            }
            QCOMPARE(viewport->selectedSolidFace(),0);QCOMPARE(viewport->m_graphics.context()->NbSelected(),0);
            QVERIFY(viewport->m_sketchEditing);QVERIFY(viewport->m_sketch->attachment);
            QCOMPARE(viewport->m_sketch->attachment->faceIndex,selected);
            QVERIFY(viewport->m_graphics.view()->Camera()->Direction().Dot(axes.Direction())< -0.999);
            const auto origin=viewport->screenPoint({0,0});const auto local=viewport->sketchPosition(origin);
            QVERIFY(local && std::abs(local->x)<0.2 && std::abs(local->y)<0.2);
            for(const auto& axis:viewport->m_globalAxes)QVERIFY(axis.IsNull());
            // Reproduce drawing after zooming/panning on top and side body
            // faces through the real MainWindow sketchChanged/regeneration path.
            viewport->m_graphics.view()->SetZoom(4.0);viewport->m_graphics.view()->Pan(25,-15);
            const auto camera=viewport->m_graphics.view()->Camera();
            const auto eye=camera->Eye(),center=camera->Center();const auto up=camera->Up();const auto direction=camera->Direction();const double scale=camera->Scale();
            const auto cameraUnchanged=[&]{const auto current=viewport->m_graphics.view()->Camera();return std::abs(current->Scale()-scale)<1e-7 && current->Eye().Distance(eye)<1e-7 && current->Center().Distance(center)<1e-7 && current->Up().Angle(up)<1e-7 && current->Direction().Angle(direction)<1e-7;};
            const auto draw=[&](const char* tool,modeling::Point2 first,modeling::Point2 second){
                command(window,tool);
                QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,project(viewport,modeling::sketchPoint(support,first)));
                const auto pixel=project(viewport,modeling::sketchPoint(support,second));
                QMouseEvent hover(QEvent::MouseMove,pixel,viewport->mapToGlobal(pixel),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(viewport,&hover);
                QVERIFY(cameraUnchanged());
                QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,pixel);
                QVERIFY(cameraUnchanged());
            };
            draw("actionLine",{-8,6},{5,6});
            QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));QVERIFY(cameraUnchanged());
            draw("actionCircle",{0,-6},{3,-6});
            QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(2));QVERIFY(cameraUnchanged());
            command(window,"actionSelectSketchEntity");
            // Drag the line endpoint and check live updates as well as release.
            const auto end=project(viewport,modeling::sketchPoint(support,viewport->m_sketch->entities[0].second));
            const auto target=end+QPoint(12,0);
            QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,end);
            QMouseEvent move(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);
            QVERIFY(viewport->m_pointDrag && viewport->m_pointDrag->candidate);QVERIFY(cameraUnchanged());
            QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,target);QVERIFY(cameraUnchanged());
            for(int i=0;i<3;++i){command(window,"actionUndo");QVERIFY(cameraUnchanged());}
            QVERIFY(viewport->m_sketch->entities.empty());
            command(window,"actionRedo");QCOMPARE(viewport->m_sketch->entities.size(),std::size_t(1));QVERIFY(cameraUnchanged());
            command(window,"actionUndo");QVERIFY(viewport->m_sketch->entities.empty());QVERIFY(cameraUnchanged());
            command(window,"actionRectangle");
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,project(viewport,modeling::sketchPoint(support,{-4,-3})));
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,project(viewport,modeling::sketchPoint(support,{4,3})));
            command(window,"actionExtrude");auto* contours=window.findChild<QListWidget*>("operationContour");QVERIFY(contours && contours->count()==1);contours->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);
            auto* buttons=window.findChild<QDialogButtonBox*>("operationButtons");QVERIFY(buttons && buttons->button(QDialogButtonBox::Ok)->isEnabled());
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCOMPARE(window.document().features().size(),std::size_t(4));
            QVERIFY(window.errors.isEmpty());window.setWindowModified(false);QVERIFY(window.close());
        }
    }
    void graphicalSelectionHoverOrbitCancelAndPersistence()
    {
        QTemporaryDir temporary;
        PlaneTestWindow window;
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* viewport = window.findChild<CadViewport*>();
        QTRY_VERIFY(viewport->isReady());
        QVERIFY(viewport->m_planesVisible);
        const bool dirty = window.isWindowModified();
        command(window, "actionCreateSketch");
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(viewport->m_selectingPlane);
        QVERIFY(window.document().features().empty());
        std::optional<QPoint> empty;
        for (const QPoint point : {QPoint(5, 5), QPoint(viewport->width()-5, 5), QPoint(5, viewport->height()-5)}) {
            if (!viewport->planeAt(point)) { empty = point; break; }
        }
        QVERIFY(empty);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *empty);
        QVERIFY(window.document().features().empty());
        const auto direction = viewport->m_graphics.view()->Camera()->Direction();
        drag(viewport, QPoint(viewport->width()/2, viewport->height()/2),
             QPoint(viewport->width()/2+80, viewport->height()/2+45));
        QVERIFY(direction.Angle(viewport->m_graphics.view()->Camera()->Direction()) > 0.01);
        QVERIFY(window.document().features().empty());
        QVERIFY(viewport->m_selectingPlane);
        QTest::keyClick(viewport, Qt::Key_Escape);
        QVERIFY(!viewport->m_selectingPlane);
        QVERIFY(window.document().features().empty());
        QCOMPARE(window.isWindowModified(), dirty);
        QVERIFY(!viewport->m_planesVisible);
        for (const auto plane : {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ}) {
            command(window, "actionCreateSketch");
            QVERIFY(viewport->m_selectingPlane);
            command(window, "actionIsometric");
            const auto pixel = pickPoint(viewport, plane);
            QVERIFY(pixel);
            QMouseEvent hover(QEvent::MouseMove, *pixel, viewport->mapToGlobal(*pixel),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &hover);
            QCOMPARE(viewport->m_hoveredPlane, std::optional<modeling::SketchPlane>(plane));
            const auto index = static_cast<std::size_t>(plane);
            QVERIFY(std::abs(viewport->m_referencePlanes[index]->Transparency() - 0.91) < 1e-6);
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(viewport, &leave);
            QVERIFY(!viewport->m_hoveredPlane);
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *pixel);
            QCOMPARE(window.document().features().back().sketch.plane, plane);
            QVERIFY(viewport->m_sketchEditing);
            QVERIFY(!viewport->m_planesVisible);
            QVERIFY(!viewport->m_sketchAxes[0].IsNull());
            QVERIFY(!viewport->m_sketchOrigin.IsNull());
            const auto axes = modeling::sketchAxes(plane);
            QVERIFY(viewport->m_graphics.view()->Camera()->Direction().Dot(axes.Direction()) < -0.999);
            QVERIFY(viewport->m_graphics.view()->Camera()->Up().Dot(axes.YDirection()) > 0.999);
            command(window, "actionRectangle");
            const auto first = project(viewport, modeling::sketchPoint(plane, {-12, -10}));
            const auto second = project(viewport, modeling::sketchPoint(plane, {12, 10}));
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, first);
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, second);
            command(window, "actionFinishSketch");
            QVERIFY(!viewport->m_sketchEditing);
            QVERIFY(!viewport->m_planesVisible);
            QCOMPARE(window.document().features().back().sketch.entities.size(), std::size_t(4));
        }
        QCOMPARE(window.document().features().size(), std::size_t(3));
        // Sketch exit preserves the last normal view. Orient explicitly before
        // picking another plane that would otherwise be edge-on.
        command(window,"actionIsometric");
        // Selected light blue differs from amber hover.
        viewport->showReferencePlanes(true);
        const auto selectedPixel = pickPoint(viewport, modeling::SketchPlane::XY);
        QVERIFY(selectedPixel);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, *selectedPixel);
        QCOMPARE(window.document().features().size(), std::size_t(3));
        QCOMPARE(viewport->m_selectedPlane, std::optional<modeling::SketchPlane>(modeling::SketchPlane::XY));
        QVERIFY(std::abs(viewport->m_referencePlanes[0]->Transparency()-0.91) < 1e-6);
        command(window, "actionCreateSketch");
        QVERIFY(viewport->m_sketchEditing);
        QCOMPARE(window.document().features().size(), std::size_t(4));
        command(window, "actionFinishSketch");
        // Tree plane-first uses the real reference and has no plane-choice dialog.
        auto* tree = window.findChild<QTreeWidget*>("featureTree");
        tree->setCurrentItem(tree->topLevelItem(0)->child(1));
        command(window, "actionCreateSketch");
        QCOMPARE(window.document().features().back().sketch.plane, modeling::SketchPlane::XZ);
        command(window, "actionFinishSketch");
        window.path = temporary.filePath("perpendicular.thepart");
        command(window, "actionSave");
        QVERIFY(!window.isWindowModified());
        QVERIFY(window.close());
        window.showMaximized();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        command(window, "actionNew");
        command(window, "actionOpen");
        QCOMPARE(window.document().features().size(), std::size_t(5));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(window.document().features()[std::size_t(i)].sketch.plane, static_cast<modeling::SketchPlane>(i));
            tree->setCurrentItem(tree->topLevelItem(0)->child(i + 4));
            if (i == 0) {
                const auto rectangle = tree->visualItemRect(tree->topLevelItem(0)->child(i + 4));
                QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rectangle.center());
                QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rectangle.center());
                QApplication::processEvents();
                QVERIFY(viewport->m_sketchEditing);
            } else {
                command(window, "actionEditFeature");
            }
            const auto normal = modeling::sketchAxes(static_cast<modeling::SketchPlane>(i)).Direction();
            QVERIFY(viewport->m_graphics.view()->Camera()->Direction().Dot(normal) < -0.999);
            QVERIFY(viewport->m_graphics.view()->Camera()->Up().Dot(modeling::sketchAxes(static_cast<modeling::SketchPlane>(i)).YDirection()) > 0.999);
            command(window, "actionFinishSketch");
        }
        QVERIFY(window.errors.isEmpty());
        const QString artifacts = qEnvironmentVariable("THECAD_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            window.document().save(artifacts + "/perpendicular-sketches.thepart");
            viewport->showReferencePlanes(true);
            viewport->resetView();
            viewport->m_graphics.view()->Redraw();
            QVERIFY(viewport->m_graphics.view()->Dump((artifacts + "/reference-planes.png").toUtf8().constData()));
        }
        window.setWindowModified(false);
        QVERIFY(window.close());
    }
};
}
QTEST_MAIN(thecad::ui::GraphicalPlaneWorkflowTest)
#include "GraphicalPlaneWorkflowTests.moc"
