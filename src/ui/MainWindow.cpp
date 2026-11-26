#include "ui/Theme.h"
#include "ui/startup/WorkspaceLauncher.h"
#include "application/RecentDocuments.h"
#include "ui/PreferencesDialog.h"
#include <QDialogButtonBox>
#include "modeling/SketchTools.h"
#include "modeling/DimensionAnnotation.h"
#include <QInputDialog>
#include "ui/MainWindow.h"
#include "ui/UiCommandSession.h"
#include "ui/files/NativeFileDialogs.h"
#include "exchange/ShapeExporter.h"
#include "ui/widgets/FeatureTreePresenter.h"
#include "ui/widgets/CommandBar.h"
#include "application/FeatureOperation.h"
#include "ui/CadViewport.h"
#include "ui/PropertyManagerPage.h"
#include "ui/features/ExtrusionOptionsPanel.h"
#include "ui/features/SplineParametersDialog.h"
#include "ui/widgets/PropertyManagerControls.h"
#include "ui/widgets/FeatureCommandBar.h"
#include "ui/assembly/AssemblyWindow.h"
#include "ui/ReferenceGeometryUi.h"
#include "ui/features/SolidFeaturePanel.h"
#include "modeling/Sketch.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/ReferencePlanes.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QAction>
#include <QMouseEvent>
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QStyle>
#include <QSplitter>
#include <QStackedWidget>
#include <QScrollArea>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <exception>
#include <cmath>
#include <utility>
#include <stdexcept>
#include <algorithm>
#include <map>
#include <set>
#include <QVariantList>

namespace thecad::ui {
namespace {
QDoubleSpinBox* number(QFormLayout* layout, const QString& label, double value, bool positive = false)
{
    auto* editor = new QDoubleSpinBox;
    editor->setDecimals(6);
    editor->setRange(positive ? 0.000001 : -1000000.0, 1000000.0);
    editor->setValue(value);
    editor->setSuffix(" mm");
    layout->addRow(label, editor);
    return editor;
}

void dialogButtons(QDialog& dialog, QVBoxLayout* layout)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
}

bool legacyPath(const QString& path)
{
    return QFileInfo(path).suffix().compare("tcpart", Qt::CaseInsensitive) == 0;
}

QByteArray fileDigest(const QString& path, bool* readable = nullptr)
{
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    if (readable)
        *readable = opened;
    return opened ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
                  : QByteArray{};
}

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

QString entityText(const modeling::SketchEntity& entity)
{
    using modeling::EntityKind;
    const QString first = QString("(%1, %2)").arg(entity.first.x).arg(entity.first.y);
    if(entity.kind==EntityKind::Point)return QString("Point: %1").arg(first);
    if (entity.kind == EntityKind::Circle) {
        return QString("Circle: center %1, radius %2 mm").arg(first).arg(entity.radius);
    }
    return QString("%1: %2 to (%3, %4)")
        .arg(entity.kind == EntityKind::Line ? (entity.construction?"Centerline":"Line")
                                            : entity.kind == EntityKind::Arc ? "Arc" : "Rectangle", first)
        .arg(entity.second.x).arg(entity.second.y);
}
}

struct MainWindow::OperationDraft {
    document::Feature feature;
    bool editing = false;
    bool fromSketch = false;
    bool modifiedBefore = false;
    bool defaultContour = false;
    bool pickingSketch = false;
    std::optional<document::PartDocument> sourceDocument;
    PropertyManagerPage* panel = nullptr;
    QComboBox* sketches = nullptr;
    QListWidget* contours = nullptr;
    QDoubleSpinBox* length = nullptr;
    std::unique_ptr<ExtrusionOptionsPanel> options;
    std::vector<int> pickOptions;
    QSplitter* pickPanel = nullptr;
    QSplitter* historySplitter = nullptr;
    QList<int> historySizes;
    struct DepthGrip {
        gp_Ax1 endpoint;
        QDoubleSpinBox* field;
        double travelPerMillimeter;
    };
    struct DepthDrag {
        QPointF start, pixelsPerMillimeter;
        QDoubleSpinBox* field;
        double value;
    };
    std::vector<DepthGrip> grips;
    std::optional<DepthDrag> drag;
    QPushButton* preview = nullptr;
    QDialogButtonBox* buttons = nullptr;
    QLabel* selectionCount = nullptr;
    std::vector<modeling::Contour> availableContours;
    std::optional<document::PartDocument> candidate;
    std::string candidateFeatureId;
};

