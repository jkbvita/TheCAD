#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/Sketch.h"
#include "modeling/ReferencePlanes.h"

#include <QAction>
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
#include <QVBoxLayout>

#include <exception>
#include <algorithm>
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
    if (entity.kind == EntityKind::Circle) {
        return QString("Circle: center %1, radius %2 mm").arg(first).arg(entity.radius);
    }
    return QString("%1: %2 to (%3, %4)")
        .arg(entity.kind == EntityKind::Line ? "Line" : "Rectangle", first)
        .arg(entity.second.x).arg(entity.second.y);
}
}

struct MainWindow::OperationDraft {
    document::Feature feature;
    bool editing = false;
    QDialog* dialog = nullptr;
    QComboBox* sketches = nullptr;
    QComboBox* contours = nullptr;
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

    auto* modelingToolbar = addToolBar("Modeling");
    modelingToolbar->setObjectName("modelingToolbar");
    modelingToolbar->setMovable(false);
    m_tabs = new QTabWidget;
    m_tabs->setObjectName("modelingTabs");
    m_tabs->setMaximumHeight(88);
    m_tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    modelingToolbar->addWidget(m_tabs);
    auto* features = new QWidget;
    auto* featureButtons = new QHBoxLayout(features);
    auto* sketch = new QWidget;
    auto* sketchButtons = new QHBoxLayout(sketch);
    m_tabs->addTab(features, "FEATURES");
    m_tabs->addTab(sketch, "SKETCH");
    const auto modelingAction = [this](QHBoxLayout* layout, const QString& text,
                                      const char* name, auto callback) {
        auto* action = new QAction(text, this);
        action->setObjectName(name);
        auto* button = new QToolButton;
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        layout->addWidget(button);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    m_extrude = modelingAction(featureButtons, "Extrude", "actionExtrude", [this] { extrude(false); });
    m_cut = modelingAction(featureButtons, "Extruded Cut", "actionExtrudedCut", [this] { extrude(true); });
    featureButtons->addStretch();
    m_createSketch = modelingAction(sketchButtons, "Sketch", "actionCreateSketch", [this] { createSketch(); });
    m_line = modelingAction(sketchButtons, "Line", "actionLine", [this] { m_viewport->setSketchTool(modeling::EntityKind::Line); });
    m_rectangle = modelingAction(sketchButtons, "Rectangle", "actionRectangle", [this] { m_viewport->setSketchTool(modeling::EntityKind::Rectangle); });
    m_circle = modelingAction(sketchButtons, "Circle", "actionCircle", [this] { m_viewport->setSketchTool(modeling::EntityKind::Circle); });
    m_finishSketch = modelingAction(sketchButtons, "Finish Sketch", "actionFinishSketch", [this] { finishSketch(); });
    sketchButtons->addStretch();

    addToolBarBreak();
    auto* toolbar = addToolBar("View");
    toolbar->setMovable(false);
    auto* fit = toolbar->addAction("Fit all");
    fit->setObjectName("actionFitAll");
    fit->setShortcut(QKeySequence(Qt::Key_F));
    connect(fit, &QAction::triggered, m_viewport, &CadViewport::fitAll);
    auto* reset = toolbar->addAction("Isometric");
    reset->setObjectName("actionIsometric");
    reset->setShortcut(QKeySequence(Qt::Key_Home));
    connect(reset, &QAction::triggered, m_viewport, &CadViewport::resetView);

    auto* historyDock = new QDockWidget("Part history", this);
    historyDock->setObjectName("historyDock");
    historyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    historyDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* history = new QWidget;
    auto* historyLayout = new QVBoxLayout(history);
    m_tree = new QTreeWidget;
    m_tree->setObjectName("featureTree");
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(200);
    historyLayout->addWidget(m_tree);
    auto* editButton = new QToolButton;
    m_editFeature = new QAction("Edit selected feature", this);
    m_editFeature->setObjectName("actionEditFeature");
    editButton->setDefaultAction(m_editFeature);
    historyLayout->addWidget(editButton);
    historyLayout->addWidget(new QLabel("Sketch geometry (millimeters)"));
    m_entities = new QListWidget;
    m_entities->setObjectName("sketchEntities");
    historyLayout->addWidget(m_entities);
    auto* entityButtons = new QHBoxLayout;
    m_editEntity = modelingAction(entityButtons, "Edit geometry", "actionEditEntity", [this] { editSketchEntity(); });
    m_deleteEntity = modelingAction(entityButtons, "Remove", "actionDeleteEntity", [this] { deleteSketchEntity(); });
    historyLayout->addLayout(entityButtons);
    historyDock->setWidget(history);
    addDockWidget(Qt::LeftDockWidgetArea, historyDock);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { editSelectedFeature(); });
    connect(m_editFeature, &QAction::triggered, this, [this] { editSelectedFeature(); });
    connect(m_entities, &QListWidget::itemDoubleClicked, this, [this] { editSketchEntity(); });
    connect(m_entities, &QListWidget::itemSelectionChanged, this, [this] { updateActions(); });
    connect(m_viewport, &CadViewport::sketchEntityCreated, this, [this](const modeling::SketchEntity& entity) { addSketchEntity(entity); });
    connect(m_viewport, &CadViewport::sketchRegionClicked, this, [this](modeling::Point2 point) { operationRegionClicked(point); });
    connect(m_viewport, &CadViewport::referencePlaneSelected, this, [this](modeling::SketchPlane plane) { referencePlaneSelected(plane); });
    connect(m_viewport, &CadViewport::planeSelectionCanceled, this, [this] { planeSelectionCanceled(); });

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
    if (m_operation) {
        m_operation->dialog->disconnect(this);
        delete m_operation->dialog;
        m_operation.reset();
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
    else {
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
    else entity.second = {x2->value(), y2->value()};
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
        setWindowModified(false);
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
    m_path = path;
    updateDocumentUi();
    m_viewport->showReferencePlanes(m_document.features().empty());
    return true;
}

bool MainWindow::applyModel(document::PartDocument candidate, const std::string& selectedId)
{
    if (!m_viewport->setShape(candidate.shape())) {
        reportError("The regenerated geometry could not be displayed. The previous part was retained.");
        return false;
    }
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
    for (int i = 0; i < m_tree->topLevelItem(0)->childCount(); ++i) {
        auto* item = m_tree->topLevelItem(0)->child(i);
        if (item->data(0, Qt::UserRole).toString().toStdString() == id) { m_tree->setCurrentItem(item); return; }
    }
}

void MainWindow::selectionChanged()
{
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
    m_dimensions->setText(QString("%1 features | mm%2").arg(m_document.features().size()).arg(m_editSketch ? " | Editing sketch" : ""));
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
        auto* part = new QTreeWidgetItem(m_tree, {"Part"});
        for (const auto& feature : m_document.features()) {
            auto* item = new QTreeWidgetItem(part, {QString::fromStdString(feature.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(feature.id));
            item->setToolTip(0, QString::fromStdString(feature.id));
            if (feature.id == m_selectedId) m_tree->setCurrentItem(item);
        }
        part->setExpanded(true);
        auto* origin = new QTreeWidgetItem(m_tree, {"Origin"});
        origin->setData(0, Qt::UserRole, "origin:");
        for (const auto plane : {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ}) {
            const QString key = plane == modeling::SketchPlane::XY ? "origin:XY"
                : plane == modeling::SketchPlane::XZ ? "origin:XZ" : "origin:YZ";
            auto* reference = new QTreeWidgetItem(origin, {modeling::referencePlaneName(plane)});
            reference->setData(0, Qt::UserRole, key);
            reference->setToolTip(0, QString("%1 origin reference plane").arg(modeling::referencePlaneName(plane)));
            if (m_selectedPlane && *m_selectedPlane == plane) m_tree->setCurrentItem(reference);
        }
        origin->setExpanded(true);
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
    if (sketch) for (const auto& entity : sketch->entities) m_entities->addItem(entityText(entity));
    if (m_entities->count()) m_entities->setCurrentRow(std::min(std::max(selectedRow, 0), m_entities->count() - 1));
}

void MainWindow::updateActions()
{
    const bool operation = bool(m_operation);
    const bool editing = m_editSketch.has_value();
    const bool selecting = m_selectingPlane;
    m_tabs->setEnabled(!operation);
    m_tree->setEnabled(!operation && !editing && !selecting);
    for (const auto* name : {"actionNew", "actionOpen", "actionSave", "actionSaveAs"}) {
        if (auto* action = findChild<QAction*>(name)) action->setEnabled(!operation && !selecting);
    }
    m_createSketch->setEnabled(!editing && !operation && !selecting);
    m_line->setEnabled(editing && !operation);
    m_rectangle->setEnabled(editing && !operation);
    m_circle->setEnabled(editing && !operation);
    m_finishSketch->setEnabled(editing && !operation);
    const auto* selected = selectedFeature();
    m_editFeature->setEnabled(!editing && !operation && !selecting && selected);
    m_editEntity->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    m_deleteEntity->setEnabled(editing && !operation && m_entities->currentRow() >= 0);
    bool profile = false;
    if (!editing && !operation && !selecting) {
        for (const auto& feature : m_document.features()) {
            if (feature.kind != document::FeatureKind::Sketch) continue;
            try { profile = !modeling::closedContours(feature.sketch).empty(); }
            catch (const std::exception&) { }
            if (profile) break;
        }
    }
    m_extrude->setEnabled(profile);
    m_cut->setEnabled(profile && !m_document.shape().IsNull());
}
void MainWindow::createSketch()
{
    if (m_editSketch || m_operation || m_selectingPlane) return;
    if (m_selectedPlane) {
        createSketchOnPlane(*m_selectedPlane);
        return;
    }
    m_selectingPlane = true;
    m_viewport->beginPlaneSelection();
    updateActions();
    statusBar()->showMessage("Select Front, Top or Right in the viewport. Drag to orbit; Esc cancels.");
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
    m_editSketch = feature.sketch;
    m_editSketchId = feature.id;
    m_selectedId = feature.id;
    m_viewport->beginSketch(*m_editSketch);
    m_viewport->setSketchTool(modeling::EntityKind::Line);
    m_tabs->setCurrentIndex(1);
    updateDocumentUi();
    statusBar()->showMessage("Sketch: click two model points | Middle/right drag pans | Wheel zooms | Finish Sketch commits changes");
}

bool MainWindow::finishSketch()
{
    if (!m_editSketch) return true;
    try {
        auto candidate = m_document;
        auto feature = *std::find_if(candidate.features().begin(), candidate.features().end(),
                                    [this](const auto& item) { return item.id == m_editSketchId; });
        feature.sketch = *m_editSketch;
        candidate.replaceFeature(feature);
        const auto id = m_editSketchId;
        if (!applyModel(std::move(candidate), id)) return false;
        m_editSketch.reset();
        m_editSketchId.clear();
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

void MainWindow::addSketchEntity(const modeling::SketchEntity& entity)
{
    if (!m_editSketch) return;
    auto candidate = *m_editSketch;
    candidate.entities.push_back(entity);
    try {
        (void)modeling::sketchShape(candidate);
        m_editSketch = std::move(candidate);
        m_viewport->setSketch(*m_editSketch);
        setWindowModified(true);
        updateEntityList();
        updateActions();
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::editSketchEntity()
{
    if (!m_editSketch || m_entities->currentRow() < 0) return;
    const auto index = static_cast<std::size_t>(m_entities->currentRow());
    auto entity = m_editSketch->entities.at(index);
    if (!chooseEntityParameters(entity)) return;
    auto candidate = *m_editSketch;
    candidate.entities[index] = entity;
    try {
        (void)modeling::sketchShape(candidate);
        m_editSketch = std::move(candidate);
        m_viewport->setSketch(*m_editSketch);
        setWindowModified(true);
        updateEntityList();
        updateActions();
    } catch (const std::exception& error) { reportError(QString::fromUtf8(error.what())); }
}

void MainWindow::deleteSketchEntity()
{
    if (!m_editSketch || m_entities->currentRow() < 0) return;
    m_editSketch->entities.erase(m_editSketch->entities.begin() + m_entities->currentRow());
    m_viewport->setSketch(*m_editSketch);
    setWindowModified(true);
    updateEntityList();
    updateActions();
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
    if (m_editSketch || m_operation) return;
    document::Feature feature;
    feature.kind = cut ? document::FeatureKind::Cut : document::FeatureKind::Extrude;
    const auto* selected = selectedFeature();
    if (selected && selected->kind == document::FeatureKind::Sketch) feature.sketchId = selected->id;
    startOperation(feature, false);
}

void MainWindow::startOperation(document::Feature feature, bool editing)
{
    if (m_operation || m_editSketch) return;
    m_operation = std::make_unique<OperationDraft>();
    auto& draft = *m_operation;
    draft.feature = std::move(feature);
    draft.editing = editing;
    draft.dialog = new QDialog(this);
    draft.dialog->setObjectName("operationDialog");
    draft.dialog->setWindowTitle(draft.feature.kind == document::FeatureKind::Cut ? "Extruded Cut" : "Extrude");
    draft.dialog->setModal(false);
    auto* layout = new QVBoxLayout(draft.dialog);
    auto* instructions = new QLabel("Select a sketch, then click its closed region in the viewport or choose an explicit contour below.");
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    draft.sketches = new QComboBox;
    draft.sketches->setObjectName("operationSketch");
    draft.sketches->setPlaceholderText("Select a sketch explicitly");
    int selectedIndex = -1;
    for (const auto& item : m_document.features()) {
        // An edited operation may reference only sketches before it in history.
        if (editing && item.id == draft.feature.id) break;
        if (item.kind != document::FeatureKind::Sketch) continue;
        draft.sketches->addItem(QString::fromStdString(item.name), QString::fromStdString(item.id));
        if (item.id == draft.feature.sketchId) selectedIndex = draft.sketches->count() - 1;
    }
    form->addRow("Sketch", draft.sketches);
    draft.contours = new QComboBox;
    draft.contours->setObjectName("operationContour");
    draft.contours->setPlaceholderText("Select a contour explicitly");
    form->addRow("Contour", draft.contours);
    draft.length = number(form, draft.feature.kind == document::FeatureKind::Cut ? "Depth" : "Length", draft.feature.length, true);
    draft.length->setObjectName("featureLength");
    draft.direction = new QComboBox;
    draft.direction->setObjectName("featureDirection");
    draft.direction->addItems({"Positive sketch normal", "Negative sketch normal"});
    draft.direction->setCurrentIndex(draft.feature.direction == modeling::Direction::Positive ? 0 : 1);
    form->addRow("Direction", draft.direction);
    draft.through = new QCheckBox("Through all in selected direction", draft.dialog);
    draft.through->setObjectName("featureThroughAll");
    if (draft.feature.kind == document::FeatureKind::Cut) {
        form->addRow(draft.through);
        draft.through->setChecked(draft.feature.throughAll);
        draft.length->setEnabled(!draft.feature.throughAll);
    }
    draft.status = new QLabel;
    draft.status->setObjectName("operationStatus");
    draft.status->setWordWrap(true);
    layout->addWidget(draft.status);
    auto* preview = new QPushButton("Update preview");
    preview->setObjectName("operationPreview");
    layout->addWidget(preview);
    draft.buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    draft.buttons->setObjectName("operationButtons");
    draft.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(draft.buttons);
    connect(draft.sketches, &QComboBox::currentIndexChanged, this, [this] { operationSketchChanged(); });
    connect(draft.contours, &QComboBox::currentIndexChanged, this, [this] { updateOperationPreview(); });
    connect(draft.length, &QDoubleSpinBox::valueChanged, this, [this] { updateOperationPreview(); });
    connect(draft.direction, &QComboBox::currentIndexChanged, this, [this] { updateOperationPreview(); });
    connect(draft.through, &QCheckBox::toggled, this, [this](bool through) {
        if (m_operation) m_operation->length->setEnabled(!through);
        updateOperationPreview();
    });
    connect(preview, &QPushButton::clicked, this, [this] { updateOperationPreview(); });
    connect(draft.buttons, &QDialogButtonBox::rejected, draft.dialog, &QDialog::reject);
    connect(draft.buttons, &QDialogButtonBox::accepted, this, [this] {
        updateOperationPreview();
        if (!m_operation || !m_operation->candidate) return;
        m_viewport->clearPreview();
        if (applyModel(*m_operation->candidate, m_operation->candidateFeatureId)) m_operation->dialog->accept();
    });
    connect(draft.dialog, &QDialog::finished, this, [this] {
        m_viewport->clearPreview();
        m_viewport->endContourSelection();
        if (m_operation) {
            m_operation->dialog->deleteLater();
            m_operation.reset();
        }
        updateActions();
    });
    // A source may only be prefilled from an explicit tree selection or the
    // edited feature reference. Never silently choose the first available sketch.
    draft.sketches->setCurrentIndex(selectedIndex);
    operationSketchChanged();
    if (editing) {
        const auto expected = draft.feature.contour;
        for (int i = 0; i < int(draft.availableContours.size()); ++i) {
            auto actual = draft.availableContours[std::size_t(i)].entityIds;
            auto sortedExpected = expected;
            std::sort(actual.begin(), actual.end());
            std::sort(sortedExpected.begin(), sortedExpected.end());
            if (actual == sortedExpected) { draft.contours->setCurrentIndex(i); break; }
        }
        if (draft.sketches->currentIndex() < 0) draft.status->setText("The saved source sketch is unavailable. Select a replacement explicitly; nothing has been changed.");
        else if (draft.contours->currentIndex() < 0) draft.status->setText("The saved contour is unavailable. Select a replacement explicitly; nothing has been changed.");
    }
    updateActions();
    draft.dialog->show();
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
    const auto found = std::find_if(m_document.features().begin(), m_document.features().end(),
                                    [&sourceId](const auto& item) { return item.id == sourceId; });
    if (found == m_document.features().end()) {
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
            draft.contours->addItem(QString("Contour %1 (%2 entities)").arg(i + 1).arg(ids.size()), ids);
        }
        draft.contours->setCurrentIndex(-1);
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
    const int index = draft.contours->currentIndex();
    if (index < 0 || index >= int(draft.availableContours.size())) {
        m_viewport->clearPreview();
        draft.status->setText("Select a contour explicitly to generate a preview.");
        return;
    }
    try {
        auto feature = draft.feature;
        feature.sketchId = draft.sketches->currentData().toString().toStdString();
        feature.contour = draft.availableContours[std::size_t(index)].entityIds;
        feature.length = draft.length->value();
        feature.direction = draft.direction->currentIndex() == 0 ? modeling::Direction::Positive : modeling::Direction::Negative;
        feature.throughAll = feature.kind == document::FeatureKind::Cut && draft.through->isChecked();
        auto candidate = m_document;
        std::string id;
        if (draft.editing) {
            candidate.replaceFeature(feature);
            id = feature.id;
        } else if (feature.kind == document::FeatureKind::Cut) {
            id = candidate.addCut(feature.sketchId, feature.contour, feature.length, feature.throughAll, feature.direction);
        } else {
            id = candidate.addExtrude(feature.sketchId, feature.contour, feature.length, feature.direction);
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
    const auto found = std::find_if(m_document.features().begin(), m_document.features().end(),
                                   [&sourceId](const auto& item) { return item.id == sourceId; });
    if (found == m_document.features().end()) return;
    int selected = -1;
    try {
        for (std::size_t i = 0; i < draft.availableContours.size(); ++i) {
            if (!modeling::contourContains(draft.availableContours[i], found->sketch.plane, point)) continue;
            if (selected >= 0) {
                draft.status->setText("Several contours contain this point. Choose the intended contour in the list.");
                return;
            }
            selected = int(i);
        }
        if (selected >= 0) {
            draft.contours->setCurrentIndex(selected);
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
