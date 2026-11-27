#include "ui/CadViewport.h"
#include "ui/assembly/AssemblyViewport.h"
#include "ui/MainWindow.h"
#include "ui/assembly/AssemblyWindow.h"
#include "ui/input/NavigationInputSettings.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <Graphic3d_Camera.hxx>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <QMainWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <atomic>
#include <memory>

namespace thecad::ui
{
class TouchpadNavigationTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir preferences;
    std::unique_ptr<QMainWindow> host;
    QWidget* surface = nullptr;
    CadViewport* part = nullptr;
    AssemblyViewport* assembly = nullptr;
    Handle(V3d_View) view;
    TopoDS_Shape shape;
    std::unique_ptr<QSignalSpy> edits, picks;

    void createSurface(bool inAssembly)
    {
        host = std::make_unique<QMainWindow>();
        shape = BRepPrimAPI_MakeBox(30, 20, 10).Shape();
        if (inAssembly)
        {
            assembly = new AssemblyViewport;
            assembly->setComponents({{"component", shape, {}, true, false}});
            surface = assembly;
            edits = std::make_unique<QSignalSpy>(assembly, &AssemblyViewport::dragStarted);
            picks = std::make_unique<QSignalSpy>(assembly, &AssemblyViewport::entityPicked);
        }
        else
        {
            part = new CadViewport(shape);
            surface = part;
            edits = std::make_unique<QSignalSpy>(part, &CadViewport::sketchChanged);
            picks = std::make_unique<QSignalSpy>(part, &CadViewport::sketchSelectionChanged);
        }
        host->setCentralWidget(surface);
        host->resize(1000, 760);
        host->show();
        host->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(host.get()));
        QTRY_VERIFY(inAssembly ? assembly->isReady() : part->isReady());
        view = inAssembly ? assembly->m_graphics.view() : part->m_graphics.view();
        if (assembly)
            assembly->fitAll();
        else
            part->fitAll();
        QApplication::processEvents();
        edits->clear();
        picks->clear();
    }
    void move(QPoint point, Qt::KeyboardModifiers modifiers,
              Qt::MouseButtons buttons = Qt::NoButton)
    {
        QMouseEvent event(QEvent::MouseMove, point, surface->mapToGlobal(point), Qt::NoButton,
                          buttons, modifiers);
        QApplication::sendEvent(surface, &event);
    }
    void release(int key)
    {
        QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(surface, &event);
    }
    Handle(Graphic3d_Camera) copyCamera() { return new Graphic3d_Camera(view->Camera()); }
    void unchanged(const Handle(Graphic3d_Camera) & original)
    {
        const auto now = view->Camera();
        QVERIFY(now->Eye().IsEqual(original->Eye(), 1e-10));
        QVERIFY(now->Center().IsEqual(original->Center(), 1e-10));
        QVERIFY(now->Up().IsEqual(original->Up(), 1e-10));
        QCOMPARE(now->Scale(), original->Scale());
        QCOMPARE(now->ProjectionType(), original->ProjectionType());
    }
    void wheel(QPoint pixels, QPoint angles, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QPoint center = surface->rect().center();
        QWheelEvent event(center, surface->mapToGlobal(center), pixels, angles, Qt::NoButton,
                          modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(surface, &event);
    }
    void native(Qt::NativeGestureType kind, double value = 0, QPointF delta = {})
    {
        const QPointF center = surface->rect().center();
        QNativeGestureEvent event(kind, QPointingDevice::primaryPointingDevice(), 2, center, center,
                                  surface->mapToGlobal(center.toPoint()), value, delta);
        QApplication::sendEvent(surface, &event);
    }
    void workspaceData()
    {
        QTest::addColumn<bool>("inAssembly");
        QTest::newRow("part") << false;
        QTest::newRow("assembly") << true;
    }
private slots:
    void initTestCase()
    {
        QVERIFY(preferences.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    }
    void init()
    {
        NavigationInputSettings::instance().setMode(NavigationInputMode::Touchpad, false);
    }
    void cleanup()
    {
        if (edits)
        {
            QCOMPARE(edits->count(), 0);
            QCOMPARE(picks->count(), 0);
        }
        if (part)
            QVERIFY(part->displayedShape().IsSame(shape));
        if (assembly)
            QVERIFY(assembly->m_visuals.front().placement.Form() == gp_Identity);
        edits.reset();
        picks.reset();
        host.reset();
        surface = nullptr;
        part = nullptr;
        assembly = nullptr;
        view.Nullify();
        NavigationInputSettings::instance().setMode(NavigationInputMode::Mouse, false);
    }
    void deviceClassification()
    {
        for (const auto& descriptor :
             {"HID-compliant touch pad", "Synaptics TouchPad", "ACPI\\ELAN1200", "ACPI\\SYN1234",
              "HID_DEVICE_UP:000D_U:0005", "Apple SPI Trackpad"})
            QVERIFY(isTouchpadDescriptor(descriptor));
        for (const auto& descriptor : {"HID-compliant mouse USB\\VID_046D", "Bluetooth Mouse",
                                       "PS/2 Compatible Mouse", "HID_DEVICE_UP:0001_U:0002"})
            QVERIFY(!isTouchpadDescriptor(descriptor));
    }
    void automaticDetectionAndHotplug()
    {
        auto state = std::make_shared<std::atomic<MousePresence>>(MousePresence::Absent);
        NavigationInputSettings settings(nullptr, [state] { return state->load(); });
        settings.setMode(NavigationInputMode::Automatic, false);
        QTRY_COMPARE(settings.mousePresence(), MousePresence::Absent);
        QVERIFY(settings.touchpadEnabled());
        state->store(MousePresence::Present);
        settings.refreshDevices();
        QTRY_COMPARE(settings.mousePresence(), MousePresence::Present);
        QVERIFY(!settings.touchpadEnabled());
        settings.setMode(NavigationInputMode::Touchpad, false);
        QVERIFY(settings.touchpadEnabled());
        settings.setMode(NavigationInputMode::Mouse, false);
        QVERIFY(!settings.touchpadEnabled());
        settings.setMode(NavigationInputMode::Automatic, false);
        state->store(MousePresence::Unknown);
        settings.refreshDevices();
        QTRY_COMPARE(settings.mousePresence(), MousePresence::Unknown);
        QVERIFY(!settings.touchpadEnabled());
        state->store(MousePresence::Absent);
        settings.refreshDevices();
        QTRY_COMPARE(settings.mousePresence(), MousePresence::Absent);
        QVERIFY(settings.touchpadEnabled());
    }
    void preferencePersists()
    {
        const auto probe = [] { return MousePresence::Present; };
        NavigationInputSettings settings(nullptr, probe);
        settings.setMode(NavigationInputMode::Touchpad);
        const auto saved = QSettings("TheCAD", "TheCAD").value("navigation/inputMode").toString();
        QCOMPARE(saved, QString("touchpad"));
        qunsetenv("THECAD_TEST_NAVIGATION");
        NavigationInputSettings reopened(nullptr, probe);
        QCOMPARE(reopened.mode(), NavigationInputMode::Touchpad);
        qputenv("THECAD_TEST_NAVIGATION", "mouse");
        settings.setMode(NavigationInputMode::Automatic);
    }
    void completedScanDoesNotStartDuplicateWork()
    {
        auto calls = std::make_shared<std::atomic<int>>(0);
        NavigationInputSettings settings(nullptr,
                                         [calls]
                                         {
                                             ++*calls;
                                             return MousePresence::Absent;
                                         });
        // Let the worker finish without delivering its queued completion yet.
        QTest::qSleep(50);
        for (int i = 0; i < 20; ++i)
            settings.refreshDevices();
        QTRY_COMPARE(settings.mousePresence(), MousePresence::Absent);
        QCOMPARE(calls->load(), 1);
        settings.refreshDevices();
        QTRY_COMPARE(calls->load(), 2);
    }
    void identifiedTouchpadWithConnectedMouse()
    {
        createSurface(false);
        NavigationInputSettings settings(nullptr, [] { return MousePresence::Present; });
        settings.setMode(NavigationInputMode::Automatic, false);
        QTRY_COMPARE(settings.mousePresence(), MousePresence::Present);
        TouchpadNavigation controller(&settings);
        const auto before = copyCamera();
        const QPoint center = surface->rect().center();
        const auto pivot = [this] { return view->Camera()->Center(); };
        const auto zoom = [](QPointF, double) {};
        QMouseEvent pointer(QEvent::MouseMove, center, surface->mapToGlobal(center), Qt::NoButton,
                            Qt::NoButton, Qt::ShiftModifier);
        QVERIFY(!controller.move(&pointer, view, surface->size(), surface->devicePixelRatioF(),
                                 true, pivot));
        QWheelEvent mouse(center, surface->mapToGlobal(center), {8, 4}, {}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QVERIFY(!controller.wheel(&mouse, view, surface->size(), surface->devicePixelRatioF(),
                                  pivot, zoom));
        unchanged(before);
        QPointingDevice touchpad("test touchpad", 42, QInputDevice::DeviceType::TouchPad,
                                 QPointingDevice::PointerType::Finger,
                                 QInputDevice::Capability::Position, 5, 0);
        QWheelEvent scroll(center, surface->mapToGlobal(center), {8, 4}, {}, Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized,
                           &touchpad);
        QVERIFY(controller.wheel(&scroll, view, surface->size(), surface->devicePixelRatioF(),
                                 pivot, zoom));
        QVERIFY(!view->Camera()->Center().IsEqual(before->Center(), 1e-8));
        QCOMPARE(view->Camera()->Scale(), before->Scale());
        settings.setMode(NavigationInputMode::Mouse, false);
        QVERIFY(!controller.wheel(&scroll, view, surface->size(), surface->devicePixelRatioF(),
                                  pivot, zoom));
    }
    void sketchNavigationDoesNotEditGeometry()
    {
        createSurface(false);
        modeling::Sketch sketch;
        sketch.entities = {{modeling::EntityKind::Line, {-20, -10}, {30, 15}, 1, "line"}};
        part->beginSketch(sketch);
        part->selectSketchTool();
        edits->clear();
        picks->clear();
        const auto frame = modeling::sketchAxes(*part->m_sketch);
        const QPoint start = surface->rect().center();
        move(start, Qt::ShiftModifier);
        move(start + QPoint(30, 20), Qt::ShiftModifier);
        release(Qt::Key_Shift);
        move(start, Qt::ControlModifier);
        move(start + QPoint(20, 30), Qt::ControlModifier);
        release(Qt::Key_Control);
        QCOMPARE(part->m_sketch->entities[0].first.x, -20.0);
        QCOMPARE(part->m_sketch->entities[0].second.y, 15.0);
        QVERIFY(modeling::sketchAxes(*part->m_sketch).Location().IsEqual(frame.Location(), 1e-12));
        QVERIFY(
            modeling::sketchAxes(*part->m_sketch).Direction().IsEqual(frame.Direction(), 1e-12));
    }
    void modifierNavigation_data() { workspaceData(); }
    void modifierNavigation()
    {
        QFETCH(bool, inAssembly);
        createSurface(inAssembly);
        QVERIFY(view);
        const QPoint start = surface->rect().center(), end = start + QPoint(45, 20);
        const auto before = copyCamera();
        move(start, Qt::ShiftModifier);
        unchanged(before);
        move(end, Qt::ShiftModifier);
        QVERIFY(!view->Camera()->Direction().IsEqual(before->Direction(), 1e-6));
        QCOMPARE(view->Camera()->Scale(), before->Scale());
        release(Qt::Key_Shift);
        const auto rotated = copyCamera();
        move(end + QPoint(20, 10), Qt::NoModifier);
        unchanged(rotated);
        move(start, Qt::ControlModifier);
        unchanged(rotated);
        move(end, Qt::ControlModifier);
        QVERIFY(!view->Camera()->Center().IsEqual(rotated->Center(), 1e-7));
        QVERIFY(view->Camera()->Direction().IsEqual(rotated->Direction(), 1e-10));
        QCOMPARE(view->Camera()->Scale(), rotated->Scale());
        release(Qt::Key_Control);
    }
    void resetAndMouseMode_data() { workspaceData(); }
    void resetAndMouseMode()
    {
        QFETCH(bool, inAssembly);
        createSurface(inAssembly);
        const QPoint start = surface->rect().center();
        move(start, Qt::ShiftModifier);
        move(start + QPoint(30, 15), Qt::ShiftModifier);
        const auto before = copyCamera();
        for (const auto kind :
             {QEvent::Leave, QEvent::FocusOut, QEvent::WindowDeactivate, QEvent::UngrabMouse,
              QEvent::DevicePixelRatioChange, QEvent::ScreenChangeInternal})
        {
            QEvent event(kind);
            QApplication::sendEvent(surface, &event);
            move(start - QPoint(150, 100), Qt::ShiftModifier);
            unchanged(before);
        }
        auto& settings = NavigationInputSettings::instance();
        settings.setMode(NavigationInputMode::Mouse, false);
        move(start, Qt::ShiftModifier);
        move(start + QPoint(50, 30), Qt::ControlModifier);
        unchanged(before);
        settings.setMode(NavigationInputMode::Touchpad, false);
        move(start, Qt::ShiftModifier);
        unchanged(before);
        // A held selection button never starts modifier-only navigation.
        move(start + QPoint(40, 25), Qt::ShiftModifier, Qt::LeftButton);
        unchanged(before);
    }
    void panPinchAndWheel_data() { workspaceData(); }
    void panPinchAndWheel()
    {
        QFETCH(bool, inAssembly);
        createSurface(inAssembly);
        int x0, y0, x1, y1;
        view->Convert(0, 0, 0, x0, y0);
        const auto before = copyCamera();
        wheel({7, 4}, {});
        view->Convert(0, 0, 0, x1, y1);
        const double ratio = surface->devicePixelRatioF();
        QVERIFY(std::abs((x1 - x0) / ratio - 7) < 1);
        QVERIFY(std::abs((y1 - y0) / ratio - 4) < 1);
        QCOMPARE(view->Camera()->Scale(), before->Scale());
        QVERIFY(view->Camera()->Direction().IsEqual(before->Direction(), 1e-10));
        const double scale = view->Camera()->Scale();
        native(Qt::BeginNativeGesture);
        native(Qt::ZoomNativeGesture, .2);
        QVERIFY(view->Camera()->Scale() < scale);
        const auto zoomed = copyCamera();
        native(Qt::ZoomNativeGesture, -.1);
        QVERIFY(view->Camera()->Scale() > zoomed->Scale());
        QVERIFY(view->Camera()->Center().IsEqual(zoomed->Center(), 1e-10));
        native(Qt::EndNativeGesture);
        const auto panned = copyCamera();
        native(Qt::PanNativeGesture, 0, {6, -3});
        QVERIFY(!view->Camera()->Center().IsEqual(panned->Center(), 1e-8));
        native(Qt::BeginNativeGesture);
        view->Convert(0, 0, 0, x0, y0);
        for (int i = 0; i < 10; ++i)
            native(Qt::PanNativeGesture, 0, {.2, 0});
        view->Convert(0, 0, 0, x1, y1);
        QVERIFY(std::abs((x1 - x0) / ratio - 2) < 1);
        const double ctrlScale = view->Camera()->Scale();
        wheel({}, {0, 120}, Qt::ControlModifier);
        QVERIFY(view->Camera()->Scale() < ctrlScale);
        NavigationInputSettings::instance().setMode(NavigationInputMode::Mouse, false);
        const double mouseScale = view->Camera()->Scale();
        wheel({}, {0, 120});
        QVERIFY(view->Camera()->Scale() >
                mouseScale); // Preserve the owner's mouse-wheel direction.
    }
    void sharedPreferences()
    {
        for (const bool inAssembly : {false, true})
        {
            std::unique_ptr<QMainWindow> window =
                inAssembly ? std::unique_ptr<QMainWindow>(new AssemblyWindow)
                           : std::unique_ptr<QMainWindow>(new MainWindow);
            auto* action = window->findChild<QAction*>("actionPreferences");
            QVERIFY(action);
            bool visited = false;
            QTimer::singleShot(
                0, window.get(),
                [&]
                {
                    auto* dialog = window->findChild<QDialog*>("preferencesDialog");
                    if (!dialog)
                        return;
                    auto* input = dialog->findChild<QComboBox*>("navigationInputMode");
                    if (!input)
                        return;
                    visited = true;
                    input->setCurrentIndex(input->findData(int(NavigationInputMode::Touchpad)));
                    dialog->reject();
                });
            action->trigger();
            QVERIFY(visited);
            QCOMPARE(NavigationInputSettings::instance().mode(), NavigationInputMode::Touchpad);
        }
    }
};
} // namespace thecad::ui
QTEST_MAIN(thecad::ui::TouchpadNavigationTest)
#include "TouchpadNavigationTests.moc"