void MainWindow::bindOperationCommands()
{
    for (const auto& entry : {std::pair<const char*, bool>{"feature.extrude", false},
                              {"feature.extruded_cut", true}}) {
        const std::string id = entry.first;
        const bool cut = entry.second;
        m_commands.bindInteractive(id,
            [this, cut](const application::CommandContext&) {
                const auto* action = cut ? m_cut : m_extrude;
                return action && action->isEnabled()
                    ? application::CommandAvailability{true, {}, {}}
                    : application::CommandAvailability{false, "Select a suitable sketch/profile", {"profile"}};
            },
            [this, cut](const application::CommandRequest&, const application::CommandContext&)
                -> std::unique_ptr<application::CommandSession> {
                extrude(cut);
                if (!m_operation) return {};
                auto session = std::make_unique<UiCommandSession>(
                    [this](const application::CommandRequest& update) {
                        if (!m_operation) return application::CommandResult{application::CommandState::Rejected, "Extrude is no longer active"};
                        const auto depth = update.parameters.find("depth");
                        if (depth != update.parameters.end()) {
                            if (!std::holds_alternative<application::Length>(depth->second))
                                return application::CommandResult{application::CommandState::Rejected, "Depth must be a length"};
                            const double mm = std::get<application::Length>(depth->second).millimeters;
                            if (!std::isfinite(mm) || mm <= 0)
                                return application::CommandResult{application::CommandState::Rejected, "Depth must be positive"};
                            m_operation->length->setValue(mm);
                        }
                        updateOperationPreview();
                        return m_operation->candidate
                            ? application::CommandResult{application::CommandState::PreviewReady, "Extrude preview ready"}
                            : application::CommandResult{application::CommandState::WaitingForSelection, "Select a valid contour"};
                    },
                    [this] {
                        updateOperationPreview();
                        if (!m_operation || !m_operation->candidate)
                            return application::CommandResult{application::CommandState::Rejected, "No valid extrude preview"};
                        const auto id = m_operation->candidateFeatureId;
                        m_viewport->clearPreview();
                        if (!applyModel(*m_operation->candidate, id))
                            return application::CommandResult{application::CommandState::Failed, "Could not commit extrusion"};
                        finishOperation(true);
                        return application::CommandResult{application::CommandState::Committed, "Extrusion committed", {}, {id}, true};
                    },
                    [this] {
                        finishOperation(false);
                        return application::CommandResult{application::CommandState::Canceled, "Extrusion canceled"};
                    });
                return session;
            });
    }
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_session(document::PartDocument::createEmpty()),
      m_commands([this] { return commandContext(); })
{
    application::registerBuiltInCommands(m_commands.registry());
    m_commands.bindSessionControls();
    ThemeManager::instance();
    resize(1200, 800);
    m_viewport = new CadViewport(m_session.document().shape(), this);
    setCentralWidget(m_viewport);

    auto* fileMenu = menuBar()->addMenu("&File");
    const auto addCommand = [this, fileMenu](const QString& text, const char* name,
                                           const QKeySequence& shortcut, auto command) {
        auto* action = fileMenu->addAction(text);
        action->setObjectName(name);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, command);
        return action;
    };
    addCommand("&New", "actionNew", QKeySequence::New, [this] { newDocument(); });
    addCommand("New &Assembly...", "actionNewAssembly", {}, [] { showAssemblyWindow(); });
    addCommand("&Open...", "actionOpen", QKeySequence::Open, [this] { openDocument(); });
    addWelcomeActions(fileMenu, this);
    fileMenu->addSeparator();
    addCommand("&Save", "actionSave", QKeySequence::Save, [this] { saveDocument(); });
    addCommand("Save &As...", "actionSaveAs", QKeySequence::SaveAs, [this] { saveDocumentAs(); });
    fileMenu->addSeparator();
    addCommand("E&xit", "actionExit", QKeySequence(Qt::ALT | Qt::Key_F4), [this] { close(); });

    ThemeManager::instance();

    auto* editMenu = menuBar()->addMenu("&Edit");
    auto* viewMenu = menuBar()->addMenu("&View");
    auto* preferences=editMenu->addAction("Preferences…");preferences->setObjectName("actionPreferences");connect(preferences,&QAction::triggered,this,[this]{showPreferences(this);});
    connect(&ThemeManager::instance(),&ThemeManager::changed,this,[this]{hideRelationsContext();for(auto* a:findChildren<QAction*>())if(!a->icon().isNull()&&a->objectName().startsWith("action")&&!a->objectName().contains("Preferences"))a->setIcon(commandIcon(a->text().remove('&')));});
    m_undo=editMenu->addAction("Undo");m_undo->setObjectName("actionUndo");m_undo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    m_redo=editMenu->addAction("Redo");m_redo->setObjectName("actionRedo");m_redo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    connect(m_undo,&QAction::triggered,this,[this]{dispatchCommand("edit.undo");});
    connect(m_redo,&QAction::triggered,this,[this]{dispatchCommand("edit.redo");});
    editMenu->addSeparator();
    auto* insertMenu = menuBar()->addMenu("&Insert");
    auto* toolsMenu = menuBar()->addMenu("&Tools");
    auto* windowMenu = menuBar()->addMenu("&Window");
    auto* helpMenu = menuBar()->addMenu("&Help");

    auto* modelingToolbar = addToolBar("Command Manager");
    modelingToolbar->setObjectName("modelingToolbar");
    modelingToolbar->setMovable(false);
    modelingToolbar->setFloatable(false);
    m_tabs = new QTabWidget;
    m_tabs->setObjectName("modelingTabs");
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->setMaximumHeight(126);
    m_tabs->setMinimumHeight(118);
    m_tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    modelingToolbar->addWidget(m_tabs);
    const auto page = [this](const QString& name) {
        auto* widget = new QWidget;
        auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(2, 3, 2, 2);
        row->setSpacing(0);
        m_tabs->addTab(widget, name);
        return row;
    };
    auto* featureButtons = page("Features");
    auto* sketchButtons = page("Sketch");
    auto* evaluateButtons = page("Evaluate");
    const auto action = [this](const QString& text, const char* name, auto callback) {
        auto* result = new QAction(commandIcon(text), text, this);
        result->setObjectName(name);
        if(QStringList{"actionSmartDimension","actionSelectSketchEntity","actionLine","actionRectangle","actionCircle","actionArc","actionSpline","actionStraightSlot","actionPoint","actionCenterline","actionPolygon"}.contains(name))result->setCheckable(true);
        result->setToolTip(text);
        if(QString::fromLatin1(name)!="actionMeasure")
            connect(result,&QAction::triggered,this,[this]{if(m_measure && m_measure->isChecked())setMeasureActive(false);});
        connect(result, &QAction::triggered, this, callback);
        return result;
    };
    const auto unavailable = [this](const QString& text, const char* name) {
        auto* result = new QAction(commandIcon(text), text, this);
        result->setObjectName(name);
        result->setEnabled(false);
        result->setToolTip(text + " — not implemented in this milestone.");
        return result;
    };
    const auto modelingAction = [this](QHBoxLayout* layout, const QString& text,
                                      const char* name, auto callback) {
        auto* result = new QAction(text, this);
        result->setObjectName(name);
        auto* button = new QToolButton;
        button->setDefaultAction(result);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        layout->addWidget(button);
        if(QString::fromLatin1(name)!="actionMeasure")
            connect(result,&QAction::triggered,this,[this]{if(m_measure && m_measure->isChecked())setMeasureActive(false);});
        connect(result, &QAction::triggered, this, callback);
        return result;
    };
    m_extrude = action("Extruded Boss/Base", "actionExtrude", [this] { dispatchCommand("feature.extrude"); });
    m_cut = action("Extruded Cut", "actionExtrudedCut", [this] { dispatchCommand("feature.extruded_cut"); });
    m_revolve=action("Revolved Boss/Base","actionRevolve",[this]{startSolidFeature(document::FeatureKind::Revolve);});
    m_revolvedCut=action("Revolved Cut","actionRevolvedCut",[this]{startSolidFeature(document::FeatureKind::RevolvedCut);});
    m_sweep=action("Swept Boss/Base","actionSweep",[this]{startSolidFeature(document::FeatureKind::Sweep);});
    m_sweptCut=action("Swept Cut","actionSweptCut",[this]{startSolidFeature(document::FeatureKind::SweptCut);});
    m_fillet=action("Fillet","actionFillet",[this]{startSolidFeature(document::FeatureKind::Fillet);});
    m_chamfer=action("Chamfer","actionChamfer",[this]{startSolidFeature(document::FeatureKind::Chamfer);});
    m_linearPattern=action("Linear Pattern","actionLinearPattern",[this]{startSolidFeature(document::FeatureKind::LinearPattern);});
    m_mirror=action("Mirror","actionMirror",[this]{startSolidFeature(document::FeatureKind::Mirror);});
    m_circularPattern=action("Circular Pattern","actionCircularPattern",[this]{startSolidFeature(document::FeatureKind::CircularPattern);});
    m_referencePlane=action("Reference Plane","actionReferencePlane",[this]{dispatchCommand("reference.plane");});
    m_referenceAxis=action("Reference Axis","actionReferenceAxis",[this]{startReferenceGeometry(modeling::DatumKind::Axis);});
    m_referencePoint=action("Reference Point","actionReferencePoint",[this]{startReferenceGeometry(modeling::DatumKind::Point);});
    populateFeatureCommands(featureButtons,{m_extrude,m_revolve,m_sweep,m_cut,m_revolvedCut,m_sweptCut,
        m_fillet,m_chamfer,m_linearPattern,m_circularPattern,m_mirror,m_referencePlane,m_referenceAxis,m_referencePoint});

    QGridLayout *sketchGrid = nullptr, *drawGrid = nullptr, *modifyGrid = nullptr, *relationGrid = nullptr;
    commandGroup(sketchButtons, "Sketch", sketchGrid);
    m_createSketch = action("Sketch", "actionCreateSketch", [this] { createSketch(); });
    m_finishSketch = action("Exit Sketch", "actionFinishSketch", [this] { finishSketch(); });
    commandButton(m_createSketch, sketchGrid, 0, 0, true);
    commandButton(m_finishSketch, sketchGrid, 0, 1, true);
    commandButton(action("Smart Dimension", "actionSmartDimension", [this]{startSmartDimension();}), sketchGrid, 0, 2, true);
    commandGroup(sketchButtons, "Draw", drawGrid);
    m_selectEntity = action("Select", "actionSelectSketchEntity", [this] { closeSmartDimension();m_viewport->selectSketchTool(); });
    m_line = action("Line", "actionLine", [this] { closeSmartDimension();dispatchCommand("sketch.line"); });
    m_rectangle = action("Rectangle", "actionRectangle", [this] { closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Rectangle); });
    m_circle = action("Circle", "actionCircle", [this] { closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Circle); });
    commandButton(m_selectEntity, drawGrid, 0, 0);
    commandButton(m_line, drawGrid, 1, 0);
    commandButton(m_rectangle, drawGrid, 2, 0);
    commandButton(m_circle, drawGrid, 0, 1);
    m_arc = action("3 Point Arc", "actionArc", [this] {
        closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Arc);
        statusBar()->showMessage("3 Point Arc. Click the start, end, then a point along the arc.");
    });
    commandButton(m_arc, drawGrid, 1, 1);
    m_straightSlot = action("Straight Slot", "actionStraightSlot", [this] {
        closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::StraightSlot);
        statusBar()->showMessage("Straight Slot. Click both end centers, then set the width.");
    });
    commandButton(m_straightSlot, drawGrid, 1, 2);
    m_spline=action("Spline","actionSpline",[this] {
        closeSmartDimension(); m_viewport->setSketchTool(modeling::EntityKind::Spline);
        statusBar()->showMessage("Spline. Click fit points; double-click or Enter to finish. Esc cancels.");
    });
    commandButton(m_spline,drawGrid,2,1);
    m_point=action("Point","actionPoint",[this]{closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Point);});
    m_centerline=action("Centerline","actionCenterline",[this]{closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Line,true);});
    m_polygon=action("Polygon","actionPolygon",[this]{closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Polygon);});
    m_construction=action("For Construction","actionConstructionLine",[this]{toggleConstruction();});m_construction->setCheckable(true);
    commandButton(m_centerline,drawGrid,0,2);
    commandButton(m_point,drawGrid,2,2);commandButton(m_polygon,drawGrid,0,3);
    m_polygon->setToolTip("Regular hexagon: click the center, then set size and orientation");
    commandGroup(sketchButtons, "Modify", modifyGrid);
    using Modifier = modeling::SketchModifier;
    commandButton(action("Trim Entities", "actionTrimEntities",
                         [this] { startSketchModifier(Modifier::Trim); }), modifyGrid, 0, 0);
    commandButton(action("Extend Entities", "actionExtendEntities",
                         [this] { startSketchModifier(Modifier::Extend); }), modifyGrid, 1, 0);
    commandButton(action("Split Entities", "actionSplitEntities",
                         [this] { startSketchModifier(Modifier::Split); }), modifyGrid, 2, 0);
    commandButton(action("Convert Entities", "actionConvertEntities",
                         [this] { startSketchModifier(Modifier::Convert); }), modifyGrid, 0, 1);
    commandButton(action("Offset Entities", "actionOffsetEntities",
                         [this] { startSketchModifier(Modifier::Offset); }), modifyGrid, 1, 1);
    commandButton(action("Mirror Entities", "actionMirrorEntities",
                         [this] { startSketchModifier(Modifier::Mirror); }), modifyGrid, 2, 1);
    commandButton(action("Linear Sketch Pattern", "actionLinearSketchPattern",
                         [this] { startSketchModifier(Modifier::LinearPattern); }), modifyGrid, 0, 2);
    commandButton(action("Circular Sketch Pattern", "actionCircularSketchPattern",
                         [this] { startSketchModifier(Modifier::CircularPattern); }), modifyGrid, 1, 2);
    commandButton(unavailable("Move Entities", "actionMoveEntities"), modifyGrid, 2, 2);
    commandGroup(sketchButtons, "Relations / inspection", relationGrid);
    commandButton(action("Display/Delete Relations", "actionDisplayDeleteRelations", [this]{startConstraints();}), relationGrid, 0, 0);
    commandButton(unavailable("Repair Sketch", "actionRepairSketch"), relationGrid, 1, 0);
    commandButton(unavailable("Shaded Sketch Contours", "actionShadedSketchContours"), relationGrid, 2, 0);
    sketchButtons->addStretch();
    QGridLayout* evaluateGrid = nullptr;
    commandGroup(evaluateButtons, "Evaluate", evaluateGrid);
    m_measure = action("Measure", "actionMeasure", [this](bool active) {
        if (active && m_commands.activeCommandId() == "sketch.line")
            m_commands.cancel(m_commands.activeSessionId());
        application::CommandRequest request{"inspect.measure"};
        request.parameters["active"] = active;
        if (!m_commands.execute(request)) m_measure->setChecked(false);
    });
    m_measure->setCheckable(true);
    commandButton(m_measure, evaluateGrid, 0, 0, true);
    toolsMenu->addMenu("Evaluate")->addAction(m_measure);
    commandButton(unavailable("Mass Properties", "actionMassProperties"), evaluateGrid, 0, 1, true);
    commandButton(unavailable("Check Geometry", "actionCheckGeometry"), evaluateGrid, 0, 2, true);
    evaluateButtons->addStretch();

    addToolBarBreak();
    auto* toolbar = addToolBar("View");
    toolbar->setObjectName("viewToolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    auto* fit = toolbar->addAction(commandIcon("Fit"), "Fit All");
    fit->setObjectName("actionFitAll");
    fit->setShortcut(QKeySequence(Qt::Key_F));
    connect(fit, &QAction::triggered, m_viewport, &CadViewport::fitAll);
    auto* reset = toolbar->addAction(commandIcon("Part"), "Isometric");
    reset->setObjectName("actionIsometric");
    reset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_7));
    using StandardView = CadViewport::StandardView;
    connect(reset, &QAction::triggered, this, [this] { dispatchCommand("view.isometric"); });
    viewMenu->addAction(fit);
    auto* resetView = viewMenu->addAction("Reset View");
    resetView->setShortcut(QKeySequence(Qt::Key_Home));
    connect(resetView, &QAction::triggered, m_viewport, &CadViewport::resetView);
    auto* orientations = viewMenu->addMenu("Orientation");
    const std::array<const char*, 6> viewLabels{"Front", "Back", "Left", "Right", "Top", "Bottom"};
    for (std::size_t i = 0; i < viewLabels.size(); ++i) {
        auto* orientation = orientations->addAction(viewLabels[i]);
        orientation->setObjectName(QString("actionView%1").arg(viewLabels[i]));
        orientation->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + int(i))));
        connect(orientation, &QAction::triggered, this, [this, i] { m_viewport->setStandardView(StandardView(i)); });
    }
    orientations->addAction(reset);
    auto* normal=toolbar->addAction(commandIcon("Plane"),"Normal To");normal->setObjectName("actionNormalTo");normal->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_8));viewMenu->addAction(normal);connect(normal,&QAction::triggered,m_viewport,&CadViewport::normalTo);
    auto* hideShow=viewMenu->addMenu("Hide/Show");auto* relationVisibility=hideShow->addAction("Sketch Relations");relationVisibility->setObjectName("actionShowSketchRelations");relationVisibility->setCheckable(true);relationVisibility->setChecked(true);
    connect(relationVisibility,&QAction::toggled,m_viewport,&CadViewport::showSketchRelations);
    insertMenu->addAction(m_createSketch);
    insertMenu->addAction(m_extrude);
    insertMenu->addAction(m_cut);
    auto* referenceMenu=insertMenu->addMenu("Reference Geometry");referenceMenu->addActions({m_referencePlane,m_referenceAxis,m_referencePoint});
    toolsMenu->addAction(m_selectEntity);
    auto* tolerance=toolsMenu->addAction("Picking Tolerance...");
    tolerance->setObjectName("actionSketchPickingTolerance");
    connect(tolerance,&QAction::triggered,this,[this]{
        bool accepted=false;
        const double pixels=QInputDialog::getDouble(this,"Picking Tolerance","Tolerance (logical pixels)",
            m_viewport->pickingTolerance(),1,32,1,&accepted);
        if(accepted){m_viewport->setPickingTolerance(pixels);if(auto* control=findChild<QDoubleSpinBox*>("pickingTolerance")){const QSignalBlocker blocker(control);control->setValue(pixels);}}
    });
    toolsMenu->addSeparator();
    toolsMenu->addAction(findChild<QAction*>("actionDisplayDeleteRelations"));
    toolsMenu->addAction(findChild<QAction*>("actionRepairSketch"));
    auto* maximize = windowMenu->addAction("Maximize");
    connect(maximize, &QAction::triggered, this, &QWidget::showMaximized);
    auto* restore = windowMenu->addAction("Restore");
    connect(restore, &QAction::triggered, this, &QWidget::showNormal);
    auto* about = helpMenu->addAction("About TheCAD");
    about->setObjectName("actionAboutTheCAD");
    connect(about, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("About TheCAD");
        auto* layout = new QVBoxLayout(&dialog);

        auto* logo = new QLabel(&dialog);
        logo->setAccessibleName("TheCAD logo");
        logo->setAlignment(Qt::AlignCenter);
        const QIcon artwork(":/branding/thecad-logo.png");
        logo->setPixmap(artwork.pixmap(QSize(280, 280), dialog.devicePixelRatioF()));
        layout->addWidget(logo);

        auto* description = new QLabel("Open-source parametric mechanical CAD prototype.\n"
                                       "C++ / Qt 6 / Open CASCADE Technology", &dialog);
        description->setAlignment(Qt::AlignCenter);
        layout->addWidget(description);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    });
    auto* historyDock = new QDockWidget("FeatureManager", this);
    m_historyDock = historyDock;
    historyDock->setObjectName("historyDock");
    historyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    historyDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* history = new QWidget;
    auto* historyLayout = new QVBoxLayout(history);
    historyLayout->setContentsMargins(5, 5, 5, 5);
    historyLayout->setSpacing(5);
    auto* historySplitter = new QSplitter(Qt::Vertical);
    historySplitter->setChildrenCollapsible(false);
    m_tree = new QTreeWidget;
    m_treePresenter=std::make_unique<FeatureTreePresenter>(*m_tree);
    m_tree->setObjectName("featureTree");
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumSize(240, 170);
    m_tree->setIndentation(17);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    historySplitter->addWidget(m_tree);
    auto* geometryPanel = new QWidget;
    auto* geometryLayout = new QVBoxLayout(geometryPanel);
    geometryLayout->setContentsMargins(0, 0, 0, 0);
    geometryLayout->setSpacing(4);
    auto* editButton = new QToolButton;
    m_editFeature = new QAction("Edit selected feature", this);
    m_editFeature->setObjectName("actionEditFeature");
    editButton->setDefaultAction(m_editFeature);
    geometryLayout->addWidget(editButton);
    geometryLayout->addWidget(new QLabel("Sketch entities"));
    m_entities = new QListWidget;
    m_entities->setObjectName("sketchEntities");
    m_entities->setMinimumHeight(65);
    m_entities->setMaximumHeight(150);
    geometryLayout->addWidget(m_entities);
    m_propertyPanel = new QGroupBox("Selected entity properties");
    m_propertyPanel->setObjectName("entityProperties");
    auto* propertyLayout = new QVBoxLayout(m_propertyPanel);
    propertyLayout->setContentsMargins(7, 14, 7, 5);
    m_entitySummary = new QLabel;
    m_entitySummary->setWordWrap(true);
    propertyLayout->addWidget(m_entitySummary);
    auto* propertyForm = new QFormLayout;
    propertyForm->setContentsMargins(0, 0, 0, 0);
    propertyForm->setSpacing(3);
    m_propertyFirstX = number(propertyForm, "First / center U", 0.0);
    m_propertyFirstY = number(propertyForm, "First / center V", 0.0);
    m_propertySecondX = number(propertyForm, "Second U", 0.0);
    m_propertySecondY = number(propertyForm, "Second V", 0.0);
    m_propertyRadius = number(propertyForm, "Radius", 1.0, true);
    m_propertyFirstX->setObjectName("propertyFirstX");
    m_propertyFirstY->setObjectName("propertyFirstY");
    m_propertySecondX->setObjectName("propertySecondX");
    m_propertySecondY->setObjectName("propertySecondY");
    m_propertyRadius->setObjectName("propertyRadius");
    propertyLayout->addLayout(propertyForm);
    m_applyProperties = new QPushButton("Apply properties");
    m_applyProperties->setObjectName("applyEntityProperties");
    propertyLayout->addWidget(m_applyProperties);
    geometryLayout->addWidget(m_propertyPanel);
    auto* entityButtons = new QHBoxLayout;
    m_editEntity = modelingAction(entityButtons, "Edit geometry...", "actionEditEntity", [this] { editSketchEntity(); });
    m_deleteEntity = modelingAction(entityButtons, "Remove", "actionDeleteEntity", [this] { deleteSketchEntity(); });
    m_deleteEntity->setShortcut(QKeySequence(Qt::Key_Delete));
    m_deleteEntity->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_entities->addAction(m_deleteEntity);
    geometryLayout->addLayout(entityButtons);
    auto* geometryScroll=new QScrollArea;geometryScroll->setObjectName("sketchGeometryScroll");geometryScroll->setWidgetResizable(true);geometryScroll->setFrameShape(QFrame::NoFrame);geometryScroll->setWidget(geometryPanel);
    connect(m_measure, &QAction::toggled, geometryScroll, &QWidget::setHidden);
    historySplitter->addWidget(geometryScroll);
    historySplitter->setStretchFactor(0, 2);
    historySplitter->setStretchFactor(1, 1);
    historyLayout->addWidget(historySplitter);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &MainWindow::refreshMeasurement);
    m_managerStack = new QStackedWidget;
    m_managerStack->setObjectName("managerStack");
    m_managerStack->addWidget(history);
    historyDock->setWidget(m_managerStack);
    addDockWidget(Qt::LeftDockWidgetArea, historyDock);
    resizeDocks({historyDock}, {300}, Qt::Horizontal);
    createMeasurePanel();
    viewMenu->addSeparator();
    viewMenu->addAction(historyDock->toggleViewAction());
    editMenu->addAction(m_editEntity);
    editMenu->addAction(m_deleteEntity);
    initializeSelectionUi(viewMenu);
    editMenu->addAction(m_deleteFeatures);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this] {
        if (m_measure->isChecked()) measureTreeSelection();
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { if(!m_measure->isChecked())editSelectedFeature(); });
    connect(m_editFeature, &QAction::triggered, this, [this] { editSelectedFeature(); });
    connect(m_entities, &QListWidget::itemDoubleClicked, this, [this] {
        if (!m_measure->isChecked()) editSketchEntity();
    });
    connect(m_entities, &QListWidget::itemSelectionChanged, this, [this] {
        updateActions();
        updateEntityProperties();
        if (m_session.editingSketch() && m_entities->currentRow() >= 0)
            m_viewport->selectSketchEntity(QString::fromStdString(m_session.editingSketch()->entities.at(std::size_t(m_entities->currentRow())).id));
    });
    connect(m_applyProperties, &QPushButton::clicked, this, [this] {
        if (!m_session.editingSketch() || m_entities->currentRow() < 0) return;
        auto entity = m_session.editingSketch()->entities.at(std::size_t(m_entities->currentRow()));
        entity.first = {m_propertyFirstX->value(), m_propertyFirstY->value()};
        if (modeling::isCircular(entity.kind)) entity.radius = m_propertyRadius->value();
        if(entity.kind!=modeling::EntityKind::Point && entity.kind!=modeling::EntityKind::Circle)entity.second = {m_propertySecondX->value(), m_propertySecondY->value()};
        applySketchEntityEdit(entity);
    });
    connect(m_viewport, &CadViewport::sketchEntitySelected, this, [this](const QString& id) {
        if (!m_session.editingSketch()) return;
        constraintEntitySelected(id);
        for (std::size_t i = 0; i < m_session.editingSketch()->entities.size(); ++i) {
            if (m_session.editingSketch()->entities[i].id == id.toStdString()) {
                const QSignalBlocker blocker(m_entities);m_entities->setCurrentRow(int(i));
                updateEntityProperties();updateActions();
                return;
            }
        }
        {const QSignalBlocker blocker(m_entities);m_entities->setCurrentRow(-1);}
        updateActions();
    });
    connect(m_viewport, &CadViewport::sketchInteractionError, this, [this](const QString& message) {
        statusBar()->showMessage(message);
    });
    connect(m_viewport, &CadViewport::sketchPointSelected, this, [this](const QString& id, int role) {
        m_constraintPoint=modeling::SketchPointRef{id.toStdString(),modeling::PointRole(role)};
        constraintPointSelected(*m_constraintPoint);
        const QString pointName = id.isEmpty() ? "Sketch origin (0, 0)" : QString("%1 of %2")
            .arg(modeling::isSplinePoint(modeling::PointRole(role)) ? QString("Fit point %1").arg(role-int(modeling::PointRole::SplineInterior)+2) : role == int(modeling::PointRole::Second) ? "Endpoint" : role == int(modeling::PointRole::Center) ? "Center" : "Start point", id);
        statusBar()->showMessage(pointName + " selected. Snapping uses exact sketch coordinates.");
    });
    connect(m_viewport,&CadViewport::sketchSelectionChanged,this,[this]{hideRelationsContext();updateActions();if(m_constraints && m_viewport->selectedSketchRelation().empty())updateConstraintDraft();if(m_sketchModifier)refreshSketchModifier(true);});
    connect(m_viewport,&CadViewport::sketchModifierEntityPicked,this,&MainWindow::pickSketchMirrorEntity);
    connect(m_viewport, &CadViewport::sketchLineEditPointed, this, &MainWindow::updateSketchLineEdit);
    connect(m_viewport,&CadViewport::relationsContextRequested,this,&MainWindow::showRelationsContext);
    qApp->installEventFilter(this);
    connect(m_viewport,&CadViewport::sketchRelationDeleteRequested,this,[this](const QString& id){deleteRelation(id.toStdString());});
    connect(m_viewport,&CadViewport::sketchDeleteRequested,this,[this]{deleteSketchEntity();});
    connect(m_viewport,&CadViewport::sketchToolChanged,this,[this]{
        hideRelationsContext();updateSketchToolActions();
        if (!m_viewport->sketchTool() || *m_viewport->sketchTool() != modeling::EntityKind::Line ||
            m_viewport->constructionTool())
            if (m_commands.activeCommandId() == "sketch.line")
                m_commands.cancel(m_commands.activeSessionId());
    });
    connect(m_viewport,&CadViewport::constraintPickingCanceled,this,[this]{closeConstraints();closeSmartDimension();});
    connect(m_viewport,&CadViewport::dimensionPlacementRequested,this,[this](modeling::Point2 point){placeSmartDimension(point);});
    connect(m_viewport,&CadViewport::dimensionReferencePicked,this,[this](const QString& id,int role){smartReferencePicked(id,role);});
    connect(m_viewport,&CadViewport::dimensionBodyReferencePicked,this,&MainWindow::smartBodyReferencePicked);
    connect(m_viewport,&CadViewport::dimensionPointerMoved,this,[this](modeling::Point2 point){updateSmartPreview(point);});
    connect(m_viewport,&CadViewport::dimensionVariantRequested,this,[this](bool radius){if(m_smartDimension && !m_smartDimension->editor){m_smartDimension->radius=radius;if(m_smartDimension->pending)updateSmartPreview(modeling::dimensionAnnotationPosition(smartDimensionSketch(),*m_smartDimension->pending));}});
    connect(m_viewport,&CadViewport::dimensionEditRequested,this,[this](const QString& id){if(m_session.editingSketch())editSmartDimension(id.toStdString());});
    connect(m_viewport, &CadViewport::sketchChanged, this, [this](const modeling::Sketch& sketch) {
        if (!m_session.editingSketch()) return;
        try {
            modeling::validateSketchRelations(sketch);
            (void)modeling::sketchShape(sketch);
            acceptConstrainedSketch(sketch);
            m_viewport->setSketch(*m_session.editingSketch());
            setWindowModified(true);
            updateEntityList();
            updateActions();
        } catch (const std::exception& error) {
            m_viewport->setSketch(*m_session.editingSketch());
            reportError(QString::fromUtf8(error.what()));
        }
    });
    connect(m_viewport, &CadViewport::sketchRegionClicked, this, [this](modeling::Point2 point) { if(m_solidFeature)m_solidFeature->pickRegion(point);else operationRegionClicked(point); });
    connect(m_viewport, &CadViewport::referencePlaneSelected, this, &MainWindow::referencePlaneSelected);
    connect(m_viewport,&CadViewport::solidFaceSelected,this,[this](int index){
        if(m_session.editingSketch() || featureDraftActive())return;
        m_selectedFace=0;m_selectedPlane.reset();m_session.selectedFeatureId().clear();
        {const QSignalBlocker blocker(m_tree);m_tree->clearSelection();}
        m_viewport->setSelectedReferencePlane({});updateActions();
        if(index==0){m_viewport->setSelectedSolidFace(0);return;}
        if(m_selectingPlane)try {(void)m_session.document().sketchOnFace(index);}catch(const std::exception& error){m_selectedFace=0;m_viewport->setSelectedSolidFace(0);reportError(QString::fromUtf8(error.what()));return;}
        m_selectedPlane.reset();m_selectedFace=index;m_viewport->setSelectedReferencePlane({});
        m_viewport->setSelectedSolidFace(index);
        if(m_selectingPlane)createSketchOnFace(index);
        else statusBar()->showMessage("Planar face selected. Click Sketch to create an attached sketch.");
    });
    connect(m_viewport, &CadViewport::planeSelectionCanceled, this, [this] { planeSelectionCanceled(); });
    connect(m_viewport,&CadViewport::referenceGeometryPicked,this,[this](int option){
        if (m_operation && option >= 0 && option < int(m_operation->pickOptions.size()))
            m_operation->options->picked(m_operation->pickOptions[std::size_t(option)]);
        else if (m_solidFeature) m_solidFeature->pickReference(option);
        else referenceGeometryPicked(option);
    });
    connect(m_viewport,&CadViewport::datumSelected,this,[this](const QString& id){
        if(featureDraftActive()||m_session.editingSketch())return;
        if(id.isEmpty()){m_session.selectedFeatureId().clear();{const QSignalBlocker blocker(m_tree);m_tree->clearSelection();m_tree->setCurrentItem(nullptr);}updateActions();return;}
        if(m_selectingPlane){createSketchOnReferencePlane(id.toStdString());return;}
        selectFeature(id.toStdString());
    });

    auto* cancelFeature = new QAction("Cancel feature", this);
    cancelFeature->setObjectName("actionCancelFeature");cancelFeature->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(cancelFeature);connect(cancelFeature,&QAction::triggered,this,[this]{
        if(!m_commands.activeSessionId().empty() && (m_operation || m_referenceDraft))
            m_commands.cancel(m_commands.activeSessionId());
        else if(m_sketchModifier)finishSketchModifier(false);
        else if(m_solidFeature)finishSolidFeature(false);
        else if(m_referenceDraft)finishReferenceGeometry(false);
        else finishOperation(false);
    });
    statusBar()->showMessage("Click Front, Top or Right in the viewport to start your first sketch.");
    m_dimensions = new QLabel(this);
    statusBar()->addPermanentWidget(m_dimensions);
    bindCommandHandlers();
    bindOperationCommands();
    bindReferenceCommands();
    setWindowModified(true);
    updateDocumentUi();
    m_viewport->showReferencePlanes(true);
    connect(m_viewport, &CadViewport::initializationFailed, this, [this](const QString& message) {
        QTimer::singleShot(0, this, [this, message] { reportError(message + "\nCheck your graphics driver and OpenGL support."); });
    });
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    closeSmartDimension();
    if(m_solidFeature){delete m_solidFeature;m_solidFeature=nullptr;}
    if(m_referenceDraft){auto* page=m_referenceDraft->page;m_referenceDraft.reset();delete page;}
    if (m_operation) {
        auto* panel = m_operation->panel;
        m_operation.reset();
        delete panel;
    }
    delete takeCentralWidget();
    m_viewport = nullptr;
}

