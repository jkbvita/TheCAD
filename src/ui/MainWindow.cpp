#include "modeling/SketchTools.h"
#include "modeling/DimensionAnnotation.h"
#include <QInputDialog>
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
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
// Original line-art icons, painted at device-independent resolution.
QIcon commandIcon(const QString& name)
{
    QPixmap image(64, 64);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    painter.setPen(QPen(QColor("#276a91"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if(name.contains("Point")){painter.setBrush(QColor("#276a91"));painter.drawEllipse(QPointF(16,16),3.5,3.5);}
    else if(name.contains("Polygon"))painter.drawPolygon(QPolygonF{QPointF(16,4),QPointF(26,10),QPointF(26,22),QPointF(16,28),QPointF(6,22),QPointF(6,10)});
    else if(name.contains("Centerline") || name.contains("Construction")){painter.setPen(QPen(QColor("#276a91"),1.6,Qt::DashDotLine));painter.drawLine(QPointF(4,27),QPointF(28,5));}
    else if (name.contains("Circle")) painter.drawEllipse(QRectF(6, 6, 20, 20));
    else if (name.contains("Rectangle")) painter.drawRect(QRectF(5, 8, 22, 16));
    else if (name.contains("Line")) {
        painter.drawLine(QPointF(5, 26), QPointF(27, 6));
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(3, 24, 4, 4));
        painter.drawRect(QRectF(25, 4, 4, 4));
    } else if (name.contains("Arc")) {
        painter.drawArc(QRectF(5, 5, 22, 22), 15 * 16, 270 * 16);
    } else if (name.contains("Spline")) {
        QPainterPath path(QPointF(4, 25));
        path.cubicTo(12, -10, 17, 43, 28, 5);
        painter.drawPath(path);
    } else if (name.contains("Sketch") || name.contains("Exit")) {
        painter.drawRect(QRectF(5, 6, 19, 21));
        painter.setPen(QPen(QColor("#d49024"), 2.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(13, 22), QPointF(28, 5));
    } else if (name.contains("Origin")) {
        painter.setPen(QPen(QColor("#b83b42"), 1.6));
        painter.drawLine(5, 25, 27, 25); painter.drawLine(8, 28, 8, 4);
        painter.setBrush(QColor("#b83b42")); painter.drawEllipse(QPointF(8, 25), 2.5, 2.5);
    } else if (name.contains("Plane")) {
        painter.drawPolygon(QPolygonF{QPointF(4, 11), QPointF(23, 5), QPointF(28, 23), QPointF(9, 28)});
    } else if (name.contains("Extrude") || name.contains("Part")) {
        painter.drawPolygon(QPolygonF{QPointF(6, 10), QPointF(20, 5), QPointF(27, 11), QPointF(13, 17)});
        painter.drawPolygon(QPolygonF{QPointF(6, 10), QPointF(13, 17), QPointF(13, 28), QPointF(6, 21)});
        painter.drawPolygon(QPolygonF{QPointF(13, 17), QPointF(27, 11), QPointF(27, 22), QPointF(13, 28)});
        if (name.contains("Cut")) { painter.setPen(QPen(QColor("#b83b42"), 2)); painter.drawLine(18, 12, 24, 19); }
    } else if (name.contains("Select")) {
        painter.drawPolygon(QPolygonF{QPointF(8, 4), QPointF(8, 26), QPointF(14, 20), QPointF(20, 29), QPointF(24, 27), QPointF(18, 18), QPointF(27, 17)});
    } else {
        painter.drawRect(QRectF(6, 7, 20, 18));
        painter.drawLine(8, 24, 25, 8);
        painter.drawEllipse(QPointF(16, 16), 3, 3);
    }
    return QIcon(image);
}

QWidget* commandGroup(QHBoxLayout* row, const QString& name, QGridLayout*& grid)
{
    auto* group = new QWidget;
    auto* column = new QVBoxLayout(group);
    column->setContentsMargins(6, 0, 6, 0);
    column->setSpacing(1);
    grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(2);
    grid->setVerticalSpacing(1);
    column->addLayout(grid);
    auto* caption = new QLabel(name);
    caption->setAlignment(Qt::AlignCenter);
    caption->setStyleSheet("color: #626a72; font-size: 10px;");
    column->addWidget(caption);
    row->addWidget(group);
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet("color: #d1d5d9;");
    row->addWidget(separator);
    return group;
}

QToolButton* commandButton(QAction* action, QGridLayout* grid, int row, int column, bool large = false)
{
    auto* button = new QToolButton;
    button->setDefaultAction(action);
    button->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
    button->setIconSize(large ? QSize(28, 28) : QSize(18, 18));
    button->setAutoRaise(true);
    button->setAttribute(Qt::WA_AlwaysShowToolTips);
    grid->addWidget(button, row, column, large ? 3 : 1, 1);
    return button;
}
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
    QWidget* panel = nullptr;
    QComboBox* sketches = nullptr;
    QListWidget* contours = nullptr;
    QDoubleSpinBox* length = nullptr;
    QComboBox* direction = nullptr;
    QCheckBox* through = nullptr;
    QLabel* status = nullptr;
    QDialogButtonBox* buttons = nullptr;
    std::vector<modeling::Contour> availableContours;
    std::optional<document::PartDocument> candidate;
    std::string candidateFeatureId;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_document(document::PartDocument::createEmpty())
{
    // Windows dark widget themes can ignore a light per-window palette. Use
    // Qt's palette-aware widget style; native Windows title bars/dialogs remain.
    if (QApplication::style()->objectName().compare("fusion", Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create("Fusion"));
    resize(1200, 800);
    m_viewport = new CadViewport(m_document.shape(), this);
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

    // Keep the standard Windows window and Qt menus, with a light workspace palette.
    QPalette light = palette();
    light.setColor(QPalette::Window, QColor("#f5f6f7"));
    light.setColor(QPalette::Base, QColor("#ffffff"));
    light.setColor(QPalette::AlternateBase, QColor("#f1f4f6"));
    light.setColor(QPalette::Text, QColor("#20282f"));
    light.setColor(QPalette::WindowText, QColor("#20282f"));
    light.setColor(QPalette::Button, QColor("#f5f6f7"));
    light.setColor(QPalette::ButtonText, QColor("#20282f"));
    light.setColor(QPalette::Highlight, QColor("#c6e3f3"));
    light.setColor(QPalette::HighlightedText, QColor("#173e55"));
    light.setColor(QPalette::Disabled, QPalette::Text, QColor("#989fa5"));
    light.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#989fa5"));
    QApplication::setPalette(light);
    setPalette(light);
    setStyleSheet("QMainWindow { background: #f5f6f7; } "
                  "QToolBar { background: #f5f6f7; border: none; spacing: 3px; } "
                  "QMenuBar, QMenu { background: #f5f6f7; color: #20282f; } "
                  "QMenu::item:selected, QMenuBar::item:selected { background: #d9ebf6; } "
                  "QMenu::item:disabled { color: #989fa5; } "
                  "QTabWidget::pane { border: 1px solid #cbd0d5; background: #f5f6f7; } "
                  "QTabBar::tab { padding: 4px 15px; background: #e6e8eb; border: 1px solid #cbd0d5; } "
                  "QTabBar::tab:selected { background: #ffffff; color: #174c6e; } "
                  "QToolButton:hover { background: #d9ebf6; border-radius: 2px; } "
                  "QToolButton:checked { background: #b8d8ef; border: 1px solid #709ab7; border-radius: 2px; } "
                  "QToolButton:disabled { color: #a3a8ad; } "
                  "QTreeWidget, QListWidget { border: 1px solid #d5d9dd; background: #ffffff; } "
                  "QDockWidget::title { background: #e9ecef; padding: 5px; } "
                  "QGroupBox { font-weight: 600; margin-top: 10px; border: 1px solid #d5d9dd; } "
                  "QGroupBox::title { subcontrol-origin: margin; left: 7px; padding: 0 3px; }");

    auto* editMenu = menuBar()->addMenu("&Edit");
    auto* viewMenu = menuBar()->addMenu("&View");
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
    commandButton(m_centerline,drawGrid,0,2);commandButton(m_construction,drawGrid,1,2);
    commandButton(m_point,drawGrid,2,2);commandButton(m_polygon,drawGrid,0,3);
    m_polygon->setToolTip("Regular hexagon: click the center, then set size and orientation");
    commandGroup(sketchButtons, "Modify", modifyGrid);
    const QList<QPair<QString, const char*>> modifyCommands = {
        {"Trim Entities", "actionTrimEntities"}, {"Convert Entities", "actionConvertEntities"},
        {"Offset Entities", "actionOffsetEntities"}, {"Mirror Entities", "actionMirrorEntities"},
        {"Linear Sketch Pattern", "actionLinearSketchPattern"}, {"Move Entities", "actionMoveEntities"}};
    for (int i = 0; i < modifyCommands.size(); ++i)
        commandButton(unavailable(modifyCommands[i].first, modifyCommands[i].second), modifyGrid, i % 3, i / 3);
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
    insertMenu->addAction(m_createSketch);
    insertMenu->addAction(m_extrude);
    insertMenu->addAction(m_cut);
    toolsMenu->addAction(m_selectEntity);
    auto* tolerance=toolsMenu->addAction("Sketch Picking Tolerance...");
    tolerance->setObjectName("actionSketchPickingTolerance");
    connect(tolerance,&QAction::triggered,this,[this]{
        bool accepted=false;
        const double pixels=QInputDialog::getDouble(this,"Sketch Picking Tolerance","Tolerance (logical pixels)",
            m_viewport->pickingTolerance(),1,32,1,&accepted);
        if(accepted)m_viewport->setPickingTolerance(pixels);
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
    historySplitter->addWidget(geometryPanel);
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
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { editSelectedFeature(); });
    connect(m_editFeature, &QAction::triggered, this, [this] { editSelectedFeature(); });
    connect(m_entities, &QListWidget::itemDoubleClicked, this, [this] { editSketchEntity(); });
    connect(m_entities, &QListWidget::itemSelectionChanged, this, [this] {
        updateActions();
        updateEntityProperties();
        if (m_editSketch && m_entities->currentRow() >= 0)
            m_viewport->selectSketchEntity(QString::fromStdString(m_editSketch->entities.at(std::size_t(m_entities->currentRow())).id));
    });
    connect(m_applyProperties, &QPushButton::clicked, this, [this] {
        if (!m_editSketch || m_entities->currentRow() < 0) return;
        auto entity = m_editSketch->entities.at(std::size_t(m_entities->currentRow()));
        entity.first = {m_propertyFirstX->value(), m_propertyFirstY->value()};
        if (entity.kind == modeling::EntityKind::Circle) entity.radius = m_propertyRadius->value();
        else if(entity.kind!=modeling::EntityKind::Point)entity.second = {m_propertySecondX->value(), m_propertySecondY->value()};
        applySketchEntityEdit(entity);
    });
    connect(m_viewport, &CadViewport::sketchEntitySelected, this, [this](const QString& id) {
        if (!m_editSketch) return;
        constraintEntitySelected(id);
        for (std::size_t i = 0; i < m_editSketch->entities.size(); ++i) {
            if (m_editSketch->entities[i].id == id.toStdString()) {
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
    connect(m_viewport,&CadViewport::sketchSelectionChanged,this,[this]{updateActions();});
    connect(m_viewport,&CadViewport::sketchDeleteRequested,this,[this]{deleteSketchEntity();});
    connect(m_viewport,&CadViewport::sketchToolChanged,this,&MainWindow::updateSketchToolActions);
    connect(m_viewport,&CadViewport::constraintPickingCanceled,this,[this]{closeConstraints();closeSmartDimension();});
    connect(m_viewport,&CadViewport::dimensionPlacementRequested,this,[this](modeling::Point2 point){placeSmartDimension(point);});
    connect(m_viewport,&CadViewport::dimensionReferencePicked,this,[this](const QString& id,int role){smartReferencePicked(id,role);});
    connect(m_viewport,&CadViewport::dimensionPointerMoved,this,[this](modeling::Point2 point){updateSmartPreview(point);});
    connect(m_viewport,&CadViewport::dimensionVariantRequested,this,[this](bool radius){if(m_smartDimension && !m_smartDimension->editor){m_smartDimension->radius=radius;if(m_smartDimension->pending)updateSmartPreview(modeling::dimensionAnnotationPosition(*m_editSketch,*m_smartDimension->pending));}});
    connect(m_viewport,&CadViewport::dimensionEditRequested,this,[this](const QString& id){if(m_editSketch)editSmartDimension(id.toStdString());});
    connect(m_viewport, &CadViewport::sketchChanged, this, [this](const modeling::Sketch& sketch) {
        if (!m_editSketch) return;
        try {
            modeling::validateSketchRelations(sketch);
            (void)modeling::sketchShape(sketch);
            acceptConstrainedSketch(sketch);
            m_viewport->setSketch(*m_editSketch);
            setWindowModified(true);
            updateEntityList();
            updateActions();
        } catch (const std::exception& error) {
            m_viewport->setSketch(*m_editSketch);
            reportError(QString::fromUtf8(error.what()));
        }
    });
    connect(m_viewport, &CadViewport::sketchRegionClicked, this, [this](modeling::Point2 point) { operationRegionClicked(point); });
    connect(m_viewport, &CadViewport::referencePlaneSelected, this, [this](modeling::SketchPlane plane) { referencePlaneSelected(plane); });
    connect(m_viewport,&CadViewport::solidFaceSelected,this,[this](int index){
        if(m_editSketch || m_operation)return;
        m_selectedFace=0;m_selectedPlane.reset();m_selectedId.clear();
        {const QSignalBlocker blocker(m_tree);m_tree->clearSelection();}
        m_viewport->setSelectedReferencePlane({});updateActions();
        if(index==0)return;
        try {(void)m_document.sketchOnFace(index);}catch(const std::exception& error){m_selectedFace=0;reportError(QString::fromUtf8(error.what()));return;}
        m_selectedPlane.reset();m_selectedFace=index;m_viewport->setSelectedReferencePlane({});
        if(m_selectingPlane)createSketchOnFace(index);
        else statusBar()->showMessage("Planar face selected. Click Sketch to create an attached sketch.");
    });
    connect(m_viewport, &CadViewport::planeSelectionCanceled, this, [this] { planeSelectionCanceled(); });

    auto* cancelFeature = new QAction("Cancel feature", this);
    cancelFeature->setObjectName("actionCancelFeature");cancelFeature->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(cancelFeature);connect(cancelFeature,&QAction::triggered,this,[this]{finishOperation(false);});
    statusBar()->showMessage("Orbit: left drag | Pan: middle / right drag | Zoom: wheel | Fit: F");
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
    closeSmartDimension();
    if (m_operation) {
        auto* panel = m_operation->panel;
        m_operation.reset();
        delete panel;
    }
    delete takeCentralWidget();
    m_viewport = nullptr;
}

const document::PartDocument& MainWindow::document() const { return m_document; }
QString MainWindow::currentPath() const { return m_path; }

QString MainWindow::chooseOpenPath()
{
    return QFileDialog::getOpenFileName(this, "Open TheCAD part", m_path,
        "TheCAD Part (*.thepart *.tcpart);;TheCAD Part (*.thepart);;Legacy TheCAD Part (*.tcpart)");
}

QString MainWindow::chooseSavePath()
{
    QString suggestion = m_path.isEmpty() ? "Untitled.thepart" : m_path;
    if (legacyPath(suggestion)) {
        suggestion = QFileInfo(suggestion).absolutePath() + "/" + QFileInfo(suggestion).completeBaseName() + ".thepart";
    }
    QFileDialog dialog(this, "Save TheCAD part", suggestion, "TheCAD Part (*.thepart)");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix("thepart");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
    return dialog.selectedFiles().first();
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
    try {
        auto candidate = document::PartDocument::load(path);
        if (!maybeSave()) return;
        candidate = document::PartDocument::load(path);
        if (replaceDocument(std::move(candidate), QFileInfo(path).absoluteFilePath())) setWindowModified(false);
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

bool MainWindow::saveDocument()
{
    if (!finishSketch()) return false;
    return m_path.isEmpty() || legacyPath(m_path) ? saveDocumentAs() : saveToPath(m_path);
}

bool MainWindow::saveDocumentAs()
{
    if (!finishSketch()) return false;
    QString path = chooseSavePath();
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += ".thepart";
    if (QFileInfo(path).suffix().compare("thepart", Qt::CaseInsensitive) != 0) {
        reportError("Save parts as .thepart. Legacy .tcpart files are read-only inputs; assemblies and drawings are not implemented.");
        return false;
    }
    return saveToPath(path);
}

bool MainWindow::saveToPath(const QString& path)
{
    try {
        m_document.save(path);
        m_path = QFileInfo(path).absoluteFilePath();
        setWindowModified(false);m_savedRevision=m_revision;
        updateDocumentUi();
        statusBar()->showMessage("Saved " + QDir::toNativeSeparators(m_path), 4000);
        return true;
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); return false; }
}

bool MainWindow::replaceDocument(document::PartDocument candidate, const QString& path)
{
    if (!m_viewport->setShape(candidate.shape())) {
        reportError("The part could not be displayed. The current document has been retained.");
        return false;
    }
    m_selectingPlane = false;
    m_selectedPlane.reset();
    m_viewport->cancelPlaneSelection();
    m_viewport->setSelectedReferencePlane({});
    m_viewport->endSketch();
    m_editSketch.reset();
    m_editSketchId.clear();
    m_selectedId.clear();
    m_document = std::move(candidate);
    m_mode = WorkspaceMode::Part;
    m_selectedFace=0;
    m_path = path;
    m_editHistory.clear();m_revision=0;m_nextRevision=0;m_savedRevision=0;
    updateDocumentUi();
    m_viewport->showReferencePlanes(m_document.features().empty());
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
    m_document = std::move(candidate);
    m_selectedId = selectedId;
    setWindowModified(true);
    updateDocumentUi();
    return true;
}

const document::Feature* MainWindow::selectedFeature() const
{
    for (const auto& feature : m_document.features()) if (feature.id == m_selectedId) return &feature;
    return nullptr;
}

void MainWindow::selectFeature(const std::string& id)
{
    m_selectedId = id;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString().toStdString() == id) {
            m_tree->setCurrentItem(*it);
            m_tree->scrollToItem(*it);
            return;
        }
    }
}

void MainWindow::selectionChanged()
{
    m_selectedFace=0;
    const auto* item = m_tree->currentItem();
    const QString selection = item ? item->data(0, Qt::UserRole).toString() : QString{};
    if (!m_editSketch && !m_operation && selection.startsWith("origin:")) {
        const QString suffix = selection.mid(7);
        if (suffix == "XY") m_selectedPlane = modeling::SketchPlane::XY;
        else if (suffix == "XZ") m_selectedPlane = modeling::SketchPlane::XZ;
        else if (suffix == "YZ") m_selectedPlane = modeling::SketchPlane::YZ;
        else m_selectedPlane.reset();
        m_selectedId.clear();
        m_viewport->setSelectedReferencePlane(m_selectedPlane);
        m_viewport->showReferencePlanes(true);
        statusBar()->showMessage(m_selectedPlane
            ? QString("%1 selected. Click Sketch to begin on this reference plane.").arg(modeling::referencePlaneName(*m_selectedPlane))
            : QString("Select Front, Top or Right in the viewport, then click Sketch."));
    } else {
        m_selectedId = selection.toStdString();
        m_selectedPlane.reset();
        m_viewport->setSelectedReferencePlane({});
        if (!m_editSketch) {
            const auto* feature = selectedFeature();
            m_viewport->endSketch();
            m_viewport->showReferencePlanes(false);
            if (feature && feature->kind == document::FeatureKind::Sketch) m_viewport->setSketch(feature->sketch);
        }
    }
    updateEntityList();
    updateActions();
}
void MainWindow::updateDocumentUi()
{
    const QString name = m_path.isEmpty() ? "Untitled.thepart" : QFileInfo(m_path).fileName();
    setWindowTitle(name + "[*] - TheCAD");
    setWindowFilePath(m_path);
    m_dimensions->setText(QString("%1 features | mm%2").arg(m_document.features().size()).arg(m_mode==WorkspaceMode::FeatureDraft ? " | Feature preview" : m_mode==WorkspaceMode::EditingSketch ? " | Editing sketch" : ""));
    {
        const QSignalBlocker blocker(m_tree);
        // A shared sketch has several tree entries, all referring to one history ID.
        // Remember its parent too so refreshes retain the selected occurrence.
        const auto* current = m_tree->currentItem();
        const QString currentId = current ? current->data(0, Qt::UserRole).toString() : QString{};
        const QString parentId = current && current->parent()
            ? current->parent()->data(0, Qt::UserRole).toString() : QString{};
        std::map<QString, bool> expanded;
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->childCount()) expanded[(*it)->data(0, Qt::UserRole).toString()] = (*it)->isExpanded();
        m_tree->clear();
        auto* part = new QTreeWidgetItem(m_tree, {name});
        part->setData(0, Qt::UserRole, "part:");
        part->setIcon(0, commandIcon("Part"));
        part->setExpanded(expanded.count("part:") ? expanded.at("part:") : true);
        for (const auto plane : {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ}) {
            const QString key = plane == modeling::SketchPlane::XY ? "origin:XY"
                : plane == modeling::SketchPlane::XZ ? "origin:XZ" : "origin:YZ";
            auto* reference = new QTreeWidgetItem(part, {QString("%1 Plane").arg(modeling::referencePlaneName(plane))});
            reference->setIcon(0, commandIcon("Plane"));
            reference->setData(0, Qt::UserRole, key);
            reference->setToolTip(0, QString("%1 origin reference plane").arg(modeling::referencePlaneName(plane)));
            if (m_selectedPlane && *m_selectedPlane == plane) m_tree->setCurrentItem(reference);
        }
        auto* origin = new QTreeWidgetItem(part, {"Origin"});
        origin->setData(0, Qt::UserRole, "origin:");
        origin->setIcon(0, commandIcon("Origin"));

        std::map<std::string, const document::Feature*> sketches;
        std::set<std::string> usedSketches;
        for (const auto& feature : m_document.features()) {
            if (feature.kind == document::FeatureKind::Sketch) sketches.emplace(feature.id, &feature);
            if (feature.kind == document::FeatureKind::Extrude || feature.kind == document::FeatureKind::Cut)
                usedSketches.insert(feature.sketchId);
        }
        QTreeWidgetItem* selectedItem = nullptr;
        const auto addFeature = [&](QTreeWidgetItem* parent, const document::Feature& feature) {
            auto* item = new QTreeWidgetItem(parent, {QString::fromStdString(feature.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(feature.id));
            item->setToolTip(0, QString::fromStdString(feature.name));
            item->setIcon(0, commandIcon(feature.kind == document::FeatureKind::Sketch ? "Sketch"
                : feature.kind == document::FeatureKind::Cut ? "Extruded Cut" : "Extrude"));
            if (feature.id == m_selectedId && (!selectedItem ||
                (currentId == QString::fromStdString(feature.id) && parentId == parent->data(0, Qt::UserRole).toString())))
                selectedItem = item;
            return item;
        };
        for (const auto& feature : m_document.features()) {
            if (feature.kind == document::FeatureKind::Sketch && usedSketches.count(feature.id)) continue;
            auto* item = addFeature(part, feature);
            if (feature.kind == document::FeatureKind::Extrude || feature.kind == document::FeatureKind::Cut) {
                const auto source = sketches.find(feature.sketchId);
                if (source != sketches.end()) addFeature(item, *source->second);
            }
            const auto key = QString::fromStdString(feature.id);
            item->setExpanded(expanded.count(key) ? expanded.at(key) : true);
        }
        if (selectedItem) m_tree->setCurrentItem(selectedItem);
        else if (currentId == "part:" && !m_selectedPlane) m_tree->setCurrentItem(part);
        else if (currentId == "origin:" && !m_selectedPlane) m_tree->setCurrentItem(origin);
    }
    updateEntityList();
    updateActions();
}

void MainWindow::updateEntityList()
{
    const QSignalBlocker blocker(m_entities);
    const int selectedRow = m_entities->currentRow();
    m_entities->clear();
    const auto* feature = selectedFeature();
    const modeling::Sketch* sketch = m_editSketch ? &*m_editSketch
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
    const bool editing=m_mode==WorkspaceMode::EditingSketch && !m_constraints && !m_operation;
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
    const bool operation = bool(m_operation);
    const bool editing = m_mode == WorkspaceMode::EditingSketch && !m_constraints;
    findChild<QAction*>("actionSmartDimension")->setEnabled(editing && !operation);
    findChild<QAction*>("actionDisplayDeleteRelations")->setEnabled(editing && !operation);
    const bool selecting = m_selectingPlane;
    m_tabs->setEnabled(!operation);
    findChild<QAction*>("actionCancelFeature")->setEnabled(operation);
    m_tree->setEnabled(!operation && !editing && !selecting);
    for (const auto* name : {"actionNew", "actionOpen", "actionSave", "actionSaveAs"}) {
        if (auto* action = findChild<QAction*>(name)) action->setEnabled(!operation && !selecting && !m_constraints);
    }
    m_createSketch->setEnabled(!m_editSketch && !operation && !selecting);
    m_selectEntity->setEnabled(editing && !operation);
    m_line->setEnabled(editing && !operation);
    m_rectangle->setEnabled(editing && !operation);
    m_circle->setEnabled(editing && !operation);
    for(auto* tool:{m_point,m_centerline,m_polygon})tool->setEnabled(editing && !operation);
    const auto selectedEntity=m_viewport->selectedSketchEntity();
    const auto selectedLine=m_editSketch?std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==selectedEntity;}):std::vector<modeling::SketchEntity>::const_iterator{};
    const bool lineSelected=m_editSketch && selectedLine!=m_editSketch->entities.end() && selectedLine->kind==modeling::EntityKind::Line;
    m_construction->setEnabled(editing && !operation && lineSelected);m_construction->setChecked(lineSelected && selectedLine->construction);
    m_finishSketch->setEnabled(editing && !operation);
    const auto* selected = selectedFeature();
    m_editFeature->setEnabled(!m_editSketch && !operation && !selecting && selected);
    m_editEntity->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    m_deleteEntity->setEnabled(editing && !operation && (m_entities->currentRow()>=0 || !m_viewport->selectedSketchDimensions().empty() || !m_viewport->selectedSketchEntities().empty()));
    m_applyProperties->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    updateEntityProperties();
    bool profile = editing && !operation;
    if (!editing && !operation && !selecting) {
        for (const auto& feature : m_document.features()) {
            if (feature.kind != document::FeatureKind::Sketch) continue;
            try { profile = !modeling::closedContours(feature.sketch).empty(); }
            catch (const std::exception&) { }
            if (profile) break;
        }
    }
    m_extrude->setEnabled(profile && !m_constraints && !m_smartDimension);
    m_cut->setEnabled(profile && !m_constraints && !m_smartDimension && !m_document.shape().IsNull());
}
void MainWindow::createSketch()
{
    if (m_editSketch || m_operation || m_selectingPlane) return;
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
        auto sketch=m_document.sketchOnFace(index);auto candidate=m_document;
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
        auto candidate = m_document;
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
    if (m_editSketch || m_operation) return;
    if (m_selectingPlane) {
        createSketchOnPlane(plane);
        return;
    }
    m_selectedPlane = plane;
    m_viewport->setSelectedReferencePlane(plane);
    m_selectedId.clear();
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
    m_editSketch = feature.sketch;
    m_editSketchId = feature.id;
    m_selectedId = feature.id;
    m_viewport->beginSketch(*m_editSketch);
    m_viewport->setSketchTool(modeling::EntityKind::Line);
    m_tabs->setCurrentIndex(1);
    updateDocumentUi();
    statusBar()->showMessage("Line: click to start, click successive endpoints | Esc ends chain / Select picks geometry | Middle/right drag pans");
}

bool MainWindow::finishSketch()
{
    if(m_constraints)return false;
    closeSmartDimension();
    if (!m_editSketch) return true;
    try {
        auto candidate = m_document;
        auto feature = *std::find_if(candidate.features().begin(), candidate.features().end(),
                                    [this](const auto& item) { return item.id == m_editSketchId; });
        feature.sketch = *m_editSketch;
        candidate.replaceFeature(feature);
        const auto id = m_editSketchId;
        if (!applyModel(std::move(candidate), id,false)) return false;
        m_editSketch.reset();
        m_editSketchId.clear();
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
    const modeling::Sketch* sketch = m_editSketch ? &*m_editSketch
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
    if (!m_editSketch) return;
    try {
        auto candidate = modeling::editSketchEntity(*m_editSketch, entity);
        (void)modeling::sketchShape(candidate);
        acceptConstrainedSketch(candidate);
        m_viewport->setSketch(*m_editSketch);
        setWindowModified(true);
        updateEntityList();
        updateActions();
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::editSketchEntity()
{
    if (!m_editSketch || m_entities->currentRow() < 0) return;
    auto entity = m_editSketch->entities.at(std::size_t(m_entities->currentRow()));
    if (chooseEntityParameters(entity)) applySketchEntityEdit(entity);
}

void MainWindow::deleteSketchEntity()
{
    if(!m_editSketch || m_operation || m_constraints || (m_smartDimension && m_smartDimension->editor))return;
    auto entities=m_viewport->selectedSketchEntities();const auto dimensions=m_viewport->selectedSketchDimensions();
    if(entities.empty() && dimensions.empty() && m_entities->currentRow()>=0)
        entities.push_back(m_editSketch->entities.at(std::size_t(m_entities->currentRow())).id);
    if(entities.empty() && dimensions.empty())return;
    try {
        auto candidate=*m_editSketch;
        candidate.dimensions.erase(std::remove_if(candidate.dimensions.begin(),candidate.dimensions.end(),[&](const auto& d){
            return std::find(dimensions.begin(),dimensions.end(),d.id)!=dimensions.end();
        }),candidate.dimensions.end());
        for(const auto& id:entities)candidate=modeling::removeSketchEntity(candidate,id);
        modeling::validateSketchRelations(candidate);(void)modeling::sketchShape(candidate);
        auto previous=editState();m_editSketch=std::move(candidate);rememberSketchChange(std::move(previous));
        closeSmartDimension();m_viewport->selectSketchTool();m_viewport->selectSketchEntity({});
        m_constraintPoint.reset();m_viewport->setSketch(*m_editSketch);
        setWindowModified(true);updateEntityList();updateActions();
        statusBar()->showMessage("Deleted selected sketch geometry/dimensions. Ctrl+Z restores them.",3000);
    }catch(const std::exception& error){reportError(QString::fromUtf8(error.what()));}
}
void MainWindow::editSelectedFeature()
{
    const auto* selected = selectedFeature();
    if (!selected || m_editSketch) return;
    if (selected->kind == document::FeatureKind::Sketch) { startSketchEditing(*selected); return; }
    auto feature = *selected;
    if (feature.kind == document::FeatureKind::Extrude || feature.kind == document::FeatureKind::Cut) {
        startOperation(feature, true);
        return;
    }
    if (!chooseFeatureParameters(feature)) return;
    try {
        auto candidate = m_document;
        candidate.replaceFeature(feature);
        applyModel(std::move(candidate), feature.id);
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::extrude(bool cut)
{
    if (m_operation || m_selectingPlane) return;
    document::Feature feature;
    feature.kind = cut ? document::FeatureKind::Cut : document::FeatureKind::Extrude;
    const auto* selected = selectedFeature();
    if (m_editSketch) feature.sketchId = m_editSketchId;
    else if (selected && selected->kind == document::FeatureKind::Sketch) feature.sketchId = selected->id;
    startOperation(feature, false);
}

void MainWindow::startOperation(document::Feature feature, bool editing)
{
    if(m_operation || m_selectingPlane) return;
    auto sourceDocument=m_document;
    if(m_editSketch) {
        m_viewport->selectSketchTool();
        try {
            if(modeling::closedContours(*m_editSketch).empty())
                throw std::runtime_error("The active sketch has no valid closed contour. Continue sketch editing and close a profile.");
            auto source=*std::find_if(sourceDocument.features().begin(),sourceDocument.features().end(),[this](const auto& item){return item.id==m_editSketchId;});
            source.sketch=*m_editSketch;sourceDocument.replaceFeature(source);
        } catch(const std::exception& error) {reportError(QString("Cannot start the feature: %1").arg(error.what()));return;}
    }
    m_operation=std::make_unique<OperationDraft>();auto& draft=*m_operation;
    draft.feature=std::move(feature);draft.editing=editing;
    draft.fromSketch=m_editSketch.has_value();draft.modifiedBefore=isWindowModified();
    draft.sourceDocument=std::move(sourceDocument);m_mode=WorkspaceMode::FeatureDraft;
    draft.panel=new QWidget(m_managerStack);draft.panel->setObjectName("operationPanel");
    auto* layout=new QVBoxLayout(draft.panel);layout->setContentsMargins(8,8,8,8);
    auto* title=new QLabel(draft.feature.kind==document::FeatureKind::Cut ? "Extruded Cut" : "Extrude");
    title->setObjectName("operationTitle");title->setStyleSheet("font-size: 14px; font-weight: 600;");layout->addWidget(title);
    draft.buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    draft.buttons->setObjectName("operationButtons");draft.buttons->button(QDialogButtonBox::Ok)->setText("Confirm");
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);layout->addWidget(draft.buttons);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);layout->addWidget(scroll);
    auto* content=new QWidget;scroll->setWidget(content);auto* settings=new QVBoxLayout(content);settings->setContentsMargins(0,0,0,0);
    auto* referenceForm=new QFormLayout;
    auto* startingReference=new QLabel("Sketch Plane");startingReference->setObjectName("operationStartingReference");
    referenceForm->addRow("Starting reference",startingReference);settings->addLayout(referenceForm);
    auto* directionGroup=new QGroupBox("Direction 1");auto* directionForm=new QFormLayout(directionGroup);
    auto* endCondition=new QLabel(draft.feature.throughAll ? "Through All" : "Blind");endCondition->setObjectName("operationEndCondition");
    directionForm->addRow("End condition",endCondition);
    draft.length=number(directionForm,"Depth",draft.feature.length,true);draft.length->setObjectName("featureLength");
    draft.direction=new QComboBox;draft.direction->setObjectName("featureDirection");
    draft.direction->addItems({"Positive sketch normal","Negative sketch normal"});
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
    auto* selectionGroup=new QGroupBox("Selected Contours");auto* selectionForm=new QFormLayout(selectionGroup);
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
    draft.contours->setSelectionMode(QAbstractItemView::MultiSelection);draft.contours->setMaximumHeight(150);
    selectionForm->addRow("Contour",draft.contours);
    auto* instructions=new QLabel("Click regions to add or remove them, or select multiple contours here. Inner boundaries form holes in their surrounding region.");
    instructions->setWordWrap(true);selectionForm->addRow(instructions);settings->addWidget(selectionGroup);
    draft.status=new QLabel;draft.status->setObjectName("operationStatus");draft.status->setWordWrap(true);settings->addWidget(draft.status);settings->addStretch();
    connect(draft.sketches,&QComboBox::currentIndexChanged,this,[this]{operationSketchChanged();});
    connect(draft.contours,&QListWidget::itemSelectionChanged,this,[this]{updateOperationPreview();});
    connect(draft.length,&QDoubleSpinBox::valueChanged,this,[this]{updateOperationPreview();});
    connect(draft.length->findChild<QLineEdit*>(),&QLineEdit::textChanged,this,[this]{
        if(m_operation && (!m_operation->length->hasAcceptableInput() || !m_operation->candidate)) updateOperationPreview();
    });
    connect(draft.direction,&QComboBox::currentIndexChanged,this,[this]{updateOperationPreview();});
    connect(reverse,&QPushButton::clicked,this,[this]{if(m_operation)m_operation->direction->setCurrentIndex(1-m_operation->direction->currentIndex());});
    connect(draft.through,&QCheckBox::toggled,this,[this,endCondition](bool through){
        if(!m_operation)return;endCondition->setText(through?"Through All":"Blind");m_operation->length->setEnabled(!through);updateOperationPreview();
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
            draft.status->setText("A saved region is unavailable. Reselect the intended regions explicitly.");
        }
        updateOperationPreview();
        if(draft.sketches->currentIndex()<0)draft.status->setText("The saved source sketch is unavailable. Select a replacement explicitly.");
        else if(draft.contours->selectedItems().isEmpty())draft.status->setText("The saved contour is unavailable. Select a replacement explicitly.");
    }
    m_managerStack->addWidget(draft.panel);m_managerStack->setCurrentWidget(draft.panel);
    m_historyDock->setWindowTitle("PropertyManager");updateDocumentUi();
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
        m_viewport->beginSketch(*m_editSketch);m_viewport->selectSketchTool();m_tabs->setCurrentIndex(1);
        setWindowModified(modifiedBefore);
    } else {
        if(accepted) {m_editSketch.reset();m_editSketchId.clear();}
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
    m_viewport->clearPreview();
    const QSignalBlocker blocker(draft.contours);
    draft.contours->clear();
    const auto sourceId = draft.sketches->currentData().toString().toStdString();
    const auto found = std::find_if(draft.sourceDocument->features().begin(), draft.sourceDocument->features().end(),
                                    [&sourceId](const auto& item) { return item.id == sourceId; });
    if (found == draft.sourceDocument->features().end()) {
        m_viewport->endContourSelection();
        draft.status->setText("Select a source sketch explicitly.");
        draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    try {
        m_viewport->beginContourSelection(found->sketch);
        draft.availableContours = modeling::closedContours(found->sketch);
        for (std::size_t i = 0; i < draft.availableContours.size(); ++i) {
            QVariantList ids;
            for (const auto& id : draft.availableContours[i].entityIds) ids.append(QString::fromStdString(id));
            auto* item=new QListWidgetItem(QString("Contour %1 (%2 boundary entities)").arg(i+1).arg(ids.size()),draft.contours);
            item->setData(Qt::UserRole,ids);
        }
        draft.contours->setCurrentRow(-1);draft.contours->clearSelection();
        draft.status->setText(draft.availableContours.empty()
            ? "This sketch has no valid closed contour. Finish a valid closed profile before using it."
            : "Select a contour explicitly to generate a preview.");
    } catch (const std::exception& error) { draft.status->setText(QString::fromUtf8(error.what())); }
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}

void MainWindow::updateOperationPreview()
{
    if (!m_operation) return;
    auto& draft = *m_operation;
    draft.candidate.reset();
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    if(draft.length->isEnabled() && !draft.length->hasAcceptableInput()) {
        m_viewport->clearPreview();draft.status->setText("Enter a valid positive depth in millimeters.");return;
    }
    {const QSignalBlocker blocker(draft.length);const QSignalBlocker editorBlocker(draft.length->findChild<QLineEdit*>());draft.length->interpretText();}
    const auto selected=draft.contours->selectedItems();
    if(selected.isEmpty()) {
        m_viewport->clearPreview();
        draft.status->setText("Select a contour explicitly to generate a preview.");
        return;
    }
    try {
        auto feature = draft.feature;
        feature.sketchId = draft.sketches->currentData().toString().toStdString();
        modeling::RegionReferences references;
        for(auto* item:selected)references.push_back(draft.availableContours[std::size_t(draft.contours->row(item))].entityIds);
        feature.contour.clear();feature.contours.clear();
        if(references.size()==1)feature.contour=references.front();else feature.contours=references;
        feature.length = draft.length->value();
        feature.direction = draft.direction->currentIndex() == 0 ? modeling::Direction::Positive : modeling::Direction::Negative;
        feature.throughAll = feature.kind == document::FeatureKind::Cut && draft.through->isChecked();
        auto candidate = *draft.sourceDocument;
        std::string id;
        if (draft.editing) {
            candidate.replaceFeature(feature);
            id = feature.id;
        } else if (feature.kind == document::FeatureKind::Cut) {
            id = candidate.addCutRegions(feature.sketchId, references, feature.length, feature.throughAll, feature.direction);
        } else {
            id = candidate.addExtrudeRegions(feature.sketchId, references, feature.length, feature.direction);
        }
        m_viewport->previewShape(candidate.shape());
        draft.candidate = std::move(candidate);
        draft.candidateFeatureId = id;
        draft.status->setText("Preview is valid. Confirm to commit this feature, or Cancel to retain the original part.");
        draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    } catch (const std::exception& error) {
        m_viewport->clearPreview();
        draft.status->setText(QString("Cannot generate preview: %1").arg(error.what()));
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
                draft.status->setText("Several contours contain this point. Choose the intended contour in the list.");
                return;
            }
            selected = int(i);
        }
        if (selected >= 0) {
            draft.contours->setCurrentRow(selected,QItemSelectionModel::NoUpdate);
            draft.contours->item(selected)->setSelected(!draft.contours->item(selected)->isSelected());
            updateOperationPreview();
        } else draft.status->setText("No valid closed contour contains this point. Click inside a region or choose it in the list.");
    } catch (const std::exception& error) { draft.status->setText(QString::fromUtf8(error.what())); }
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_operation) {
        reportError("Confirm or cancel the feature preview before closing this part.");
        event->ignore();
        return;
    }
    if (m_selectingPlane) m_viewport->cancelPlaneSelection();
    if (maybeSave()) { m_viewport->releaseGraphics(); event->accept(); }
    else event->ignore();
}

} // namespace thecad::ui
