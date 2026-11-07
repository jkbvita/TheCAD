#include "ui/Theme.h"
#include <QDialogButtonBox>
#include "modeling/SketchTools.h"
#include "modeling/DimensionAnnotation.h"
#include <QInputDialog>
#include "ui/MainWindow.h"
#include "ui/files/NativeFileDialogs.h"
#include "exchange/ShapeExporter.h"
#include "ui/widgets/FeatureTreePresenter.h"
#include "ui/widgets/CommandBar.h"
#include "application/FeatureOperation.h"
#include "ui/CadViewport.h"
#include "ui/PropertyManagerPage.h"
#include "ui/ReferenceGeometryUi.h"
#include "ui/features/SolidFeaturePanel.h"
#include "modeling/Sketch.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/ReferencePlanes.h"

#include <QAction>
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
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
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

QString entityText(const modeling::SketchEntity& entity)
{
    using modeling::EntityKind;
    const QString first = QString("(%1, %2)").arg(entity.first.x).arg(entity.first.y);
    if(entity.kind==EntityKind::Point)return QString("Point: %1").arg(first);
    if (entity.kind == EntityKind::Circle) {
        return QString("Circle: center %1, radius %2 mm").arg(first).arg(entity.radius);
    }
    return QString("%1: %2 to (%3, %4)")
        .arg(entity.kind == EntityKind::Line ? (entity.construction?"Centerline":"Line") : "Rectangle", first)
        .arg(entity.second.x).arg(entity.second.y);
}
}