const document::PartDocument& MainWindow::document() const { return m_session.document(); }
QString MainWindow::currentPath() const { return m_session.path(); }

void MainWindow::setEditInPlaceContext(const QString& componentName,
                                      const std::vector<TopoDS_Shape>& surroundings,
                                      const std::vector<TopoDS_Shape>& referencedSurroundings,
                                      std::function<void()> finished)
{
    m_inPlaceSurroundings = surroundings;
    m_inPlaceReferenced = referencedSurroundings;
    m_inPlaceFinished = std::move(finished);
    m_inPlaceToolbar = addToolBar("Edit In Place");
    m_inPlaceToolbar->setObjectName("editInPlaceToolbar");
    m_inPlaceToolbar->setMovable(false);
    m_inPlaceToolbar->setFloatable(false);
    auto* title = new QLabel(QString("Editing %1 in assembly").arg(componentName), this);
    title->setObjectName("editInPlaceTitle");
    m_inPlaceToolbar->addWidget(title);
    m_inPlaceVisibility = new QComboBox(this);
    m_inPlaceVisibility->setObjectName("editInPlaceVisibility");
    m_inPlaceVisibility->addItems({"Show surroundings", "Transparent surroundings",
                                   "Hide surroundings", "Isolate active part",
                                   "Show referenced components"});
    m_inPlaceToolbar->addWidget(m_inPlaceVisibility);
    auto* finish = m_inPlaceToolbar->addAction("Finish Editing");
    finish->setObjectName("actionFinishEditInPlace");
    connect(m_inPlaceVisibility, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::applyInPlaceContextVisibility);
    connect(finish, &QAction::triggered, this, [this]
            {
                const auto result = m_commands.execute({"part.finish_edit_in_place"});
                if (result.state != application::CommandState::Completed &&
                    !result.message.empty())
                    statusBar()->showMessage(QString::fromStdString(result.message), 5000);
            });
    setWindowTitle(QString("Editing %1 — TheCAD").arg(componentName));
    statusBar()->showMessage("Edit In Place · part geometry is saved to its independent .thepart file.");
    applyInPlaceContextVisibility(0);
}

void MainWindow::applyInPlaceContextVisibility(int mode)
{
    if (!m_viewport)
        return;
    if (mode == 2 || mode == 3)
    {
        m_viewport->setContextShapes({});
        return;
    }
    const auto& shapes = mode == 4 ? m_inPlaceReferenced : m_inPlaceSurroundings;
    m_viewport->setContextShapes(shapes, mode == 1 ? 0.86 : 0.72);
}

bool MainWindow::finishInPlaceEditing()
{
    if (!m_inPlaceFinished)
        return false;
    if (!saveDocument())
        return false;
    close();
    return true;
}

QString MainWindow::chooseOpenPath()
{
    return QFileDialog::getOpenFileName(this, "Open TheCAD document", m_session.path(),
        "TheCAD Documents (*.thepart *.theasmb *.tcpart);;TheCAD Part (*.thepart);;TheCAD Assembly (*.theasmb);;Legacy TheCAD Part (*.tcpart)");
}

QString MainWindow::chooseSavePath(bool allowExport)
{
    return NativeFileDialogs::chooseSave(this,m_session.path(),allowExport);
}

MainWindow::UnsavedChoice MainWindow::confirmUnsaved()
{
    const auto answer = QMessageBox::warning(this, "Unsaved part", "Save changes to this part before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return UnsavedChoice::Save;
    return answer == QMessageBox::Discard ? UnsavedChoice::Discard : UnsavedChoice::Cancel;
}

void MainWindow::reportError(const QString& message) { QMessageBox::critical(this, "TheCAD", message); }

bool MainWindow::chooseFeatureParameters(document::Feature& feature)
{
    QDialog dialog(this);
    const bool cut = feature.kind == document::FeatureKind::Cut;
    dialog.setWindowTitle(cut ? "Extruded Cut" : feature.kind == document::FeatureKind::Box ? "Box parameters" : "Extrude");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    if (feature.kind == document::FeatureKind::Box) {
        auto* width = number(form, "Width", feature.box.width, true);
        auto* depth = number(form, "Depth", feature.box.depth, true);
        auto* height = number(form, "Height", feature.box.height, true);
        dialogButtons(dialog, layout);
        if (dialog.exec() != QDialog::Accepted) return false;
        feature.box = {width->value(), depth->value(), height->value()};
        return true;
    }
    auto* length = number(form, cut ? "Depth" : "Length", feature.length, true);
    length->setObjectName("featureLength");
    auto* direction = new QComboBox;
    direction->setObjectName("featureDirection");
    direction->addItems({"Positive sketch normal", "Negative sketch normal"});
    direction->setCurrentIndex(feature.direction == modeling::Direction::Positive ? 0 : 1);
    form->addRow("Direction", direction);
    auto* through = new QCheckBox("Through all in selected direction", &dialog);
    through->setObjectName("featureThroughAll");
    if (cut) {
        form->addRow(through);
        through->setChecked(feature.throughAll);
        length->setEnabled(!feature.throughAll);
        connect(through, &QCheckBox::toggled, length, [length](bool checked) { length->setEnabled(!checked); });
    }
    dialogButtons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted) return false;
    feature.length = length->value();
    feature.direction = direction->currentIndex() == 0 ? modeling::Direction::Positive : modeling::Direction::Negative;
    feature.throughAll = cut && through->isChecked();
    return true;
}

bool MainWindow::chooseEntityParameters(modeling::SketchEntity& entity)
{
    if(entity.kind==modeling::EntityKind::Spline)return editSplineParameters(entity,this);
    QDialog dialog(this);
    dialog.setWindowTitle("Edit sketch geometry");
    dialog.setObjectName("sketchEntityDialog");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    const bool circle = entity.kind == modeling::EntityKind::Circle;
    auto* x1 = number(form, circle ? "Center U" : "First U", entity.first.x);
    auto* y1 = number(form, circle ? "Center V" : "First V", entity.first.y);
    x1->setObjectName("entityFirstX");
    y1->setObjectName("entityFirstY");
    QDoubleSpinBox *x2 = nullptr, *y2 = nullptr, *radius = nullptr;
    if (modeling::isCircular(entity.kind)) {
        radius = number(form, "Radius", entity.radius, true);
        radius->setObjectName("entityRadius");
    }
    if(!circle && entity.kind!=modeling::EntityKind::Point) {
        x2 = number(form, "Second U", entity.second.x);
        y2 = number(form, "Second V", entity.second.y);
        x2->setObjectName("entitySecondX");
        y2->setObjectName("entitySecondY");
    }
    dialogButtons(dialog, layout);
    dialog.findChild<QDialogButtonBox*>()->setObjectName("entityButtons");
    if (dialog.exec() != QDialog::Accepted) return false;
    entity.first = {x1->value(), y1->value()};
    if (radius) entity.radius = radius->value();
    if(x2)entity.second = {x2->value(), y2->value()};
    return true;
}

bool MainWindow::maybeSave()
{
    if (!isWindowModified()) return true;
    switch (confirmUnsaved()) {
    case UnsavedChoice::Save: return saveDocument();
    case UnsavedChoice::Discard: return true;
    case UnsavedChoice::Cancel: return false;
    }
    return false;
}

void MainWindow::newDocument()
{
    if (maybeSave() && replaceDocument(document::PartDocument::createEmpty(), {})) setWindowModified(true);
}

void MainWindow::openDocument()
{
    const QString path = chooseOpenPath();
    if (path.isEmpty()) return;
    if(path.endsWith(".theasmb",Qt::CaseInsensitive)){showAssemblyWindow(path);return;}
    (void)openPartFileImpl(path,true);
}

bool MainWindow::openPartFile(const QString& path){return openPartFileImpl(path,false);}

bool MainWindow::openPartFileImpl(const QString& path,bool promptForEmptyDocument)
{
    if(path.isEmpty())return false;
    if(featureDraftActive() || m_selectingPlane || m_constraints || (m_smartDimension && m_smartDimension->editor)){reportError("Confirm or cancel the active operation before opening another part.");return false;}
    try {
        auto candidate = document::PartDocument::load(path);
        // A file supplied at launch replaces only the pristine empty startup
        // document without prompting. Existing work still uses normal Save/Cancel.
        const bool pristine=m_session.path().isEmpty()&&m_session.document().features().empty()&&!m_session.editingSketch();
        if((promptForEmptyDocument || !pristine) && !maybeSave())return false;
        candidate = document::PartDocument::load(path);
        if(!replaceDocument(std::move(candidate), QFileInfo(path).absoluteFilePath()))return false;
        setWindowModified(false);
        application::recordRecentDocument(path);
        return true;
    } catch (const std::exception& error) { reportError(QString("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()),QString::fromUtf8(error.what()))); }
    return false;
}

bool MainWindow::saveDocument()
{
    if (!finishSketch()) return false;
    return m_session.path().isEmpty() || legacyPath(m_session.path()) ? saveDocumentAs(false) : saveToPath(m_session.path());
}

bool MainWindow::saveDocumentAs(bool allowExport)
{
    if(featureDraftActive()||m_selectingPlane){reportError("Confirm or cancel the active feature before Save As.");return false;}
    if (!finishSketch()) return false;
    QString path = chooseSavePath(allowExport);
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += ".thepart";
    if(allowExport&&exchange::ShapeExporter::formatForPath(path)){
        try{
            exchange::ShapeExporter::write(m_session.document().shape(),path);
            statusBar()->showMessage("Exported "+QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()),4000);
            return true;
        }catch(const std::exception& error){reportError(QString("Cannot export %1:\n%2").arg(QDir::toNativeSeparators(path),QString::fromUtf8(error.what())));return false;}
    }
    if (QFileInfo(path).suffix().compare("thepart", Qt::CaseInsensitive) != 0) {
        reportError(allowExport?"Save as .thepart, .stl, .step or .stp. Legacy .tcpart files are read-only inputs; assemblies and drawings are not implemented.":"Save the editable part as .thepart. Legacy .tcpart files are read-only inputs.");
        return false;
    }
    return saveToPath(path);
}

bool MainWindow::saveToPath(const QString& path)
{
    if (!sourceRevisionMatches(path))
        return false;
    try {
        m_session.document().save(path);
        m_session.setPath(QFileInfo(path).absoluteFilePath());
        rememberSourceRevision(m_session.path());
        setWindowModified(false);m_session.markSaved();
        application::recordRecentDocument(path);
        updateDocumentUi();
        notifyOpenAssemblyWindowsPartSaved(m_session.path());
        statusBar()->showMessage("Saved " + QDir::toNativeSeparators(m_session.path()), 4000);
        return true;
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); return false; }
}

bool MainWindow::replaceDocument(document::PartDocument candidate, const QString& path)
{
    if (!m_viewport->setShape(candidate.shape())) {
        reportError("The part could not be displayed. The current document has been retained.");
        return false;
    }
    const bool resumeMeasure=m_viewport->measuring();
    setMeasureActive(false);
    m_viewport->clearSelection();
    m_selectingPlane = false;
    m_selectedPlane.reset();
    m_viewport->cancelPlaneSelection();
    m_viewport->setSelectedReferencePlane({});
    m_viewport->endSketch();
    m_session.editingSketch().reset();
    m_session.editingSketchId().clear();
    m_session.selectedFeatureId().clear();
    m_session.document() = std::move(candidate);
    m_mode = WorkspaceMode::Part;
    m_selectedFace=0;
    m_session.setPath(path);
    rememberSourceRevision(path);
    m_startupSketchAvailable = false;
    m_session.clearHistory();
    updateDocumentUi();
    m_viewport->showReferencePlanes(m_session.document().features().empty());
    m_viewport->resetView(); // Opening a different document frames it once.
    if(resumeMeasure && m_measure->isEnabled())setMeasureActive(true);
    statusBar()->showMessage("Orbit: middle drag | Pan: Ctrl + middle | Zoom: wheel | Normal To: Ctrl+8 | Fit: F");
    return true;
}

bool MainWindow::sourceRevisionMatches(const QString& requestedPath)
{
    if (m_session.path().isEmpty() ||
        normalizedPath(requestedPath) != normalizedPath(m_session.path()))
        return true;

    bool readable = false;
    const auto current = fileDigest(m_session.path(), &readable);
    if (readable && m_hasSourceFileDigest && current == m_sourceFileDigest)
        return true;

    if (readable)
        reportError(m_inPlaceFinished
                        ? "This part changed on disk after it was opened. Reload the latest version "
                          "before saving, so another edit is not overwritten. Save As creates a "
                          "separate part and does not retarget the assembly component."
                        : "This part changed on disk after it was opened. Reload the latest version "
                          "before saving, so another edit is not overwritten.");
    else
        reportError(m_inPlaceFinished
                        ? "The linked source part file is no longer available. Save As creates a "
                          "separate part and does not retarget the assembly component."
                        : "The source part file is no longer available. Use Save As to preserve your changes.");
    return false;
}

void MainWindow::rememberSourceRevision(const QString& path)
{
    bool readable = false;
    m_sourceFileDigest = path.isEmpty() ? QByteArray{} : fileDigest(path, &readable);
    m_hasSourceFileDigest = readable;
}

bool MainWindow::applyModel(document::PartDocument candidate, const std::string& selectedId,bool record)
{
    auto previous=editState();
    if (!m_viewport->setShape(candidate.shape())) {
        reportError("The regenerated geometry could not be displayed. The previous part was retained.");
        return false;
    }
    setMeasureActive(false);
    if(record)rememberChange(std::move(previous));
    m_startupSketchAvailable = false;
    m_session.document() = std::move(candidate);
    m_session.selectedFeatureId() = selectedId;
    setWindowModified(true);
    updateDocumentUi();
    return true;
}

const document::Feature* MainWindow::selectedFeature() const
{
    for (const auto& feature : m_session.document().features()) if (feature.id == m_session.selectedFeatureId()) return &feature;
    return nullptr;
}

void MainWindow::selectFeature(const std::string& id)
{
    m_session.selectedFeatureId() = id;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString().toStdString() == id) {
            m_tree->setCurrentItem(*it,0,QItemSelectionModel::ClearAndSelect);
            m_tree->scrollToItem(*it);
            return;
        }
    }
}