struct MainWindow::OperationDraft {
    document::Feature feature;
    bool editing = false;
    bool fromSketch = false;
    bool modifiedBefore = false;
    std::optional<document::PartDocument> sourceDocument;
    PropertyManagerPage* panel = nullptr;
    QComboBox* sketches = nullptr;
    QListWidget* contours = nullptr;
    QDoubleSpinBox* length = nullptr;
    QComboBox* direction = nullptr;
    QCheckBox* through = nullptr;
    QDialogButtonBox* buttons = nullptr;
    QLabel* selectionCount = nullptr;
    std::vector<modeling::Contour> availableContours;
    std::optional<document::PartDocument> candidate;
    std::string candidateFeatureId;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_session(document::PartDocument::createEmpty())
{
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
    addCommand("&Open...", "actionOpen", QKeySequence::Open, [this] { openDocument(); });
    fileMenu->addSeparator();
    addCommand("&Save", "actionSave", QKeySequence::Save, [this] { saveDocument(); });
    addCommand("Save &As...", "actionSaveAs", QKeySequence::SaveAs, [this] { saveDocumentAs(); });
    fileMenu->addSeparator();
    addCommand("E&xit", "actionExit", QKeySequence(Qt::ALT | Qt::Key_F4), [this] { close(); });

    ThemeManager::instance();

    auto* editMenu = menuBar()->addMenu("&Edit");
    auto* viewMenu = menuBar()->addMenu("&View");
    auto* preferences=editMenu->addAction("Preferences…");preferences->setObjectName("actionPreferences");connect(preferences,&QAction::triggered,this,[this]{QDialog dialog(this);dialog.setWindowTitle("Preferences");auto* layout=new QFormLayout(&dialog);auto* theme=new QComboBox;theme->setObjectName("appearanceTheme");theme->addItem("Dark",int(ThemeMode::Dark));theme->addItem("Light",int(ThemeMode::Light));theme->addItem("Follow system",int(ThemeMode::System));theme->setCurrentIndex(theme->findData(int(ThemeManager::instance().mode())));layout->addRow("Application theme",theme);connect(theme,&QComboBox::currentIndexChanged,&dialog,[theme]{ThemeManager::instance().setMode(ThemeMode(theme->currentData().toInt()));});auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();});
    connect(&ThemeManager::instance(),&ThemeManager::changed,this,[this]{hideRelationsContext();for(auto* a:findChildren<QAction*>())if(!a->icon().isNull()&&a->objectName().startsWith("action")&&!a->objectName().contains("Preferences"))a->setIcon(commandIcon(a->text().remove('&')));});
    m_undo=editMenu->addAction("Undo");m_undo->setObjectName("actionUndo");m_undo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    m_redo=editMenu->addAction("Redo");m_redo->setObjectName("actionRedo");m_redo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    connect(m_undo,&QAction::triggered,this,[this]{traverseHistory(false);});
    connect(m_redo,&QAction::triggered,this,[this]{traverseHistory(true);});
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
        if(QStringList{"actionSmartDimension","actionSelectSketchEntity","actionLine","actionRectangle","actionCircle","actionPoint","actionCenterline","actionPolygon"}.contains(name))result->setCheckable(true);
        result->setToolTip(text);
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
        connect(result, &QAction::triggered, this, callback);
        return result;
    };
    QGridLayout* featureGrid = nullptr;
    commandGroup(featureButtons, "Solid features", featureGrid);
    m_extrude = action("Extrude", "actionExtrude", [this] { extrude(false); });
    m_cut = action("Extruded Cut", "actionExtrudedCut", [this] { extrude(true); });
    commandButton(m_extrude, featureGrid, 0, 0, true);
    commandButton(m_cut, featureGrid, 0, 1, true);
    m_revolve=action("Revolved Boss/Base","actionRevolve",[this]{startSolidFeature(document::FeatureKind::Revolve);});
    m_revolvedCut=action("Revolved Cut","actionRevolvedCut",[this]{startSolidFeature(document::FeatureKind::RevolvedCut);});
    commandButton(m_revolve,featureGrid,0,2,true);commandButton(m_revolvedCut,featureGrid,0,3,true);
    m_sweep=action("Swept Boss/Base","actionSweep",[this]{startSolidFeature(document::FeatureKind::Sweep);});
    m_sweptCut=action("Swept Cut","actionSweptCut",[this]{startSolidFeature(document::FeatureKind::SweptCut);});
    commandButton(m_sweep,featureGrid,0,4,true);commandButton(m_sweptCut,featureGrid,0,5,true);
    QGridLayout* finishGrid=nullptr;commandGroup(featureButtons,"Edge features",finishGrid);
    m_fillet=action("Fillet","actionFillet",[this]{startSolidFeature(document::FeatureKind::Fillet);});
    m_chamfer=action("Chamfer","actionChamfer",[this]{startSolidFeature(document::FeatureKind::Chamfer);});
    commandButton(m_fillet,finishGrid,0,0,true);commandButton(m_chamfer,finishGrid,0,1,true);
    QGridLayout* patternGrid=nullptr;commandGroup(featureButtons,"Pattern / Mirror",patternGrid);
    m_linearPattern=action("Linear Pattern","actionLinearPattern",[this]{startSolidFeature(document::FeatureKind::LinearPattern);});
    m_mirror=action("Mirror","actionMirror",[this]{startSolidFeature(document::FeatureKind::Mirror);});
    commandButton(m_linearPattern,patternGrid,0,0,true);commandButton(m_mirror,patternGrid,0,1,true);
    QGridLayout* referenceGrid=nullptr;commandGroup(featureButtons,"Reference Geometry",referenceGrid);
    m_referencePlane=action("Reference Plane","actionReferencePlane",[this]{startReferenceGeometry(modeling::DatumKind::Plane);});
    m_referenceAxis=action("Reference Axis","actionReferenceAxis",[this]{startReferenceGeometry(modeling::DatumKind::Axis);});
    m_referencePoint=action("Reference Point","actionReferencePoint",[this]{startReferenceGeometry(modeling::DatumKind::Point);});
    commandButton(m_referencePlane,referenceGrid,0,0);commandButton(m_referenceAxis,referenceGrid,1,0);commandButton(m_referencePoint,referenceGrid,2,0);
    featureButtons->addStretch();