void MainWindow::selectionChanged()
{
    // Measure accepts tree clicks, including a second click on the same row.
    // Selection notifications also occur during tree rebuilds and deselection.
    if (m_measure && m_measure->isChecked()) return;
    if(m_solidFeature){const auto* item=m_tree->currentItem();if(item)m_solidFeature->pickTree(item->data(0,Qt::UserRole).toString());return;}
    if(m_referenceDraft){const auto* item=m_tree->currentItem();if(item)referenceGeometryTreePicked(item->data(0,Qt::UserRole).toString());return;}
    if (m_operation && m_operation->pickingSketch) {
        if (const auto* item=m_tree->currentItem()) {
            const auto index=m_operation->sketches->findData(item->data(0,Qt::UserRole));
            if (index>0) { m_operation->sketches->setCurrentIndex(index); focusOperationDepth(); }
        }
        return;
    }
    if (m_operation && !m_operation->pickOptions.empty()) {
        if (const auto* item = m_tree->currentItem()) {
            const auto id = item->data(0, Qt::UserRole).toString();
            using Source = modeling::ReferenceSource;
            for (const int index : m_operation->pickOptions) {
                const auto& ref = m_operation->options->references()[std::size_t(index)].reference;
                const bool originPlane = ref.source == Source::BasePlane &&
                    id == (ref.index == 0 ? "origin:XY" : ref.index == 1 ? "origin:XZ" : "origin:YZ");
                if (originPlane || (ref.source == Source::Datum && id.toStdString() == ref.featureId) ||
                    (ref.source == Source::Origin && id == "origin:")) {
                    m_operation->options->picked(index);
                    return;
                }
            }
        }
        m_operation->panel->setStatus("Select an eligible datum in the tree or pick a reference in the viewport.");
        return;
    }
    m_viewport->beginTreeSelection();
    m_selectedFace=0;
    m_viewport->setSelectedSolidFace(0);
    m_viewport->setSelectedDatum({});
    const auto* item = m_tree->currentItem();
    const QString selection = item ? item->data(0, Qt::UserRole).toString() : QString{};
    if (!m_session.editingSketch() && !m_operation && selection.startsWith("origin:")) {
        const QString suffix = selection.mid(7);
        if (suffix == "XY") m_selectedPlane = modeling::SketchPlane::XY;
        else if (suffix == "XZ") m_selectedPlane = modeling::SketchPlane::XZ;
        else if (suffix == "YZ") m_selectedPlane = modeling::SketchPlane::YZ;
        else m_selectedPlane.reset();
        m_session.selectedFeatureId().clear();
        m_viewport->setSelectedReferencePlane(m_selectedPlane);
        statusBar()->showMessage(m_selectedPlane
            ? QString("%1 selected. Click Sketch to begin on this reference plane.").arg(modeling::referencePlaneName(*m_selectedPlane))
            : QString("Select Front, Top or Right in the viewport, then click Sketch."));
    } else {
        m_session.selectedFeatureId() = selection.toStdString();
        m_selectedPlane.reset();
        m_viewport->setSelectedReferencePlane({});
        if (!m_session.editingSketch()) {
            const auto* feature = selectedFeature();
            m_viewport->endSketch();
            m_viewport->showReferencePlanes(false);
            if (feature && feature->kind == document::FeatureKind::Sketch) m_viewport->setSketch(feature->sketch);
            if(feature&&feature->kind==document::FeatureKind::ReferenceGeometry)m_viewport->setSelectedDatum(feature->id);
            if (feature && document::isEdgeFeature(feature->kind))
                m_viewport->setTreeFeatureFaces(m_session.document().edgeTreatmentFaces(feature->id, true));
            else m_viewport->setTreeBodySelected(feature&&document::isSolidFeature(feature->kind));
        }
    }
    if(m_tree->selectedItems().size()>1){std::vector<modeling::SelectionRef> refs;for(const auto* node:m_tree->selectedItems()){const auto id=node->data(0,Qt::UserRole).toString();using K=modeling::SelectionKind;if(id=="origin:XY"||id=="origin:XZ"||id=="origin:YZ")refs.push_back({K::OriginPlane,{},{},modeling::PointRole::First,id=="origin:XY"?0:id=="origin:XZ"?1:2});else for(const auto& f:m_session.document().features())if(f.id==id.toStdString()&&f.kind==document::FeatureKind::ReferenceGeometry)refs.push_back({f.datum.kind==modeling::DatumKind::Plane?K::ReferencePlane:f.datum.kind==modeling::DatumKind::Axis?K::ReferenceAxis:K::ReferencePoint,f.id});}m_viewport->setTreeReferenceSelection(refs);}
    updateEntityList();
    updateActions();
    refreshMeasurement();
}
void MainWindow::updateDocumentUi()
{
    std::string bodyId;for(const auto& feature:m_session.document().features())if(document::isSolidFeature(feature.kind))bodyId=feature.id;
    m_viewport->setSelectionFeatureIds(bodyId,m_session.editingSketchId());
    if (m_session.editingSketch())
        m_viewport->setSketchBoundaryReferences(m_session.document().sketchBoundaryReferences(*m_session.editingSketch()));
    refreshReferenceGeometry();
    const QString name = m_session.path().isEmpty() ? "Untitled.thepart" : QFileInfo(m_session.path()).fileName();
    setWindowTitle(name + "[*] - TheCAD");
    setWindowFilePath(m_session.path());
    m_dimensions->setText(QString("%1 features | mm%2").arg(m_session.document().features().size()).arg(m_mode==WorkspaceMode::FeatureDraft ? " | Feature preview" : m_mode==WorkspaceMode::EditingSketch ? " | Editing sketch" : ""));
    m_treePresenter->refresh(m_session.document(),name,m_session.selectedFeatureId(),m_selectedPlane);
    updateEntityList();
    updateActions();
}

void MainWindow::updateEntityList()
{
    const QSignalBlocker blocker(m_entities);
    const int selectedRow = m_entities->currentRow();
    m_entities->clear();
    const auto* feature = selectedFeature();
    const modeling::Sketch* sketch = m_session.editingSketch() ? &*m_session.editingSketch()
        : feature && feature->kind == document::FeatureKind::Sketch ? &feature->sketch : nullptr;
    if (sketch) for (const auto& entity : sketch->entities) {
        auto* item = new QListWidgetItem(entityText(entity), m_entities);
        item->setData(Qt::UserRole, QString::fromStdString(entity.id));
        item->setToolTip(QString::fromStdString(entity.id));
    }
    if (m_entities->count()) m_entities->setCurrentRow(std::min(std::max(selectedRow, 0), m_entities->count() - 1));
    updateEntityProperties();
}

void MainWindow::updateSketchToolActions()
{
    const bool editing=m_mode==WorkspaceMode::EditingSketch && !m_constraints && !m_operation && !m_sketchModifier;
    const auto tool=m_viewport->sketchTool();
    findChild<QAction*>("actionSmartDimension")->setChecked(editing && m_viewport->smartDimensionActive());
    m_selectEntity->setChecked(editing && !tool && !m_viewport->smartDimensionActive());
    m_line->setChecked(editing && tool==modeling::EntityKind::Line && !m_viewport->constructionTool());
    m_centerline->setChecked(editing && tool==modeling::EntityKind::Line && m_viewport->constructionTool());
    m_point->setChecked(editing && tool==modeling::EntityKind::Point);m_polygon->setChecked(editing && tool==modeling::EntityKind::Polygon);
    m_rectangle->setChecked(editing && tool==modeling::EntityKind::Rectangle);
    m_circle->setChecked(editing && tool==modeling::EntityKind::Circle);
    m_arc->setChecked(editing && tool==modeling::EntityKind::Arc);
    m_spline->setChecked(editing && tool==modeling::EntityKind::Spline);
    m_straightSlot->setChecked(editing && tool==modeling::EntityKind::StraightSlot);
}

void MainWindow::updateActions()
{
    updateSketchToolActions();updateHistoryActions();updateMeasureAction();
    const bool operation = featureDraftActive();
    const bool editing = m_mode == WorkspaceMode::EditingSketch && !m_constraints;
    findChild<QAction*>("actionSmartDimension")->setEnabled(editing && !operation);
    findChild<QAction*>("actionDisplayDeleteRelations")->setEnabled(editing && !operation);
    const bool selecting = m_selectingPlane;
    if (m_deleteFeatures)
        m_deleteFeatures->setEnabled(!m_measure->isChecked() && !m_session.editingSketch() && !operation &&
                                     !selecting && !selectedFeatureIds().empty());
    const std::array modifierNames{"actionConvertEntities", "actionMirrorEntities",
                                   "actionLinearSketchPattern", "actionCircularSketchPattern",
                                   "actionTrimEntities", "actionExtendEntities", "actionSplitEntities",
                                   "actionOffsetEntities"};
    for (std::size_t i = 0; i < modifierNames.size(); ++i)
    {
        auto* command = findChild<QAction*>(modifierNames[i]);
        const bool active = m_sketchModifier && int(m_sketchModifier->kind) == int(i);
        command->setCheckable(true);
        command->setChecked(active);
        command->setEnabled(active || (editing && !operation && !(m_smartDimension && m_smartDimension->editor) &&
                                      (i != 0 || !m_session.document().shape().IsNull())));
    }
    for(auto* reference:{m_referencePoint,m_referenceAxis,m_referencePlane})reference->setEnabled(!operation&&!m_session.editingSketch()&&!selecting);
    m_tabs->setEnabled(!operation);
    findChild<QAction*>("actionCancelFeature")->setEnabled(operation);
    m_tree->setEnabled((!operation || m_solidFeature || m_referenceDraft) && (!editing || m_measure->isChecked()) && !selecting);
    for (const auto* name : {"actionNew", "actionOpen", "actionSave", "actionSaveAs"}) {
        if (auto* action = findChild<QAction*>(name)) action->setEnabled(!operation && !selecting && !m_constraints);
    }
    bool sketchSupport=true;const auto selection=m_viewport->selection();
    if(selection.size()>1)sketchSupport=false;
    else if(selection.size()==1){using K=modeling::SelectionKind;const auto kind=selection.front().kind;sketchSupport=kind==K::Face||kind==K::ReferencePlane||kind==K::OriginPlane;
        if(kind==K::Face)try{(void)m_session.document().sketchOnFace(selection.front().index);}catch(const std::exception&){sketchSupport=false;}}
    m_createSketch->setEnabled(!m_session.editingSketch() && !operation && !selecting && sketchSupport);
    m_selectEntity->setEnabled(editing && !operation);
    m_line->setEnabled(editing && !operation &&
        (m_commands.activeCommandId() == "sketch.line" || m_commands.availability("sketch.line").available));
    m_rectangle->setEnabled(editing && !operation);
    m_circle->setEnabled(editing && !operation);
    m_arc->setEnabled(editing && !operation);
    m_spline->setEnabled(editing && !operation);
    m_straightSlot->setEnabled(editing && !operation);
    for(auto* tool:{m_point,m_centerline,m_polygon})tool->setEnabled(editing && !operation);
    updateConstructionControls();
    m_finishSketch->setEnabled(editing && !operation);
    const auto* selected = selectedFeature();
    m_editFeature->setEnabled(!m_session.editingSketch() && !operation && !selecting && selected);
    m_editEntity->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    m_deleteEntity->setEnabled(!m_measure->isChecked() && editing && !operation && (m_entities->currentRow()>=0 || !m_viewport->selectedSketchDimensions().empty() || !m_viewport->selectedSketchEntities().empty()));
    m_applyProperties->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    updateEntityProperties();
    bool profile = editing && !operation;
    if (!editing && !operation && !selecting) {
        for (const auto& feature : m_session.document().features()) {
            if (feature.kind != document::FeatureKind::Sketch) continue;
            try { profile = !modeling::closedContours(feature.sketch).empty(); }
            catch (const std::exception&) { }
            if (profile) break;
        }
    }
    const bool startProfileFeature = profile && !m_constraints;
    m_extrude->setEnabled(startProfileFeature);
    m_cut->setEnabled(startProfileFeature && !m_session.document().shape().IsNull());
    m_revolve->setEnabled(startProfileFeature);
    m_revolvedCut->setEnabled(m_revolve->isEnabled()&&!m_session.document().shape().IsNull());
    m_sweep->setEnabled(m_revolve->isEnabled());
    m_sweptCut->setEnabled(m_revolvedCut->isEnabled());
    const bool edges=!operation&&!selecting&&!m_constraints&&!m_session.document().shape().IsNull();
    m_fillet->setEnabled(edges);m_chamfer->setEnabled(edges);
    m_linearPattern->setEnabled(edges);m_circularPattern->setEnabled(edges);m_mirror->setEnabled(edges);
}
void MainWindow::createSketch()
{
    if (m_session.editingSketch() || featureDraftActive() || m_selectingPlane) return;
    if(const auto* feature=selectedFeature();feature&&feature->kind==document::FeatureKind::ReferenceGeometry&&feature->datum.kind==modeling::DatumKind::Plane){createSketchOnReferencePlane(feature->id);return;}
    if(m_selectedFace) {createSketchOnFace(m_selectedFace);return;}
    if (m_selectedPlane) {
        createSketchOnPlane(*m_selectedPlane);
        return;
    }
    m_selectingPlane = true;
    m_mode = WorkspaceMode::SelectingPlane;
    m_viewport->beginPlaneSelection();
    updateActions();
    statusBar()->showMessage("Select a planar solid face or Front, Top or Right in the viewport. Drag to orbit; Esc cancels.");
}

void MainWindow::createSketchOnFace(int index)
{
    try {
        auto sketch=m_session.document().sketchOnFace(index);auto candidate=m_session.document();
        const auto id=candidate.addSketch(sketch);m_selectingPlane=false;m_selectedFace=0;
        if(!applyModel(std::move(candidate),id))return;
        startSketchEditing(*selectedFeature());
    }catch(const std::exception& error){reportError(QString::fromUtf8(error.what()));}
}

void MainWindow::createSketchOnPlane(modeling::SketchPlane plane)
{
    modeling::Sketch sketch;
    sketch.plane = plane;
    try {
        auto candidate = m_session.document();
        const auto id = candidate.addSketch(sketch);
        m_selectingPlane = false;
        m_selectedPlane.reset();
        if (!applyModel(std::move(candidate), id)) return;
        startSketchEditing(*selectedFeature());
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::referencePlaneSelected(modeling::SketchPlane plane, bool directClick)
{
    m_selectedFace=0;
    m_viewport->setSelectedSolidFace(0);
    if (m_session.editingSketch() || featureDraftActive()) return;
    if (m_selectingPlane || (directClick && m_startupSketchAvailable)) {
        createSketchOnPlane(plane);
        return;
    }
    m_selectedPlane = plane;
    m_viewport->setSelectedReferencePlane(plane);
    m_session.selectedFeatureId().clear();
    // Rebuilding the tree under a signal blocker preserves the current camera.
    updateDocumentUi();
    statusBar()->showMessage(QString("%1 selected. Click Sketch to begin on this plane.").arg(modeling::referencePlaneName(plane)));
}

void MainWindow::planeSelectionCanceled()
{
    if (!m_selectingPlane) return;
    m_selectingPlane = false;
    m_mode = WorkspaceMode::Part;
    m_selectedPlane.reset();
    m_viewport->setSelectedReferencePlane({});
    updateActions();
    statusBar()->showMessage("Sketch plane selection canceled. The part is unchanged.");
}
void MainWindow::startSketchEditing(const document::Feature& feature)
{
    m_selectingPlane = false;
    m_selectedPlane.reset();
    m_viewport->setSelectedReferencePlane({});
    m_mode = WorkspaceMode::EditingSketch;
    m_constraintPoint.reset();
    m_session.editingSketch() = feature.sketch;
    m_session.editingSketchId() = feature.id;
    m_session.selectedFeatureId() = feature.id;
    m_viewport->beginSketch(*m_session.editingSketch());
    m_viewport->setSketchTool(modeling::EntityKind::Line);
    m_tabs->setCurrentIndex(1);
    updateDocumentUi();
    statusBar()->showMessage("Line: click to start, click successive endpoints | Esc ends chain / Select picks geometry | Middle/right drag pans");
}

bool MainWindow::finishSketch()
{
    if(m_constraints || m_sketchModifier)return false;
    closeSmartDimension();
    if (!m_session.editingSketch()) return true;
    try {
        auto candidate = m_session.document();
        auto feature = *std::find_if(candidate.features().begin(), candidate.features().end(),
                                    [this](const auto& item) { return item.id == m_session.editingSketchId(); });
        feature.sketch = *m_session.editingSketch();
        candidate.replaceFeature(feature);
        const auto id = m_session.editingSketchId();
        if (!applyModel(std::move(candidate), id,false)) return false;
        m_session.editingSketch().reset();
        m_session.editingSketchId().clear();
        m_mode = WorkspaceMode::Part;
        m_viewport->endSketch();
        m_viewport->setSketch(feature.sketch);
        m_tabs->setCurrentIndex(0);
        updateDocumentUi();
        statusBar()->showMessage("Sketch finished. Select a closed profile to Extrude or Extruded Cut.");
        return true;
    } catch (const std::exception& error) {
        reportError(QString("Sketch changes could not regenerate the part: %1\nThe previous model is retained; correct the sketch and Finish again.").arg(error.what()));
        return false;
    }
}

void MainWindow::updateEntityProperties()
{
    const auto* feature = selectedFeature();
    const modeling::Sketch* sketch = m_session.editingSketch() ? &*m_session.editingSketch()
        : feature && feature->kind == document::FeatureKind::Sketch ? &feature->sketch : nullptr;
    const int row = m_entities->currentRow();
    const bool valid = sketch && row >= 0 && std::size_t(row) < sketch->entities.size();
    m_propertyPanel->setVisible(valid);
    if (!valid) return;
    const auto& entity = sketch->entities[std::size_t(row)];
    const bool circle = entity.kind == modeling::EntityKind::Circle;
    const bool editing = m_mode == WorkspaceMode::EditingSketch;
    m_entitySummary->setText(QString("%1\n%2 supported relation(s)")
        .arg(circle ? "Circle" : entity.kind==modeling::EntityKind::Spline?"Spline":entity.kind==modeling::EntityKind::Arc?"Arc":entity.kind==modeling::EntityKind::Point?"Point":entity.kind == modeling::EntityKind::Line ? (entity.construction?"Centerline":"Line") : "Legacy rectangle")
        .arg(std::count_if(sketch->relations.begin(), sketch->relations.end(), [&entity](const auto& relation) {
            return relation.first.entityId == entity.id || relation.second.entityId == entity.id;
        })));
    QStringList associated;
    for(const auto& r:sketch->relations)if(r.first.entityId==entity.id || r.second.entityId==entity.id)associated<<QString::fromStdString(r.id);
    for(const auto& d:sketch->dimensions)if(d.first.entityId==entity.id || d.second.entityId==entity.id)associated<<QString::fromStdString(d.id);
    const auto diagnostic=modeling::solvePlanarSketch(*sketch).diagnostics;
    m_entitySummary->setText(m_entitySummary->text()+"\n"+QString::fromStdString(diagnostic.message)+"\nConstraints: "+associated.join(", "));
    m_propertyFirstX->setValue(entity.first.x);
    m_propertyFirstY->setValue(entity.first.y);
    m_propertySecondX->setValue(entity.second.x);
    m_propertySecondY->setValue(entity.second.y);
    m_propertyRadius->setValue(entity.radius);
    if (auto* form = m_propertyPanel->findChild<QFormLayout*>()) {
        form->setRowVisible(m_propertySecondX, !circle && entity.kind!=modeling::EntityKind::Point);
        form->setRowVisible(m_propertySecondY, !circle && entity.kind!=modeling::EntityKind::Point);
        form->setRowVisible(m_propertyRadius, modeling::isCircular(entity.kind));
    }
    m_propertyFirstX->setEnabled(editing);
    m_propertyFirstY->setEnabled(editing);
    m_propertySecondX->setEnabled(editing && !circle && entity.kind!=modeling::EntityKind::Point);
    m_propertySecondY->setEnabled(editing && !circle && entity.kind!=modeling::EntityKind::Point);
    m_propertyRadius->setEnabled(editing && modeling::isCircular(entity.kind));
    m_applyProperties->setEnabled(editing && !m_operation);
}

void MainWindow::applySketchEntityEdit(const modeling::SketchEntity& entity)
{
    if (!m_session.editingSketch()) return;
    try {
        auto candidate = modeling::editSketchEntity(*m_session.editingSketch(), entity);
        (void)modeling::sketchShape(candidate);
        acceptConstrainedSketch(candidate);
        m_viewport->setSketch(*m_session.editingSketch());
        setWindowModified(true);
        updateEntityList();
        updateActions();
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::editSketchEntity()
{
    if (!m_session.editingSketch() || m_entities->currentRow() < 0) return;
    auto entity = m_session.editingSketch()->entities.at(std::size_t(m_entities->currentRow()));
    if (chooseEntityParameters(entity)) applySketchEntityEdit(entity);
}

void MainWindow::deleteSketchEntity()
{
    if(!m_session.editingSketch() || m_operation || m_constraints || m_sketchModifier || (m_smartDimension && m_smartDimension->editor))return;
    if(!m_viewport->selectedSketchRelation().empty()){deleteRelation(m_viewport->selectedSketchRelation());return;}
    auto entities=m_viewport->selectedSketchEntities();const auto dimensions=m_viewport->selectedSketchDimensions();
    if(entities.empty() && dimensions.empty() && m_entities->currentRow()>=0)
        entities.push_back(m_session.editingSketch()->entities.at(std::size_t(m_entities->currentRow())).id);
    if(entities.empty() && dimensions.empty())return;
    try {
        auto candidate=*m_session.editingSketch();
        candidate.dimensions.erase(std::remove_if(candidate.dimensions.begin(),candidate.dimensions.end(),[&](const auto& d){
            return std::find(dimensions.begin(),dimensions.end(),d.id)!=dimensions.end();
        }),candidate.dimensions.end());
        for(const auto& id:entities)candidate=modeling::removeSketchEntity(candidate,id);
        modeling::validateSketchRelations(candidate);(void)modeling::sketchShape(candidate);
        auto previous=editState();m_session.editingSketch()=std::move(candidate);rememberSketchChange(std::move(previous));
        closeSmartDimension();m_viewport->selectSketchTool();m_viewport->selectSketchEntity({});
        m_constraintPoint.reset();m_viewport->setSketch(*m_session.editingSketch());
        setWindowModified(true);updateEntityList();updateActions();
        statusBar()->showMessage("Deleted selected sketch geometry/dimensions. Ctrl+Z restores them.",3000);
    }catch(const std::exception& error){reportError(QString::fromUtf8(error.what()));}
}
void MainWindow::editSelectedFeature()
{
    const auto* selected = selectedFeature();
    if (!selected || m_session.editingSketch()) return;
    if(document::isRevolveFeature(selected->kind)||document::isSweepFeature(selected->kind)||
       document::isEdgeFeature(selected->kind)||
       document::isPatternFeature(selected->kind)){startSolidFeature(selected->kind,selected);return;}
    if (selected->kind == document::FeatureKind::Sketch) { startSketchEditing(*selected); return; }
    if(selected->kind==document::FeatureKind::ReferenceGeometry){startReferenceGeometry(selected->datum.kind,selected);return;}
    auto feature = *selected;
    if (feature.kind == document::FeatureKind::Extrude || feature.kind == document::FeatureKind::Cut) {
        startOperation(feature, true);
        return;
    }
    if (!chooseFeatureParameters(feature)) return;
    try {
        auto candidate = m_session.document();
        candidate.replaceFeature(feature);
        applyModel(std::move(candidate), feature.id);
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::extrude(bool cut)
{
    if (featureDraftActive() || m_selectingPlane) return;
    document::Feature feature;
    feature.kind = cut ? document::FeatureKind::Cut : document::FeatureKind::Extrude;
    const auto* selected = selectedFeature();
    if (m_session.editingSketch()) feature.sketchId = m_session.editingSketchId();
    else if (selected && selected->kind == document::FeatureKind::Sketch) feature.sketchId = selected->id;
    startOperation(feature, false);
}

void MainWindow::startOperation(document::Feature feature, bool editing)
{
    if(featureDraftActive() || m_selectingPlane || m_constraints) return;
    closeSmartDimension();
    m_selectedFace=0;m_viewport->setSelectedSolidFace(0);
    auto sourceDocument=m_session.document();
    if(m_session.editingSketch()) {
        try {
            const auto& sketch = *m_session.editingSketch();
            if (modeling::closedContours(sketch).empty() && modeling::openExtrusionProfile(sketch).empty())
                throw std::runtime_error("The active sketch has no closed contour or supported open profile.");
            auto source=*std::find_if(sourceDocument.features().begin(),sourceDocument.features().end(),[this](const auto& item){return item.id==m_session.editingSketchId();});
            source.sketch=*m_session.editingSketch();sourceDocument.replaceFeature(source);
        } catch(const std::exception& error) {reportError(QString("Cannot start the feature: %1").arg(error.what()));return;}
        closeSmartDimension();
        m_viewport->selectSketchTool();
    }
    if (!editing && feature.kind == document::FeatureKind::Cut) {
        const auto sketch = std::find_if(sourceDocument.features().begin(), sourceDocument.features().end(),
                                         [&](const auto& item) { return item.id == feature.sketchId; });
        if (sketch != sourceDocument.features().end() && sketch->sketch.attachment)
            feature.direction = modeling::Direction::Negative;
    }
    const auto profile = std::find_if(sourceDocument.features().begin(), sourceDocument.features().end(),
        [&](const auto& item) { return item.kind == document::FeatureKind::Sketch && item.id == feature.sketchId; });
    m_viewport->prepareFeatureView(profile != sourceDocument.features().end()
        ? std::optional<gp_Ax3>(modeling::sketchAxes(profile->sketch)) : std::nullopt);
    m_operation=std::make_unique<OperationDraft>();auto& draft=*m_operation;
    draft.feature=std::move(feature);draft.editing=editing;
    draft.fromSketch=m_session.editingSketch().has_value();draft.modifiedBefore=isWindowModified();
    draft.sourceDocument=std::move(sourceDocument);m_mode=WorkspaceMode::FeatureDraft;
    draft.panel=new PropertyManagerPage(draft.feature.kind==document::FeatureKind::Cut ? "Cut-Extrude" : "Boss-Extrude",
        "Choose contours and set where the extrusion starts and ends.",m_managerStack);
    auto* settings=draft.panel->settings;draft.buttons=draft.panel->buttons;
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    draft.preview = draft.buttons->addButton("Preview", QDialogButtonBox::ActionRole);
    draft.preview->setObjectName("operationPreview");
    draft.preview->setCheckable(true);
    draft.preview->setChecked(true);
    draft.panel->useCompactFeatureHeader(draft.preview);
    const auto support = editing ? draft.sourceDocument->beforeFeature(draft.feature.id) : *draft.sourceDocument;
    draft.options = std::make_unique<ExtrusionOptionsPanel>(settings, draft.panel, draft.feature,
        support.referenceOptions(false, true), [this] { updateOperationPreview(); }, [this](const std::vector<int>& indexes) {
            auto& operation = *m_operation;
            operation.pickOptions = indexes;
            showOperationReferenceTree(!indexes.empty());
            std::vector<ReferencePickItem> items;
            for (const int index : indexes) {
                const auto& option = operation.options->references()[std::size_t(index)];
                items.push_back({option.reference, option.geometry, option.selectionShape});
            }
            m_viewport->setReferenceCandidates(items, {});
            if (items.empty()) updateOperationPreview();
            else {
                m_viewport->clearPreview();
                m_viewport->setExtrusionDepthGrips({});
                operation.grips.clear();
                operation.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
                operation.panel->setStatus("Pick a reference in the viewport or select a datum from the tree.");
                m_viewport->setFocus();
            }
        });
    draft.length = draft.options->depth();
    if (editing) m_viewport->setShape(support.shape());
    auto* selectionForm = draft.options->contourForm();
    auto* sketchBox = new PropertyReference;
    draft.sketches=sketchBox;draft.sketches->setObjectName("operationSketch");draft.sketches->setPlaceholderText("Select a sketch");
    draft.sketches->setAccessibleName("Profile sketch");
    draft.sketches->addItem("");
    sketchBox->pick = [this,sketchBox] {
        m_operation->options->stopPicking();
        m_operation->pickingSketch=true;
        sketchBox->setPicking(true);
        showOperationReferenceTree(true);
        m_operation->panel->setStatus("Select a sketch in the FeatureManager.");
    };
    int selectedIndex=-1;
    for(const auto& item:draft.sourceDocument->features()) {
        if(editing && item.id==draft.feature.id) break;
        if(item.kind!=document::FeatureKind::Sketch) continue;
        draft.sketches->addItem(QString::fromStdString(item.name),QString::fromStdString(item.id));
        if(item.id==draft.feature.sketchId) selectedIndex=draft.sketches->count()-1;
    }
    settings->insertWidget(0,draft.sketches);draft.sketches->setEnabled(!draft.fromSketch);
    draft.sketches->setVisible(selectedIndex < 0);
    auto* contourBox = new PropertySelectionList;
    contourBox->placeholder = "Select contours";
    contourBox->setAccessibleName("Selected Contours");
    contourBox->setToolTip("Click inside sketch regions to add or remove them. Delete removes the current selection.");
    contourBox->pick = [this] {
        m_operation->options->stopPicking();
        m_operation->panel->setStatus("Select contours in the viewport.");
    };
    draft.contours=contourBox;draft.contours->setObjectName("operationContour");
    draft.selectionCount=new QLabel("0 regions selected", draft.panel);draft.selectionCount->setObjectName("operationSelectionCount");
    draft.selectionCount->hide();
    auto* contourSymbol = new QLabel;
    contourSymbol->setPixmap(propertyIcon(PropertySymbol::Contour, palette().color(QPalette::WindowText)).pixmap(18,18));
    contourSymbol->setFixedWidth(22); contourSymbol->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    selectionForm->addRow(contourSymbol,draft.contours);
    settings->addStretch();
    connect(draft.sketches,&QComboBox::currentIndexChanged,this,[this]{operationSketchChanged();});
    connect(draft.contours,&QListWidget::itemSelectionChanged,this,[this]{
        m_operation->defaultContour = false;
        updateOperationPreview();
    });
    connect(draft.preview,&QPushButton::toggled,this,[this]{updateOperationPreview();});
    connect(draft.buttons,&QDialogButtonBox::rejected,this,[this]{
        if (m_commands.activeCommandId() == "feature.extrude" ||
            m_commands.activeCommandId() == "feature.extruded_cut")
            m_commands.cancel(m_commands.activeSessionId());
        else finishOperation(false);
    });
    connect(draft.buttons,&QDialogButtonBox::accepted,this,[this]{
        if (m_commands.activeCommandId() == "feature.extrude" ||
            m_commands.activeCommandId() == "feature.extruded_cut") {
            m_commands.confirm(m_commands.activeSessionId()); return;
        }
        updateOperationPreview();if(!m_operation || !m_operation->candidate)return;
        m_viewport->clearPreview();if(applyModel(*m_operation->candidate,m_operation->candidateFeatureId))finishOperation(true);
    });
    { const QSignalBlocker block(draft.sketches); draft.sketches->setCurrentIndex(selectedIndex); }
    operationSketchChanged();
    if(editing) {
        const auto references=draft.feature.contours.empty()?modeling::RegionReferences{draft.feature.contour}:draft.feature.contours;
        {const QSignalBlocker blocker(draft.contours);for(auto expected:references) {
            std::sort(expected.begin(),expected.end());
            for(int i=0;i<int(draft.availableContours.size());++i) {
                auto actual=draft.availableContours[std::size_t(i)].entityIds;std::sort(actual.begin(),actual.end());
                if(actual==expected){draft.contours->setCurrentRow(i,QItemSelectionModel::NoUpdate);draft.contours->item(i)->setSelected(true);break;}
            }
        }}
        if(draft.contours->selectedItems().size()!=int(references.size())) {
            const QSignalBlocker blocker(draft.contours);draft.contours->clearSelection();
            draft.panel->setStatus("A saved region is unavailable. Reselect the intended regions explicitly.",true);
        }
        updateOperationPreview();
        if(draft.sketches->currentIndex()<0)draft.panel->setStatus("The saved source sketch is unavailable. Select a replacement explicitly.",true);
        else if(draft.contours->selectedItems().isEmpty())draft.panel->setStatus("The saved contour is unavailable. Select a replacement explicitly.",true);
    }
    contourBox->refreshMembers();
    draft.options->setContoursExpanded(!draft.defaultContour || !draft.contours->selectedItems().isEmpty());
    m_managerStack->addWidget(draft.panel);m_managerStack->setCurrentWidget(draft.panel);
    m_historyDock->setWindowTitle("PropertyManager");updateDocumentUi();
    if (selectedIndex < 0) sketchBox->pick();
    else focusOperationDepth();
}

void MainWindow::showOperationReferenceTree(bool show)
{
    auto& operation = *m_operation;
    const QSignalBlocker block(m_tree);
    if (show && !operation.pickPanel) {
        operation.historySplitter = qobject_cast<QSplitter*>(m_tree->parentWidget());
        if (!operation.historySplitter) return;
        operation.historySizes = operation.historySplitter->sizes();
        operation.pickPanel = new QSplitter(Qt::Vertical, m_managerStack);
        m_managerStack->removeWidget(operation.panel);
        operation.pickPanel->addWidget(operation.panel);
        operation.pickPanel->addWidget(m_tree);
        operation.pickPanel->setStretchFactor(0, 3);
        operation.pickPanel->setStretchFactor(1, 1);
        operation.pickPanel->setCollapsible(0, false);
        m_tree->clearSelection();
        m_tree->setCurrentItem(nullptr);
        m_tree->show();
        m_managerStack->addWidget(operation.pickPanel);
        m_managerStack->setCurrentWidget(operation.pickPanel);
        operation.panel->show();
    } else if (!show && operation.pickPanel) {
        auto* splitter = operation.pickPanel;
        operation.pickPanel = nullptr;
        operation.historySplitter->insertWidget(0, m_tree);
        operation.historySplitter->setSizes(operation.historySizes);
        m_managerStack->addWidget(operation.panel);
        m_managerStack->setCurrentWidget(operation.panel);
        m_managerStack->removeWidget(splitter);
        splitter->hide();
        splitter->deleteLater();
    }
}

void MainWindow::finishOperation(bool accepted)
{
    if(!m_operation) return;
    const bool resumeSketch=!accepted && m_operation->fromSketch;
    const bool modifiedBefore=m_operation->modifiedBefore;
    if (m_operation->editing && !accepted) m_viewport->setShape(m_session.document().shape());
    showOperationReferenceTree(false);
    auto* panel=m_operation->panel;
    m_viewport->clearPreview();m_viewport->endContourSelection();
    // Retire the page outside the live widget tree immediately, then let Qt
    // destroy it after the current button event. New drafts cannot reuse it.
    for(auto* child:panel->findChildren<QObject*>()) {
        QObject::disconnect(child,nullptr,this,nullptr);
        QObject::disconnect(child,nullptr,panel,nullptr);
    }
    m_viewport->setReferenceCandidates({}, {});
    m_viewport->setFeatureReferenceShapes({});
    m_viewport->setExtrusionDepthGrips({});
    m_operation->drag.reset();
    if (QWidget::mouseGrabber() == m_viewport) m_viewport->releaseMouse();
    m_managerStack->setCurrentIndex(0);m_managerStack->removeWidget(panel);
    panel->hide();panel->setParent(nullptr);
    m_historyDock->setWindowTitle("FeatureManager");
    m_operation.reset();panel->deleteLater();
    if(resumeSketch) {
        m_mode=WorkspaceMode::EditingSketch;
        m_viewport->beginSketch(*m_session.editingSketch());m_viewport->selectSketchTool();m_tabs->setCurrentIndex(1);
        setWindowModified(modifiedBefore);
    } else {
        if(accepted) {m_session.editingSketch().reset();m_session.editingSketchId().clear();}
        m_mode=WorkspaceMode::Part;
        m_viewport->endSketch();
        if(!accepted) setWindowModified(modifiedBefore);
    }
    updateDocumentUi();
}

void MainWindow::operationSketchChanged()
{
    if (!m_operation) return;
    auto& draft = *m_operation;
    if (draft.pickingSketch && draft.sketches->currentData().isValid()) {
        draft.pickingSketch=false;
        static_cast<PropertyReference*>(draft.sketches)->setPicking(false);
        showOperationReferenceTree(false);
    }
    draft.candidate.reset();
    draft.availableContours.clear();
    draft.grips.clear();
    m_viewport->setExtrusionDepthGrips({});
    draft.defaultContour = false;
    draft.selectionCount->setText("0 regions selected");
    m_viewport->clearPreview();
    const QSignalBlocker blocker(draft.contours);
    draft.contours->clear();
    draft.sketches->setVisible(!draft.sketches->currentData().isValid());
    const auto sourceId = draft.sketches->currentData().toString().toStdString();
    const auto found = std::find_if(draft.sourceDocument->features().begin(), draft.sourceDocument->features().end(),
                                    [&sourceId](const auto& item) { return item.id == sourceId; });
    if (found == draft.sourceDocument->features().end()) {
        m_viewport->endContourSelection();
        draft.panel->setStatus("Select a source sketch explicitly.");
        draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    try {
        m_viewport->beginContourSelection(found->sketch);
        draft.availableContours = modeling::closedContours(found->sketch);
        for (std::size_t i = 0; i < draft.availableContours.size(); ++i) {
            QVariantList ids;
            for (const auto& id : draft.availableContours[i].entityIds) ids.append(QString::fromStdString(id));
            auto* item=new QListWidgetItem(QString("%1-Contour<%2>").arg(QString::fromStdString(found->name)).arg(i+1),draft.contours);
            item->setData(Qt::UserRole,ids);
        }
        const auto open = modeling::openExtrusionProfile(found->sketch);
        const bool onlyOpen = draft.availableContours.empty() && !open.empty();
        draft.options->setOpenProfile(!open.empty(), onlyOpen, open.size() > 1);
        if (!open.empty()) {
            draft.availableContours.push_back({open, {}, {}});
            auto* item = new QListWidgetItem("Open profile (Thin Feature)", draft.contours);
            if (onlyOpen && !draft.editing) item->setSelected(true);
        }
        draft.contours->setCurrentRow(-1, QItemSelectionModel::NoUpdate);
        static_cast<PropertySelectionList*>(draft.contours)->refreshMembers();
        draft.panel->setStatus(draft.availableContours.empty()
            ? "This sketch has no closed region or supported open profile."
            : "Select a contour to generate a preview.");
        if (draft.availableContours.size() == 1 && !draft.editing) {
            draft.defaultContour = !onlyOpen;
            updateOperationPreview();
            return;
        }
    } catch (const std::exception& error) { draft.panel->setStatus(QString::fromUtf8(error.what()),true); }
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}

void MainWindow::updateOperationPreview()
{
    if (!m_operation) return;
    auto& draft = *m_operation;
    draft.candidate.reset();
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    if (!draft.pickOptions.empty()) return;
    draft.grips.clear();
    m_viewport->setExtrusionDepthGrips({});
    auto selected = draft.contours->selectedItems();
    static_cast<PropertySelectionList*>(draft.contours)->refreshMembers();
    if (draft.defaultContour && selected.isEmpty()) selected.push_back(draft.contours->item(0));
    draft.selectionCount->setText(draft.defaultContour ? QString("Using the whole profile") :
        QString("%1 region%2 selected").arg(selected.size()).arg(selected.size()==1?"":"s"));
    std::vector<TopoDS_Face> selectedFaces;
    for (auto* item : selected) {
        const auto& face = draft.availableContours[std::size_t(draft.contours->row(item))].face;
        if (!face.IsNull()) selectedFaces.push_back(face);
    }
    m_viewport->setSelectedContourFaces(selectedFaces);
    m_viewport->setFeatureReferenceShapes(draft.options->selectedReferences());
    if(selected.isEmpty()) {
        m_viewport->clearPreview();
        draft.panel->setStatus("Click inside a closed region to select it.");
        return;
    }
    try {
        auto feature = draft.feature;
        feature.sketchId = draft.sketches->currentData().toString().toStdString();
        modeling::RegionReferences references;
        for(auto* item:selected)references.push_back(draft.availableContours[std::size_t(draft.contours->row(item))].entityIds);
        feature.contour.clear();feature.contours.clear();
        if(references.size()==1)feature.contour=references.front();else feature.contours=references;
        const auto options = draft.options->definition();
        feature.length = options.first.depth;
        feature.direction = draft.options->direction();
        feature.throughAll = options.first.end == modeling::ExtrudeEnd::ThroughAll;
        const bool legacy = options.start == modeling::ExtrudeStart::SketchPlane && !options.direction &&
            !options.second && !options.thin && options.merge && !options.flipCut && options.scope.empty() &&
            options.first.draftAngle == 0 && (options.first.end == modeling::ExtrudeEnd::Blind || feature.throughAll);
        // Keep the original topology identities for unchanged Blind and Through All features.
        feature.extrusion = legacy && !draft.feature.extrusion ? std::nullopt : std::optional(options);
        auto result=application::FeatureOperation::preview(*draft.sourceDocument,feature,references,draft.editing);
        if (draft.preview->isChecked()) {
            m_viewport->previewShape(result.previewShape,result.changes);
            updateOperationDepthGrips(feature, options, selectedFaces);
        } else m_viewport->clearPreview();
        draft.candidate=std::move(result.document);draft.candidateFeatureId=std::move(result.featureId);
        draft.panel->setStatus(draft.preview->isChecked() ? "" : "Preview hidden");
        draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    } catch (const std::exception& error) {
        m_viewport->clearPreview();
        draft.panel->setStatus(QString("Cannot generate preview: %1").arg(error.what()),true);
    }
}

void MainWindow::operationRegionClicked(modeling::Point2 point)
{
    if (!m_operation) return;
    auto& draft = *m_operation;
    const auto sourceId = draft.sketches->currentData().toString().toStdString();
    const auto found = std::find_if(draft.sourceDocument->features().begin(), draft.sourceDocument->features().end(),
                                   [&sourceId](const auto& item) { return item.id == sourceId; });
    if (found == draft.sourceDocument->features().end()) return;
    int selected = -1;
    try {
        for (std::size_t i = 0; i < draft.availableContours.size(); ++i) {
            if (draft.availableContours[i].face.IsNull() ||
                !modeling::contourContains(draft.availableContours[i], found->sketch, point)) continue;
            if (selected >= 0) {
                draft.panel->setStatus("Several contours contain this point. Choose the intended contour in the list.");
                return;
            }
            selected = int(i);
        }
        if (selected >= 0) {
            draft.defaultContour = false;
            {const QSignalBlocker blocker(draft.contours);draft.contours->setCurrentRow(selected,QItemSelectionModel::NoUpdate);
            draft.contours->item(selected)->setSelected(!draft.contours->item(selected)->isSelected());}
            updateOperationPreview();
            draft.options->setContoursExpanded(true);
            focusOperationDepth();
        } else draft.panel->setStatus("No valid closed contour contains this point. Click inside a region or choose it in the list.");
    } catch (const std::exception& error) { draft.panel->setStatus(QString::fromUtf8(error.what()),true); }
}

void MainWindow::focusOperationDepth(){
    if(!m_operation || !m_operation->length->isEnabled() || !m_operation->panel->isVisible())return;
    m_operation->length->setFocus(Qt::OtherFocusReason);m_operation->length->selectAll();
}

void MainWindow::updateOperationDepthGrips(const document::Feature& feature, const modeling::ExtrudeDefinition& options,
                                           const std::vector<TopoDS_Face>& profiles)
{
    auto& draft = *m_operation;
    if (profiles.empty() || options.start == modeling::ExtrudeStart::Surface) return;
    const auto source = std::find_if(draft.sourceDocument->features().begin(), draft.sourceDocument->features().end(),
                                     [&](const auto& item) { return item.id == feature.sketchId; });
    if (source == draft.sourceDocument->features().end()) return;
    const auto axes = modeling::sketchAxes(source->sketch);
    auto sweep = options.direction ? draft.sourceDocument->resolveReference(*options.direction).frame.Direction() : axes.Direction();
    if (sweep.Dot(axes.Direction()) < 0) sweep.Reverse();
    if (feature.direction == modeling::Direction::Negative) sweep.Reverse();
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(profiles.front(), properties);
    auto center = properties.CentreOfMass();
    double offset = options.startOffset;
    if (options.start == modeling::ExtrudeStart::Vertex && options.startReference) {
        const auto point = draft.sourceDocument->resolveReference(*options.startReference).frame.Location();
        offset = gp_Vec(axes.Location(), point).Dot(gp_Vec(axes.Direction()));
    }
    center.Translate(gp_Vec(sweep) * (offset / sweep.Dot(axes.Direction())));
    using End = modeling::ExtrudeEnd;
    const auto add = [&](const modeling::ExtrudeLeg& leg, const gp_Dir& direction, QDoubleSpinBox* field) {
        if (leg.end != End::Blind && leg.end != End::MidPlane) return;
        const double travel = leg.end == End::MidPlane ? 0.5 : 1;
        const auto tip = center.Translated(gp_Vec(direction) * (leg.depth * travel));
        draft.grips.push_back({gp_Ax1(tip, direction), field, travel});
    };
    add(options.first, sweep, draft.length);
    if (options.first.end == End::MidPlane) add(options.first, sweep.Reversed(), draft.length);
    if (options.second) add(*options.second, sweep.Reversed(), draft.options->secondDepth());
    std::vector<gp_Ax1> endpoints;
    for (const auto& grip : draft.grips) endpoints.push_back(grip.endpoint);
    m_viewport->setExtrusionDepthGrips(endpoints);
}

bool MainWindow::operationPointerEvent(QObject* watched, QEvent* event)
{
    if (watched != m_viewport || !m_viewport->isReady()) return false;
    auto& draft = *m_operation;
    const auto type = event->type();
    if (type == QEvent::UngrabMouse || type == QEvent::WindowDeactivate) {
        draft.drag.reset();
        return false;
    }
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove && type != QEvent::MouseButtonRelease) return false;
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (type == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton && !draft.drag) {
        for (const auto& grip : draft.grips) {
            const auto tip = m_viewport->projectWorldPoint(grip.endpoint.Location());
            const double sample = std::max(1.0, grip.field->value() * 0.2);
            const auto inset = grip.endpoint.Location().Translated(-gp_Vec(grip.endpoint.Direction()) * sample);
            const auto axis = tip - m_viewport->projectWorldPoint(inset);
            const auto hit = tip - mouse->position();
            // When the camera looks down the extrusion axis, leave the cursor
            // free to select the contour underneath the collapsed grip.
            if (QPointF::dotProduct(axis, axis) < 4 || QPointF::dotProduct(hit, hit) > 100) continue;
            draft.drag = OperationDraft::DepthDrag{mouse->position(), axis * (grip.travelPerMillimeter / sample),
                                                   grip.field, grip.field->value()};
            m_viewport->grabMouse(Qt::SizeAllCursor);
            event->accept();
            return true;
        }
    }
    if (!draft.drag) return false;
    if (type == QEvent::MouseMove || (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)) {
        const auto drag = *draft.drag;
        const auto delta = mouse->position() - drag.start;
        const auto& axis = drag.pixelsPerMillimeter;
        drag.field->setValue(drag.value + QPointF::dotProduct(delta, axis) / QPointF::dotProduct(axis, axis));
        if (type == QEvent::MouseButtonRelease) {
            draft.drag.reset();
            if (QWidget::mouseGrabber() == m_viewport) m_viewport->releaseMouse();
        }
        event->accept();
        return true;
    }
    return false;
}

bool MainWindow::operationKeyEvent(QObject* watched,QEvent* event){
    if (!m_operation) return false;
    if (event->type() != QEvent::KeyPress) return operationPointerEvent(watched, event);
    auto* widget=qobject_cast<QWidget*>(watched);
    if(!widget || (widget!=this && widget->window()!=this))return false;
    auto* key=static_cast<QKeyEvent*>(event);
    // Normal control activation (buttons, combos, list navigation) keeps its keys.
    const bool depth=widget==m_operation->length || m_operation->length->isAncestorOf(widget);
    if(key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter){
        if(widget!=m_viewport && !depth && !qobject_cast<QDoubleSpinBox*>(widget) &&
           !qobject_cast<QDoubleSpinBox*>(widget->parentWidget()))return false;
        updateOperationPreview();
        if(m_operation && m_operation->candidate)m_operation->buttons->button(QDialogButtonBox::Ok)->click();
        key->accept();return true;
    }
    // View navigation may take focus. The next numeric keystroke starts depth
    // entry afresh; ordinary text fields and Ctrl shortcuts are never intercepted.
    if(widget==m_viewport && m_operation->length->isEnabled() && !key->text().isEmpty()
        && !(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))
        && (key->text().at(0).isDigit() || key->text()=="." || key->text()=="," || key->text()=="-")){
        focusOperationDepth();QApplication::sendEvent(m_operation->length,key);return true;
    }
    return false;
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    const bool draft=featureDraftActive()||m_session.editingSketch().has_value()||bool(m_smartDimension);
    const auto choice=(isWindowModified()||draft)?confirmUnsaved():UnsavedChoice::Discard;
    if(choice==UnsavedChoice::Cancel){event->ignore();return;}
    if(choice==UnsavedChoice::Save){
        try{
            auto snapshot=m_session.document();
            if(m_session.editingSketch()){
                const auto found=std::find_if(snapshot.features().begin(),snapshot.features().end(),[&](const auto& f){return f.id==m_session.editingSketchId();});
                if(found!=snapshot.features().end()){auto feature=*found;feature.sketch=*m_session.editingSketch();snapshot.replaceFeature(feature);}
            }
            QString path=m_session.path();if(path.isEmpty()||legacyPath(path))path=chooseSavePath(false);
            if(path.isEmpty()){event->ignore();return;}
            if(!path.endsWith(".thepart",Qt::CaseInsensitive))path+=".thepart";
            if(!sourceRevisionMatches(path)){event->ignore();return;}
            snapshot.save(path);
            m_session.setPath(QFileInfo(path).absoluteFilePath());
            rememberSourceRevision(m_session.path());
            m_session.markSaved();
            application::recordRecentDocument(path);
            notifyOpenAssemblyWindowsPartSaved(m_session.path());
        }catch(const std::exception& e){reportError(QString::fromUtf8(e.what()));event->ignore();return;}
    }
    // Unconfirmed feature previews never silently become modeling features.
    if(m_sketchModifier)finishSketchModifier(false);
    if(m_solidFeature)finishSolidFeature(false);
    if(m_referenceDraft)finishReferenceGeometry(false);
    if(m_operation)finishOperation(false);
    closeConstraints();closeSmartDimension();
    if(m_selectingPlane)m_viewport->cancelPlaneSelection();
    setWindowModified(false);m_viewport->releaseGraphics();event->accept();
    if (m_inPlaceFinished)
    {
        auto finished = std::move(m_inPlaceFinished);
        finished();
    }
}

} // namespace thecad::ui