    QGridLayout *sketchGrid = nullptr, *drawGrid = nullptr, *modifyGrid = nullptr, *relationGrid = nullptr;
    commandGroup(sketchButtons, "Sketch", sketchGrid);
    m_createSketch = action("Sketch", "actionCreateSketch", [this] { createSketch(); });
    m_finishSketch = action("Exit Sketch", "actionFinishSketch", [this] { finishSketch(); });
    commandButton(m_createSketch, sketchGrid, 0, 0, true);
    commandButton(m_finishSketch, sketchGrid, 0, 1, true);
    commandButton(action("Smart Dimension", "actionSmartDimension", [this]{startSmartDimension();}), sketchGrid, 0, 2, true);
    commandGroup(sketchButtons, "Draw", drawGrid);
    m_selectEntity = action("Select", "actionSelectSketchEntity", [this] { closeSmartDimension();m_viewport->selectSketchTool(); });
    m_line = action("Line", "actionLine", [this] { closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Line); });
    m_rectangle = action("Rectangle", "actionRectangle", [this] { closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Rectangle); });
    m_circle = action("Circle", "actionCircle", [this] { closeSmartDimension();m_viewport->setSketchTool(modeling::EntityKind::Circle); });
    commandButton(m_selectEntity, drawGrid, 0, 0);
    commandButton(m_line, drawGrid, 1, 0);
    commandButton(m_rectangle, drawGrid, 2, 0);
    commandButton(m_circle, drawGrid, 0, 1);
    commandButton(unavailable("Arc", "actionArc"), drawGrid, 1, 1);
    commandButton(unavailable("Spline", "actionSpline"), drawGrid, 2, 1);
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
    commandButton(unavailable("Measure", "actionMeasure"), evaluateGrid, 0, 0, true);
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
    reset->setShortcut(QKeySequence(Qt::Key_Home));
    connect(reset, &QAction::triggered, m_viewport, &CadViewport::resetView);
    viewMenu->addAction(fit);
    viewMenu->addAction(reset);
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
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, "About TheCAD", "TheCAD\nOpen-source parametric mechanical CAD prototype.\nC++ / Qt 6 / Open CASCADE Technology");
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
    historySplitter->addWidget(geometryScroll);
    historySplitter->setStretchFactor(0, 2);
    historySplitter->setStretchFactor(1, 1);
    historyLayout->addWidget(historySplitter);
    m_managerStack = new QStackedWidget;
    m_managerStack->setObjectName("managerStack");
    m_managerStack->addWidget(history);
    historyDock->setWidget(m_managerStack);
    addDockWidget(Qt::LeftDockWidgetArea, historyDock);
    resizeDocks({historyDock}, {300}, Qt::Horizontal);
    viewMenu->addSeparator();
    viewMenu->addAction(historyDock->toggleViewAction());
    editMenu->addAction(m_editEntity);
    editMenu->addAction(m_deleteEntity);
    initializeSelectionUi(viewMenu);
    editMenu->addAction(m_deleteFeatures);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { editSelectedFeature(); });
    connect(m_editFeature, &QAction::triggered, this, [this] { editSelectedFeature(); });
    connect(m_entities, &QListWidget::itemDoubleClicked, this, [this] { editSketchEntity(); });
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
        if (entity.kind == modeling::EntityKind::Circle) entity.radius = m_propertyRadius->value();
        else if(entity.kind!=modeling::EntityKind::Point)entity.second = {m_propertySecondX->value(), m_propertySecondY->value()};
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
            .arg(role == int(modeling::PointRole::Second) ? "Endpoint" : role == int(modeling::PointRole::Center) ? "Center" : "Start point", id);
        statusBar()->showMessage(pointName + " selected. Snapping uses exact sketch coordinates.");
    });
    connect(m_viewport,&CadViewport::sketchSelectionChanged,this,[this]{hideRelationsContext();updateActions();if(m_constraints && m_viewport->selectedSketchRelation().empty())updateConstraintDraft();if(m_sketchModifier)refreshSketchModifier(true);});
    connect(m_viewport, &CadViewport::sketchLineEditPointed, this, &MainWindow::updateSketchLineEdit);
    connect(m_viewport,&CadViewport::relationsContextRequested,this,&MainWindow::showRelationsContext);
    qApp->installEventFilter(this);
    connect(m_viewport,&CadViewport::sketchRelationDeleteRequested,this,[this](const QString& id){deleteRelation(id.toStdString());});
    connect(m_viewport,&CadViewport::sketchDeleteRequested,this,[this]{deleteSketchEntity();});
    connect(m_viewport,&CadViewport::sketchToolChanged,this,[this]{hideRelationsContext();updateSketchToolActions();});
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
    connect(m_viewport, &CadViewport::referencePlaneSelected, this, [this](modeling::SketchPlane plane) { referencePlaneSelected(plane); });
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
    connect(m_viewport,&CadViewport::referenceGeometryPicked,this,[this](int option){if(m_solidFeature)m_solidFeature->pickReference(option);else referenceGeometryPicked(option);});
    connect(m_viewport,&CadViewport::datumSelected,this,[this](const QString& id){
        if(featureDraftActive()||m_session.editingSketch())return;
        if(id.isEmpty()){m_session.selectedFeatureId().clear();{const QSignalBlocker blocker(m_tree);m_tree->clearSelection();m_tree->setCurrentItem(nullptr);}updateActions();return;}
        if(m_selectingPlane){createSketchOnReferencePlane(id.toStdString());return;}
        selectFeature(id.toStdString());
    });

    auto* cancelFeature = new QAction("Cancel feature", this);
    cancelFeature->setObjectName("actionCancelFeature");cancelFeature->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(cancelFeature);connect(cancelFeature,&QAction::triggered,this,[this]{if(m_sketchModifier)finishSketchModifier(false);else if(m_solidFeature)finishSolidFeature(false);else if(m_referenceDraft)finishReferenceGeometry(false);else finishOperation(false);});
    statusBar()->showMessage("Orbit: middle drag | Pan: Ctrl + middle | Zoom: Shift + middle / wheel | Normal To: Ctrl+8 | Fit: F");
    m_dimensions = new QLabel(this);
    statusBar()->addPermanentWidget(m_dimensions);
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

QString MainWindow::chooseOpenPath()
{
    return QFileDialog::getOpenFileName(this, "Open TheCAD part", m_session.path(),
        "TheCAD Part (*.thepart *.tcpart);;TheCAD Part (*.thepart);;Legacy TheCAD Part (*.tcpart)");
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
    if (circle) {
        radius = number(form, "Radius", entity.radius, true);
        radius->setObjectName("entityRadius");
    }
    else if(entity.kind!=modeling::EntityKind::Point) {
        x2 = number(form, "Second U", entity.second.x);
        y2 = number(form, "Second V", entity.second.y);
        x2->setObjectName("entitySecondX");
        y2->setObjectName("entitySecondY");
    }
    dialogButtons(dialog, layout);
    dialog.findChild<QDialogButtonBox*>()->setObjectName("entityButtons");
    if (dialog.exec() != QDialog::Accepted) return false;
    entity.first = {x1->value(), y1->value()};
    if (circle) entity.radius = radius->value();
    else if(x2)entity.second = {x2->value(), y2->value()};
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
        setWindowModified(false);return true;
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
    try {
        m_session.document().save(path);
        m_session.setPath(QFileInfo(path).absoluteFilePath());
        setWindowModified(false);m_session.markSaved();
        updateDocumentUi();
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
    m_session.clearHistory();
    updateDocumentUi();
    m_viewport->showReferencePlanes(m_session.document().features().empty());
    m_viewport->resetView(); // Opening a different document frames it once.
    return true;
}

bool MainWindow::applyModel(document::PartDocument candidate, const std::string& selectedId,bool record)
{
    auto previous=editState();
    if (!m_viewport->setShape(candidate.shape())) {
        reportError("The regenerated geometry could not be displayed. The previous part was retained.");
        return false;
    }
    if(record)rememberChange(std::move(previous));
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
    if(m_solidFeature){const auto* item=m_tree->currentItem();if(item)m_solidFeature->pickTree(item->data(0,Qt::UserRole).toString());return;}
    if(m_referenceDraft){const auto* item=m_tree->currentItem();if(item)referenceGeometryTreePicked(item->data(0,Qt::UserRole).toString());return;}
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
        m_viewport->showReferencePlanes(true);
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
            m_viewport->setTreeBodySelected(feature&&document::isSolidFeature(feature->kind));
        }
    }
    if(m_tree->selectedItems().size()>1){std::vector<modeling::SelectionRef> refs;for(const auto* node:m_tree->selectedItems()){const auto id=node->data(0,Qt::UserRole).toString();using K=modeling::SelectionKind;if(id=="origin:XY"||id=="origin:XZ"||id=="origin:YZ")refs.push_back({K::OriginPlane,{},{},modeling::PointRole::First,id=="origin:XY"?0:id=="origin:XZ"?1:2});else for(const auto& f:m_session.document().features())if(f.id==id.toStdString()&&f.kind==document::FeatureKind::ReferenceGeometry)refs.push_back({f.datum.kind==modeling::DatumKind::Plane?K::ReferencePlane:f.datum.kind==modeling::DatumKind::Axis?K::ReferenceAxis:K::ReferencePoint,f.id});}m_viewport->setTreeReferenceSelection(refs);}
    updateEntityList();
    updateActions();
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
}

void MainWindow::updateActions()
{
    updateSketchToolActions();updateHistoryActions();
    const bool operation = featureDraftActive();
    const bool editing = m_mode == WorkspaceMode::EditingSketch && !m_constraints;
    findChild<QAction*>("actionSmartDimension")->setEnabled(editing && !operation);
    findChild<QAction*>("actionDisplayDeleteRelations")->setEnabled(editing && !operation);
    const bool selecting = m_selectingPlane;
    if (m_deleteFeatures)
        m_deleteFeatures->setEnabled(!m_session.editingSketch() && !operation &&
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
    m_tree->setEnabled((!operation || m_solidFeature) && !editing && !selecting);
    for (const auto* name : {"actionNew", "actionOpen", "actionSave", "actionSaveAs"}) {
        if (auto* action = findChild<QAction*>(name)) action->setEnabled(!operation && !selecting && !m_constraints);
    }
    bool sketchSupport=true;const auto selection=m_viewport->selection();
    if(selection.size()>1)sketchSupport=false;
    else if(selection.size()==1){using K=modeling::SelectionKind;const auto kind=selection.front().kind;sketchSupport=kind==K::Face||kind==K::ReferencePlane||kind==K::OriginPlane;
        if(kind==K::Face)try{(void)m_session.document().sketchOnFace(selection.front().index);}catch(const std::exception&){sketchSupport=false;}}
    m_createSketch->setEnabled(!m_session.editingSketch() && !operation && !selecting && sketchSupport);
    m_selectEntity->setEnabled(editing && !operation);
    m_line->setEnabled(editing && !operation);
    m_rectangle->setEnabled(editing && !operation);
    m_circle->setEnabled(editing && !operation);
    for(auto* tool:{m_point,m_centerline,m_polygon})tool->setEnabled(editing && !operation);
    updateConstructionControls();
    m_finishSketch->setEnabled(editing && !operation);
    const auto* selected = selectedFeature();
    m_editFeature->setEnabled(!m_session.editingSketch() && !operation && !selecting && selected);
    m_editEntity->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    m_deleteEntity->setEnabled(editing && !operation && (m_entities->currentRow()>=0 || !m_viewport->selectedSketchDimensions().empty() || !m_viewport->selectedSketchEntities().empty()));
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
    m_extrude->setEnabled(profile && !m_constraints && !m_smartDimension);
    m_cut->setEnabled(profile && !m_constraints && !m_smartDimension && !m_session.document().shape().IsNull());
    m_revolve->setEnabled(profile&&!m_constraints&&!m_smartDimension);
    m_revolvedCut->setEnabled(m_revolve->isEnabled()&&!m_session.document().shape().IsNull());
    m_sweep->setEnabled(m_revolve->isEnabled());
    m_sweptCut->setEnabled(m_revolvedCut->isEnabled());
    const bool edges=!operation&&!selecting&&!m_session.editingSketch()&&!m_session.document().shape().IsNull();
    m_fillet->setEnabled(edges);m_chamfer->setEnabled(edges);
    m_linearPattern->setEnabled(edges);m_mirror->setEnabled(edges);
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

void MainWindow::referencePlaneSelected(modeling::SketchPlane plane)
{
    m_selectedFace=0;
    m_viewport->setSelectedSolidFace(0);
    if (m_session.editingSketch() || m_operation) return;
    if (m_selectingPlane) {
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
        .arg(circle ? "Circle" : entity.kind==modeling::EntityKind::Point?"Point":entity.kind == modeling::EntityKind::Line ? (entity.construction?"Centerline":"Line") : "Legacy rectangle")
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
        form->setRowVisible(m_propertyRadius, circle);
    }
    m_propertyFirstX->setEnabled(editing);
    m_propertyFirstY->setEnabled(editing);
    m_propertySecondX->setEnabled(editing && !circle && entity.kind!=modeling::EntityKind::Point);
    m_propertySecondY->setEnabled(editing && !circle && entity.kind!=modeling::EntityKind::Point);
    m_propertyRadius->setEnabled(editing && circle);
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
    if(featureDraftActive() || m_selectingPlane) return;
    m_selectedFace=0;m_viewport->setSelectedSolidFace(0);
    auto sourceDocument=m_session.document();
    if(m_session.editingSketch()) {
        m_viewport->selectSketchTool();
        try {
            if(modeling::closedContours(*m_session.editingSketch()).empty())
                throw std::runtime_error("The active sketch has no valid closed contour. Continue sketch editing and close a profile.");
            auto source=*std::find_if(sourceDocument.features().begin(),sourceDocument.features().end(),[this](const auto& item){return item.id==m_session.editingSketchId();});
            source.sketch=*m_session.editingSketch();sourceDocument.replaceFeature(source);
        } catch(const std::exception& error) {reportError(QString("Cannot start the feature: %1").arg(error.what()));return;}
    }
    m_operation=std::make_unique<OperationDraft>();auto& draft=*m_operation;
    draft.feature=std::move(feature);draft.editing=editing;
    draft.fromSketch=m_session.editingSketch().has_value();draft.modifiedBefore=isWindowModified();
    draft.sourceDocument=std::move(sourceDocument);m_mode=WorkspaceMode::FeatureDraft;
    draft.panel=new PropertyManagerPage(draft.feature.kind==document::FeatureKind::Cut ? "Extruded Cut" : "Extrude",
        "Click inside sketch regions, then type the depth. The preview updates as you work.",m_managerStack);
    auto* settings=draft.panel->settings;draft.buttons=draft.panel->buttons;
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    auto* referenceForm=new QFormLayout;
    auto* startingReference=new QLabel("Sketch Plane");startingReference->setObjectName("operationStartingReference");
    referenceForm->addRow("Starting reference",startingReference);settings->addLayout(referenceForm);
    auto* legend=new QLabel("Blue: selected regions\nAmber: added material  ·  Red: removed material");legend->setObjectName("operationPreviewLegend");legend->setWordWrap(true);settings->addWidget(legend);
    auto* directionGroup=new QGroupBox("Direction 1");auto* directionForm=new QFormLayout(directionGroup);directionForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* endCondition=new QLabel(draft.feature.throughAll ? "Through All" : "Blind");endCondition->setObjectName("operationEndCondition");
    directionForm->addRow("End condition",endCondition);
    draft.length=number(directionForm,"Depth",draft.feature.length,true);draft.length->setObjectName("featureLength");
    draft.length->setAccessibleName("Feature depth in millimeters");
    draft.direction=new QComboBox;draft.direction->setObjectName("featureDirection");
    draft.direction->addItems({"Along sketch normal","Opposite sketch normal"});
    draft.direction->setCurrentIndex(draft.feature.direction==modeling::Direction::Positive ? 0 : 1);
    directionForm->addRow("Direction",draft.direction);
    auto* reverse=new QPushButton("Reverse direction");reverse->setObjectName("featureReverseDirection");
    directionForm->addRow(reverse);
    draft.through=new QCheckBox("Through all in selected direction",directionGroup);draft.through->setObjectName("featureThroughAll");
    draft.through->setVisible(draft.feature.kind==document::FeatureKind::Cut);
    if(draft.feature.kind==document::FeatureKind::Cut) {
        directionForm->addRow(draft.through);draft.through->setChecked(draft.feature.throughAll);draft.length->setEnabled(!draft.feature.throughAll);
    }
    settings->addWidget(directionGroup);
    auto* selectionGroup=new QGroupBox("Selected Contours");auto* selectionForm=new QFormLayout(selectionGroup);selectionForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    draft.sketches=new QComboBox;draft.sketches->setObjectName("operationSketch");draft.sketches->setPlaceholderText("Select a sketch");
    int selectedIndex=-1;
    for(const auto& item:draft.sourceDocument->features()) {
        if(editing && item.id==draft.feature.id) break;
        if(item.kind!=document::FeatureKind::Sketch) continue;
        draft.sketches->addItem(QString::fromStdString(item.name),QString::fromStdString(item.id));
        if(item.id==draft.feature.sketchId) selectedIndex=draft.sketches->count()-1;
    }
    selectionForm->addRow("Sketch",draft.sketches);draft.sketches->setEnabled(!draft.fromSketch);
    draft.contours=new QListWidget;draft.contours->setObjectName("operationContour");
    draft.contours->setSelectionMode(QAbstractItemView::MultiSelection);draft.contours->setMaximumHeight(150);draft.contours->setMinimumHeight(80);
    draft.selectionCount=new QLabel("0 regions selected");draft.selectionCount->setObjectName("operationSelectionCount");selectionForm->addRow(draft.selectionCount);
    selectionForm->addRow("Contour",draft.contours);
    auto* instructions=new QLabel("Click regions to add or remove them, or select multiple contours here. Inner boundaries form holes in their surrounding region.");
    instructions->setWordWrap(true);selectionForm->addRow(instructions);settings->addWidget(selectionGroup);
    settings->addStretch();
    connect(draft.sketches,&QComboBox::currentIndexChanged,this,[this]{operationSketchChanged();});
    connect(draft.contours,&QListWidget::itemSelectionChanged,this,[this]{updateOperationPreview();});
    connect(draft.length,&QDoubleSpinBox::valueChanged,this,[this]{updateOperationPreview();});
    connect(draft.length->findChild<QLineEdit*>(),&QLineEdit::textChanged,this,[this]{
        if(m_operation && (!m_operation->length->hasAcceptableInput() || !m_operation->candidate)) updateOperationPreview();
    });
    connect(draft.direction,&QComboBox::currentIndexChanged,this,[this]{updateOperationPreview();});
    connect(reverse,&QPushButton::clicked,this,[this]{if(m_operation)m_operation->direction->setCurrentIndex(1-m_operation->direction->currentIndex());});
    connect(draft.through,&QCheckBox::toggled,this,[this,endCondition](bool through){
        if(!m_operation)return;endCondition->setText(through?"Through All":"Blind");m_operation->length->setEnabled(!through);updateOperationPreview();if(!through)focusOperationDepth();
    });
    connect(draft.buttons,&QDialogButtonBox::rejected,this,[this]{finishOperation(false);});
    connect(draft.buttons,&QDialogButtonBox::accepted,this,[this]{
        updateOperationPreview();if(!m_operation || !m_operation->candidate)return;
        m_viewport->clearPreview();if(applyModel(*m_operation->candidate,m_operation->candidateFeatureId))finishOperation(true);
    });
    draft.sketches->setCurrentIndex(selectedIndex);operationSketchChanged();
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
    m_managerStack->addWidget(draft.panel);m_managerStack->setCurrentWidget(draft.panel);
    m_historyDock->setWindowTitle("PropertyManager");updateDocumentUi();
    focusOperationDepth();
}

void MainWindow::finishOperation(bool accepted)
{
    if(!m_operation) return;
    const bool resumeSketch=!accepted && m_operation->fromSketch;
    const bool modifiedBefore=m_operation->modifiedBefore;
    auto* panel=m_operation->panel;
    m_viewport->clearPreview();m_viewport->endContourSelection();
    // Retire the page outside the live widget tree immediately, then let Qt
    // destroy it after the current button event. New drafts cannot reuse it.
    for(auto* child:panel->findChildren<QObject*>()) QObject::disconnect(child,nullptr,this,nullptr);
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
    draft.candidate.reset();
    draft.availableContours.clear();
    draft.selectionCount->setText("0 regions selected");
    m_viewport->clearPreview();
    const QSignalBlocker blocker(draft.contours);
    draft.contours->clear();
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
            auto* item=new QListWidgetItem(QString("Region %1").arg(i+1),draft.contours);
            item->setData(Qt::UserRole,ids);
        }
        draft.contours->setCurrentRow(-1);draft.contours->clearSelection();
        draft.panel->setStatus(draft.availableContours.empty()
            ? "This sketch has no valid closed contour. Finish a valid closed profile before using it."
            : "Select a contour explicitly to generate a preview.");
    } catch (const std::exception& error) { draft.panel->setStatus(QString::fromUtf8(error.what()),true); }
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}

void MainWindow::updateOperationPreview()
{
    if (!m_operation) return;
    auto& draft = *m_operation;
    draft.candidate.reset();
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    const auto selected=draft.contours->selectedItems();
    draft.selectionCount->setText(QString("%1 region%2 selected").arg(selected.size()).arg(selected.size()==1?"":"s"));
    std::vector<TopoDS_Face> selectedFaces;
    for(auto* item:selected)selectedFaces.push_back(draft.availableContours[std::size_t(draft.contours->row(item))].face);
    m_viewport->setSelectedContourFaces(selectedFaces);
    bool numeric=true;
    const double depthValue=draft.length->isEnabled()?draft.length->locale().toDouble(draft.length->cleanText(),&numeric):draft.length->value();
    if(draft.length->isEnabled() && (!numeric || !draft.length->hasAcceptableInput())) {
        m_viewport->clearPreview();draft.panel->setStatus("Enter a valid positive depth in millimeters.",true);return;
    }
    // Read the pending numeric text without normalizing it mid-keystroke. In
    // particular, interpreting "23." here would erase the decimal separator.
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
        feature.length = depthValue;
        feature.direction = draft.direction->currentIndex() == 0 ? modeling::Direction::Positive : modeling::Direction::Negative;
        feature.throughAll = feature.kind == document::FeatureKind::Cut && draft.through->isChecked();
        auto result=application::FeatureOperation::preview(*draft.sourceDocument,feature,references,draft.editing);
        m_viewport->previewShape(result.document.shape(),result.changes);
        draft.candidate=std::move(result.document);draft.candidateFeatureId=std::move(result.featureId);
        draft.panel->setStatus("Preview ready. Type a depth or select more regions. Enter confirms.");
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
            if (!modeling::contourContains(draft.availableContours[i], found->sketch, point)) continue;
            if (selected >= 0) {
                draft.panel->setStatus("Several contours contain this point. Choose the intended contour in the list.");
                return;
            }
            selected = int(i);
        }
        if (selected >= 0) {
            {const QSignalBlocker blocker(draft.contours);draft.contours->setCurrentRow(selected,QItemSelectionModel::NoUpdate);
            draft.contours->item(selected)->setSelected(!draft.contours->item(selected)->isSelected());}
            updateOperationPreview();
            focusOperationDepth();
        } else draft.panel->setStatus("No valid closed contour contains this point. Click inside a region or choose it in the list.");
    } catch (const std::exception& error) { draft.panel->setStatus(QString::fromUtf8(error.what()),true); }
}

void MainWindow::focusOperationDepth(){
    if(!m_operation || !m_operation->length->isEnabled() || !m_operation->panel->isVisible())return;
    m_operation->length->setFocus(Qt::OtherFocusReason);m_operation->length->selectAll();
}

bool MainWindow::operationKeyEvent(QObject* watched,QEvent* event){
    if(!m_operation || event->type()!=QEvent::KeyPress)return false;
    auto* widget=qobject_cast<QWidget*>(watched);
    if(!widget || (widget!=this && widget->window()!=this))return false;
    auto* key=static_cast<QKeyEvent*>(event);
    // Normal control activation (buttons, combos, list navigation) keeps its keys.
    const bool depth=widget==m_operation->length || m_operation->length->isAncestorOf(widget);
    if(key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter){
        if(widget!=m_viewport && !depth)return false;
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
    if (featureDraftActive()) {
        reportError("Confirm or cancel the feature preview before closing this part.");
        event->ignore();
        return;
    }
    if (m_selectingPlane) m_viewport->cancelPlaneSelection();
    if (maybeSave()) { m_viewport->releaseGraphics(); event->accept(); }
    else event->ignore();
}

} // namespace thecad::ui
