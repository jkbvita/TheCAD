#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/MateIcons.h"
#include "ui/assembly/AssemblyViewport.h"
#include "ui/assembly/AssemblyMateEditor.h"
#include "ui/assembly/MateIcons.h"
#include "ui/assembly/AssemblyPropertyEditor.h"
#include "ui/assembly/AssemblyBeltEditor.h"
#include "ui/assembly/ComponentPlacementEditor.h"
#include "ui/MainWindow.h"
#include "ui/PropertyManagerPage.h"
#include "ui/Theme.h"
#include "ui/startup/WorkspaceLauncher.h"
#include "application/RecentDocuments.h"
#include "ui/PreferencesDialog.h"
#include "ui/widgets/CommandBar.h"
#include "ui/widgets/FeatureCommandBar.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <Standard_Failure.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <QAction>
#include <QTimer>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace thecad::ui
{
namespace
{
constexpr int kindRole = Qt::UserRole + 1, referenceRole = Qt::UserRole + 2;
QString q(const std::string& text)
{
    return QString::fromStdString(text);
}
bool sameReference(const document::AssemblyReference& a, const document::AssemblyReference& b)
{
    const auto& x = a.geometry;
    const auto& y = b.geometry;
    return a.componentId == b.componentId && a.connectorId == b.connectorId &&
           x.source == y.source &&
           (!x.topologyName.empty() ? x.topologyName == y.topologyName
                                    : x.featureId == y.featureId && x.entityId == y.entityId &&
                                          x.role == y.role && x.index == y.index);
}
} // namespace
AssemblyWindow::AssemblyWindow(QWidget* parent) : QMainWindow(parent),
    m_commands([this] { return commandContext(); })
{
    application::registerBuiltInCommands(m_commands.registry());
    m_commands.bindSessionControls();
    ThemeManager::instance();
    setObjectName("assemblyWindow");
    resize(1280, 850);
    m_viewport = new AssemblyViewport;
    setCentralWidget(m_viewport);
    auto* dock = new QDockWidget("FeatureManager", this);
    dock->setObjectName("assemblyManager");
    dock->setMinimumWidth(300);
    m_left = new QSplitter(Qt::Vertical);
    m_tree = new QTreeWidget;
    m_tree->setObjectName("assemblyTree");
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(17);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_panels = new QStackedWidget;
    m_panels->setObjectName("assemblyPropertyManager");
    m_placement = new ComponentPlacementEditor;
    m_panels->addWidget(m_placement);
    m_left->addWidget(m_tree);
    m_left->addWidget(m_panels);
    m_left->setSizes({650, 130});
    m_left->setStretchFactor(0, 1);
    m_left->setStretchFactor(1, 0);
    connect(m_placement, &ComponentPlacementEditor::layoutChanged, this,
            &AssemblyWindow::updateActions);
    dock->setWidget(m_left);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    buildCommands();
    bindCommandHandlers();
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &AssemblyWindow::selectTree);
    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int)
            {
                if (draftActive() || item->data(0, kindRole).toString() != "component")
                    return;
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                const bool visible = item->checkState(0) == Qt::Checked;
                // QTreeWidget still uses this item after itemChanged returns. Never
                // delete it by rebuilding the tree inside the model notification.
                QTimer::singleShot(0, this,
                                   [this, id, visible]
                                   {
                                       if (draftActive())
                                           return;
                                       const auto& components = document().components();
                                       const auto found =
                                           std::find_if(components.begin(), components.end(),
                                                        [&](const auto& c) { return c.id == id; });
                                       if (found != components.end() && found->visible != visible)
                                           mutate([&](auto& doc) { doc.setVisible(id, visible); });
                                   });
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int)
            {
                if (draftActive())
                    return;
                const auto kind = item->data(0, kindRole).toString();
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                if (kind == "belt")
                {
                    startWorkflow(Workflow::Belt, id);
                    return;
                }
                if (kind == "asset")
                {
                    startWorkflow(Workflow::Asset,
                                  item->data(0, referenceRole).toString().toStdString());
                    return;
                }
                if (kind == "ground")
                {
                    startWorkflow(Workflow::Ground);
                    return;
                }
                if (kind == "component")
                    for (const auto& part : document().inPlaceParts())
                        if (part.componentId == id)
                        {
                            startWorkflow(Workflow::InPlace, part.id);
                            return;
                        }
                if (!draftActive() && item->data(0, kindRole).toString() == "mate")
                    for (const auto& mate : document().mates())
                        if (q(mate.id) == item->data(0, Qt::UserRole).toString())
                        {
                            editMate(mate);
                            break;
                        }
            });
    connect(
        m_tree, &QTreeWidget::customContextMenuRequested, this,
        [this](QPoint point)
        {
            if (draftActive())
                return;
            auto* item = m_tree->itemAt(point);
            if (!item)
                return;
            if (!item->isSelected())
                m_tree->setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect);
            QMenu menu(this);
            workflowContextMenu(menu, item);
            if (item->data(0, kindRole).toString() == "component")
            {
                menu.addAction(m_fix);
                menu.addAction(m_hide);
            }
            if (item->data(0, kindRole).toString() == "mate")
            {
                auto* edit = menu.addAction("Edit mate");
                const auto id = item->data(0, Qt::UserRole).toString();
                connect(edit, &QAction::triggered, this,
                        [this, id]
                        {
                            for (const auto& mate : document().mates())
                                if (q(mate.id) == id)
                                {
                                    editMate(mate);
                                    break;
                                }
                        });
            }
            if (item->data(0, kindRole).toString() == "mate")
            {
                std::vector<std::string> ids;
                for (const auto* selected : m_tree->selectedItems())
                    if (selected->data(0, kindRole).toString() == "mate")
                        ids.push_back(selected->data(0, Qt::UserRole).toString().toStdString());
                auto* suppress = menu.addAction("Suppress selected mates");
                auto* unsuppress = menu.addAction("Unsuppress selected mates");
                connect(suppress, &QAction::triggered, this,
                        [this, ids]
                        {
                            mutate(
                                [&](auto& doc)
                                {
                                    doc.setMateStatus(ids,
                                                      modeling::assembly::MateStatus::Suppressed);
                                });
                        });
                connect(
                    unsuppress, &QAction::triggered, this,
                    [this, ids]
                    {
                        mutate([&](auto& doc)
                               { doc.setMateStatus(ids, modeling::assembly::MateStatus::Active); });
                    });
            }
            menu.addSeparator();
            menu.addAction(m_delete);
            menu.exec(m_tree->viewport()->mapToGlobal(point));
        });
    connect(m_placement, &ComponentPlacementEditor::applyRequested, this,
            &AssemblyWindow::applyPlacement);
    connect(m_viewport, &AssemblyViewport::initializationFailed, this,
            &AssemblyWindow::reportError);
    connect(m_viewport, &AssemblyViewport::entityPicked, this, &AssemblyWindow::pickedEntity);
    connect(m_viewport, &AssemblyViewport::dragStarted, this,
            [this]
            {
                m_dragCandidate = document();
                updateActions();
            });
    connect(m_viewport, &AssemblyViewport::dragPointRequested, this,
            [this](const QString& id, const gp_Pnt& local, const gp_Pnt& target,
                   const gp_Dir& normal, double tolerance, double rotationDeadband)
            {
                previewComponentDrag(
                    id, [&](auto& doc)
                    { doc.dragPoint(id.toStdString(), local, target, normal, tolerance, rotationDeadband); });
            });
    connect(m_viewport, &AssemblyViewport::dragRotationRequested, this,
            [this](const QString& id, const gp_Pnt& pivot, const gp_Quaternion& rotation)
            {
                previewComponentDrag(id, [&](auto& doc)
                    { doc.dragRotation(id.toStdString(), pivot, rotation); });
            });
    connect(m_viewport, &AssemblyViewport::dragFinished, this,
            [this](bool accepted)
            {
                if (!m_dragCandidate)
                    return;
                if (accepted)
                    m_session.commit(*m_dragCandidate);
                m_dragCandidate.reset();
                refresh();
            });
    connect(m_viewport, &AssemblyViewport::insertionMoved, this, &AssemblyWindow::moveInsertion);
    connect(m_viewport, &AssemblyViewport::insertionConfirmed, this,
            [this] { finishInsertion(true); });
    connect(m_viewport, &AssemblyViewport::canceled, this,
            [this]
            {
                if (draftActive())
                    cancelDraft();
                else
                    selectComponent({});
            });
    connect(&ThemeManager::instance(), &ThemeManager::changed, this,
            [this]
            {
                for (auto* action : findChildren<QAction*>())
                    if (!action->icon().isNull())
                        action->setIcon(commandIcon(action->text().remove('&')));
            });
    qApp->installEventFilter(this);
    refresh();
}
bool AssemblyWindow::draftActive() const
{
    return m_mate || m_workflow || m_insertPage || m_dragCandidate.has_value();
}
void AssemblyWindow::buildCommands()
{
    const auto action = [this](const char* name, const QString& text, auto callback)
    {
        auto* a = new QAction(commandIcon(text), text, this);
        a->setObjectName(name);
        a->setToolTip(text);
        connect(a, &QAction::triggered, this, callback);
        addAction(a);
        return a;
    };
    auto* file = menuBar()->addMenu("&File");
    m_new = action("actionNewAssembly", "New Assembly",
                   [this]
                   {
                       if (maybeSave())
                       {
                           m_session.reset();
                           m_preselection.clear();
                           m_selected.clear();
                           refresh();
                       }
                   });
    m_new->setShortcut(QKeySequence::New);
    file->addAction(m_new);
    file->addAction(action("actionNewPart", "New Part",
                           []
                           {
                               launchWorkspace(application::LaunchDocument::Part);
                           }));
    m_open = action("actionOpenAssembly", "Open Assembly...",
                    [this]
                    {
                        const auto path = chooseAssemblyPath(false);
                        if (!path.isEmpty())
                            openAssemblyFile(path);
                    });
    m_open->setShortcut(QKeySequence::Open);
    file->addAction(m_open);
    addWelcomeActions(file, this);
    m_save = action("actionSaveAssembly", "Save", [this] { save(); });
    m_save->setShortcut(QKeySequence::Save);
    file->addAction(m_save);
    m_saveAs = action("actionSaveAssemblyAs", "Save As...", [this] { save(true); });
    m_saveAs->setShortcut(QKeySequence::SaveAs);
    file->addAction(m_saveAs);
    file->addSeparator();
    file->addAction(action("actionCloseAssembly", "Close", [this] { close(); }));
    auto* edit = menuBar()->addMenu("&Edit");
    m_undo = action("actionUndo", "Undo",
                    [this] { m_commands.execute({"edit.undo"}); });
    m_undo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    edit->addAction(m_undo);
    m_redo = action("actionRedo", "Redo",
                    [this] { m_commands.execute({"edit.redo"}); });
    m_redo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    edit->addAction(m_redo);
    m_delete = action("actionDeleteAssemblySelection", "Delete", [this] { deleteSelected(); });
    m_delete->setShortcut(QKeySequence::Delete);
    edit->addAction(m_delete);
    m_fix = action("actionFixFloatComponent", "Fix / Float",
                   [this] { m_commands.execute({"assembly.fix_float"}); });
    edit->addAction(m_fix);
    m_hide =
        action("actionHideShowComponent", "Hide / Show",
               [this]
               {
                   if (!m_selected.empty())
                   {
                       const auto id = m_selected;
                       mutate([&](auto& doc) { doc.setVisible(id, !doc.component(id).visible); });
                   }
               });
    edit->addAction(m_hide);
    auto* preferences = edit->addAction("Preferences...");
    preferences->setObjectName("actionPreferences");
    connect(preferences, &QAction::triggered, this, [this] { showPreferences(this); });
    auto* insert = menuBar()->addMenu("&Insert");
    m_insertAction = action("actionInsertComponent", "Insert Components",
                            [this]
                            {
                                const auto path = chooseComponentPath();
                                if (!path.isEmpty())
                                    insertPartFile(path);
                            });
    m_insertAction->setIconText("Insert\nComponents");
    insert->addAction(m_insertAction);
    m_mateAction = action("actionMate", "Mate", [this] { startMate(); });
    insert->addAction(m_mateAction);
    m_beltAction =
        action("actionBeltChain", "Belt/Chain", [this] { startWorkflow(Workflow::Belt); });
    m_assetAction = action("actionAssetPublisher", "Asset Publisher",
                           [this] { startWorkflow(Workflow::Asset); });
    m_groundAction = action("actionAssemblyGroundPlane", "Ground Plane",
                            [this] { startWorkflow(Workflow::Ground); });
    m_contextAction = action("actionNewInContextPart", "New Part in Context",
                             [this]
                             {
                                 const auto path = chooseNewPartPath();
                                 if (path.isEmpty())
                                     return;
                                 application::CommandRequest request{"assembly.new_part"};
                                 request.parameters["path"] = path.toStdString();
                                 const auto result = m_commands.execute(request);
                                 if (result.state == application::CommandState::Rejected)
                                     reportError(QString::fromStdString(result.message));
                             });
    for (auto* command : {m_beltAction, m_assetAction, m_groundAction, m_contextAction})
        insert->addAction(command);
    m_magneticAction = action("actionMagneticMates", "Magnetic Mates", [] {});
    m_magneticAction->setCheckable(true);
    m_magneticAction->setChecked(QSettings().value("assembly/magneticEnabled", true).toBool());
    m_magneticAction->setToolTip("Snap published assets. Moving an unlocked asset disconnects its "
                                 "magnetic mate; lock the mate to keep it joined.");
    connect(m_magneticAction, &QAction::toggled, this,
            [](bool enabled) { QSettings().setValue("assembly/magneticEnabled", enabled); });
    m_magneticDistance = QSettings().value("assembly/magneticSnapDistance", 10.).toDouble();
    if (!std::isfinite(m_magneticDistance) || m_magneticDistance <= 0 || m_magneticDistance > 1e9)
        m_magneticDistance = 10;
    auto* snapDistance =
        action("actionMagneticSnapDistance", "Magnetic snap distance",
               [this]
               {
                   bool accepted = false;
                   const double value =
                       QInputDialog::getDouble(this, "Magnetic snap distance", "Distance (mm)",
                                               m_magneticDistance, 0.000001, 1e9, 6, &accepted);
                   if (accepted)
                   {
                       m_magneticDistance = value;
                       QSettings().setValue("assembly/magneticSnapDistance", value);
                   }
               });
    insert->addAction(m_magneticAction);
    insert->addAction(snapDistance);
    m_rebuild = action("actionRebuildAssembly", "Rebuild",
                       [this]
                       {
                           const auto result = m_commands.execute({"assembly.refresh_references"});
                           if (result.state == application::CommandState::Failed ||
                               result.state == application::CommandState::Rejected)
                               reportError(QString::fromStdString(result.message));
                       });
    m_rebuild->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    edit->addAction(m_rebuild);
    auto* view = menuBar()->addMenu("&View");
    auto* fit = action("actionFitAll", "Fit All", [this] { m_viewport->fitAll(); });
    fit->setShortcut(QKeySequence(Qt::Key_F));
    view->addAction(fit);
    const char* names[]{"Front", "Back", "Left", "Right", "Top", "Bottom", "Isometric"};
    for (int i = 0; i < 7; ++i)
    {
        auto* a = action((std::string("actionView") + names[i]).c_str(), names[i],
                         [this, i] { m_viewport->standardView(i); });
        a->setShortcut(QKeySequence(Qt::CTRL | int(Qt::Key_1 + i)));
        view->addAction(a);
    }
    auto* toolbar = addToolBar("Assembly commands");
    toolbar->setObjectName("assemblyToolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    auto* tabs = new QTabWidget;
    tabs->setTabPosition(QTabWidget::South);
    tabs->setFixedHeight(110);
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* page = new QWidget;
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(5, 1, 5, 1);
    row->setSpacing(3);
    row->addWidget(commandManagerButton(m_insertAction));
    row->addWidget(commandManagerButton(m_mateAction));
    auto* group = new QWidget;
    auto* grid = new QGridLayout(group);
    grid->setContentsMargins(5, 0, 5, 0);
    commandButton(m_rebuild, grid, 0, 0);
    row->addWidget(group);
    auto* workflows = new QWidget;
    workflows->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* workflowGrid = new QGridLayout(workflows);
    workflowGrid->setContentsMargins(5, 0, 5, 0);
    commandButton(m_beltAction, workflowGrid, 0, 0);
    commandButton(m_contextAction, workflowGrid, 1, 0);
    commandButton(m_magneticAction, workflowGrid, 2, 0);
    commandButton(m_assetAction, workflowGrid, 0, 1);
    commandButton(m_groundAction, workflowGrid, 1, 1);
    commandButton(snapDistance, workflowGrid, 2, 1);
    row->addWidget(workflows);
    row->addStretch();
    tabs->addTab(page, "Assembly");
    toolbar->addWidget(tabs);
}
QString AssemblyWindow::chooseComponentPath()
{
    return QFileDialog::getOpenFileName(this, "Insert Components", {},
                                        "TheCAD Parts (*.thepart *.tcpart)");
}
QString AssemblyWindow::chooseNewPartPath()
{
    const auto directory = m_session.path().isEmpty() ? QString{} : QFileInfo(m_session.path()).absolutePath();
    return QFileDialog::getSaveFileName(this, "New Part in Assembly",
                                        QDir(directory).filePath("NewPart.thepart"),
                                        "TheCAD Part (*.thepart)");
}
QString AssemblyWindow::chooseAssemblyPath(bool saving)
{
    return saving ? QFileDialog::getSaveFileName(this, "Save Assembly",
                                                 m_session.path().isEmpty() ? "Untitled.theasmb"
                                                                            : m_session.path(),
                                                 "TheCAD Assembly (*.theasmb)")
                  : QFileDialog::getOpenFileName(this, "Open Assembly", {},
                                                 "TheCAD Assembly (*.theasmb)");
}
void AssemblyWindow::reportError(const QString& message)
{
    QMessageBox::critical(this, "TheCAD Assembly", message);
}
bool AssemblyWindow::confirmDiscard()
{
    const auto choice = QMessageBox::warning(
        this, "Unsaved assembly", "Save the assembly before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && save());
}
bool AssemblyWindow::maybeSave()
{
    const bool untouched =
        m_session.path().isEmpty() && document().components().empty() && !m_session.canUndo();
    return (!m_session.modified() || untouched) ? true : confirmDiscard();
}
bool AssemblyWindow::confirmComponentDeletion(const QStringList& names, const QStringList& mates)
{
    return QMessageBox::question(
               this, "Delete components and mates",
               "Delete:\n" + names.join('\n') + "\n\nDependent mates and generated features:\n" +
                   mates.join('\n') + "\n\nExternal part files are retained.",
               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}
void AssemblyWindow::closeEvent(QCloseEvent* event)
{
    if (draftActive())
    {
        reportError("Confirm or cancel the assembly operation before closing.");
        event->ignore();
        return;
    }
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}
bool AssemblyWindow::openAssemblyFile(const QString& path)
{
    if (draftActive() || !maybeSave())
        return false;
    try
    {
        m_session.open(path);
        application::recordRecentDocument(path);
        m_preselection.clear();
        m_selected.clear();
        refresh();
        m_viewport->fitAll();
        if (document().sourcesChangedOnLoad())
            statusBar()->showMessage("Source parts changed. The assembly was rebuilt; save to "
                                     "record its current placements.");
        return true;
    }
    catch (const Standard_Failure& error)
    {
        reportError(QString::fromUtf8(error.GetMessageString()));
    }
    catch (const std::exception& error)
    {
        reportError(QString::fromUtf8(error.what()));
    }
    return false;
}
bool AssemblyWindow::save(bool saveAs)
{
    if (draftActive())
        return false;
    auto path = saveAs || m_session.path().isEmpty() ? chooseAssemblyPath(true) : m_session.path();
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".theasmb", Qt::CaseInsensitive))
        path += ".theasmb";
    try
    {
        m_session.save(path);
        application::recordRecentDocument(path);
        refresh();
        return true;
    }
    catch (const std::exception& error)
    {
        reportError(QString::fromUtf8(error.what()));
        return false;
    }
}
void AssemblyWindow::mutate(const std::function<void(document::AssemblyDocument&)>& operation)
{
    if (draftActive())
        return;
    try
    {
        auto candidate = document();
        operation(candidate);
        render(candidate);
        m_session.commit(std::move(candidate));
        refresh();
    }
    catch (const Standard_Failure& error)
    {
        render(document());
        reportError(QString::fromUtf8(error.GetMessageString()));
    }
    catch (const std::exception& error)
    {
        render(document());
        reportError(QString::fromUtf8(error.what()));
    }
}
void AssemblyWindow::render(const document::AssemblyDocument& doc)
{
    std::vector<AssemblyVisual> visuals;
    for (const auto& c : doc.components())
    {
        visuals.push_back({c.id, c.shape(), c.placement, c.visible, !doc.componentMovable(c.id),
                           m_insertCandidate && c.id == m_insertId});
        const auto dependents = doc.componentDependents({c.id});
        visuals.back().constrained =
            std::any_of(dependents.begin(), dependents.end(), [](const auto& mate)
                        { return mate.status == modeling::assembly::MateStatus::Active; });
    }
    m_viewport->setComponents(visuals);
    std::vector<AssemblyVisual> highlights;
    std::vector<document::AssemblyReference> refs;
    if (m_mate)
        refs = m_mate->references();
    else if (m_workflow)
        refs = m_workflow->references();
    else if ((m_tree->currentItem() &&
              m_tree->currentItem()->data(0, kindRole).toString() == "ground") ||
             m_preselection.size() > 1 ||
             (m_preselection.size() == 1 &&
              m_preselection[0].reference.geometry.source != modeling::ReferenceSource::BodyFace))
        for (const auto& option : m_preselection)
            refs.push_back(option.reference);
    for (const auto& ref : refs)
        for (const auto& option : doc.referenceOptions(ref.componentId))
            if (sameReference(option.reference, ref))
            {
                AssemblyVisual visual;
                visual.shape = option.shape;
                if (!ref.componentId.empty())
                    visual.placement = doc.component(ref.componentId).placement;
                visual.filled = ref.geometry.source == modeling::ReferenceSource::BodyFace &&
                                !(m_tree->currentItem() &&
                                  m_tree->currentItem()->data(0, kindRole).toString() == "ground");
                highlights.push_back(std::move(visual));
                break;
            }
    std::string beltId = m_selectedBelt;
    if (auto* editor = dynamic_cast<AssemblyBeltEditor*>(m_workflow))
        beltId = editor->definition().id;
    if (!beltId.empty())
        for (const auto& belt : doc.belts())
            if (belt.id == beltId)
            {
                const auto layout = doc.beltGeometry(belt);
                AssemblyVisual path;
                path.shape = modeling::sketchShape(layout.centerline);
                path.placement.SetDisplacement(gp_Ax3(), layout.frame);
                path.preview = true;
                highlights.push_back(std::move(path));
            }
    m_viewport->setReferenceHighlights(highlights);
}
void AssemblyWindow::refresh()
{
    if (std::none_of(document().belts().begin(), document().belts().end(),
                     [&](const auto& b) { return b.id == m_selectedBelt; }))
        m_selectedBelt.clear();
    std::vector<document::AssemblyReferenceOption> valid;
    for (const auto& old : m_preselection)
        try
        {
            for (const auto& option : document().referenceOptions(old.reference.componentId))
                if (sameReference(old.reference, option.reference))
                {
                    valid.push_back(option);
                    break;
                }
        }
        catch (const std::exception&)
        {
        }
    m_preselection = std::move(valid);
    render(document());
    refreshTree();
    const auto found = std::find_if(document().components().begin(), document().components().end(),
                                    [&](const auto& c) { return c.id == m_selected; });
    if (found == document().components().end())
        m_selected.clear();
    m_placement->setComponent(found == document().components().end() ? nullptr : &*found);
    const auto title = m_session.path().isEmpty() ? QString("Untitled.theasmb")
                                                  : QFileInfo(m_session.path()).fileName();
    setWindowTitle(title + "[*] — TheCAD Assembly");
    setWindowFilePath(m_session.path());
    setWindowModified(m_session.modified());
    statusBar()->showMessage(QString("%1 components · %2 mates · %3 degree(s) of freedom")
                                 .arg(document().components().size())
                                 .arg(document().mates().size())
                                 .arg(document().solution().degreesOfFreedom));
    updateActions();
    if (!draftActive() && !m_pendingSourceRefreshes.isEmpty() && !m_pendingSourceRefreshQueued)
    {
        m_pendingSourceRefreshQueued = true;
        QTimer::singleShot(0, this, [this] { refreshPendingLinkedParts(); });
    }
}
void AssemblyWindow::refreshPendingLinkedParts()
{
    m_pendingSourceRefreshQueued = false;
    if (draftActive() || m_pendingSourceRefreshes.isEmpty())
        return;
    QSet<QString> pending;
    pending.swap(m_pendingSourceRefreshes);
    for (const auto& path : pending)
        refreshLinkedPartSource(path);
}
void AssemblyWindow::refreshTree()
{
    const QSignalBlocker block(m_tree);
    QSet<QString> expanded;
    QString current;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
    {
        if ((*it)->isExpanded())
            expanded.insert((*it)->data(0, Qt::UserRole).toString());
        if ((*it) == m_tree->currentItem())
            current = (*it)->data(0, Qt::UserRole).toString();
    }
    m_tree->clear();
    m_treeReferences.clear();
    auto* root = new QTreeWidgetItem(
        m_tree,
        {m_session.path().isEmpty() ? "Untitled.theasmb" : QFileInfo(m_session.path()).fileName()});
    root->setData(0, kindRole, "root");
    root->setData(0, Qt::UserRole, "assembly-root");
    root->setExpanded(true);
    const auto references = [&](QTreeWidgetItem* parent, const std::string& id)
    {
        QTreeWidgetItem* sketches = nullptr;
        for (const auto& option : document().referenceOptions(id))
        {
            const auto source = option.reference.geometry.source;
            if (int(source) >= 6 || !option.reference.connectorId.empty())
                continue;
            auto* referenceParent = parent;
            if (source == modeling::ReferenceSource::SketchLine ||
                source == modeling::ReferenceSource::SketchPoint)
            {
                if (!sketches)
                {
                    sketches = new QTreeWidgetItem(parent, {"Sketch reference geometry"});
                    sketches->setData(0, Qt::UserRole, "sketch-references:" + q(id));
                    sketches->setData(0, kindRole, "folder");
                }
                referenceParent = sketches;
            }
            const int index = int(m_treeReferences.size());
            m_treeReferences.push_back(option);
            const auto label = option.label.section(" / ", -1);
            auto* child = new QTreeWidgetItem(
                referenceParent,
                {referenceParent == sketches ? option.label.section(" / ", 1) : label});
            child->setIcon(0, commandIcon(option.geometry.kind == modeling::DatumKind::Plane
                                              ? "Reference Plane"
                                          : option.geometry.kind == modeling::DatumKind::Point
                                              ? "Reference Point"
                                              : "Reference Axis"));
            child->setData(0, kindRole, "reference");
            child->setData(0, referenceRole, index);
            child->setData(0, Qt::UserRole,
                           QString("reference:%1:%2:%3:%4:%5")
                               .arg(q(id))
                               .arg(int(source))
                               .arg(source == modeling::ReferenceSource::Datum ||
                                            source == modeling::ReferenceSource::SketchPoint ||
                                            source == modeling::ReferenceSource::SketchLine
                                        ? q(option.reference.geometry.featureId)
                                        : QString::number(option.reference.geometry.index))
                               .arg(q(option.reference.geometry.entityId))
                               .arg(int(option.reference.geometry.role)));
        }
    };
    references(root, {});
    for (std::size_t i = 0; i < document().components().size(); ++i)
    {
        const auto& c = document().components()[i];
        const auto prefix = c.fixed                                     ? "(f) "
                            : document().solution().fullyConstrained[i] ? ""
                                                                        : "(-) ";
        auto* item = new QTreeWidgetItem(root, {QString::fromLatin1(prefix) + q(c.name)});
        item->setIcon(0, commandIcon("Part"));
        item->setData(0, Qt::UserRole, q(c.id));
        item->setData(0, kindRole, "component");
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, c.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, c.path);
        references(item, c.id);
        for (const auto& inPlace : document().inPlaceParts())
            if (inPlace.componentId == c.id)
            {
                auto* folder = new QTreeWidgetItem(item, {"Assembly References"});
                folder->setData(0, kindRole, "assembly-references");
                folder->setIcon(0, commandIcon("Reference Geometry"));
                QString supportName = "Assembly Origin";
                if (!inPlace.support.componentId.empty())
                    supportName = q(document().component(inPlace.support.componentId).name);
                QString geometryName = "Reference unavailable";
                for (const auto& option : document().referenceOptions(inPlace.support.componentId))
                    if (option.reference.connectorId == inPlace.support.connectorId &&
                        sameReference(option.reference, inPlace.support))
                    {
                        geometryName = option.label.section(" / ", -1);
                        break;
                    }
                auto* reference = new QTreeWidgetItem(
                    folder, {QString("%1 · %2 · %3")
                                 .arg(inPlace.linked ? "Associative" : "Local")
                                 .arg(supportName, geometryName)});
                reference->setToolTip(0, inPlace.linked
                                             ? "Stable assembly support that drives this component instance placement."
                                             : "Local placement. Changes to source geometry do not move this component.");
                folder->setExpanded(true);
            }
    }
    addWorkflowTree(root);
    auto* mates = new QTreeWidgetItem(root, {"Mates"});
    mates->setData(0, Qt::UserRole, "assembly-mates");
    mates->setData(0, kindRole, "folder");
    mates->setExpanded(true);
    for (const auto& mate : document().mates())
    {
        const auto status =
            mate.status == modeling::assembly::MateStatus::Suppressed ? " (suppressed)"
            : mate.status == modeling::assembly::MateStatus::Unsolved ? " (unsolved)"
                                                                      : "";
        auto* item = new QTreeWidgetItem(mates, {q(mate.name) + status});
        item->setIcon(0, assemblyMateIcon(mate.kind));
        item->setData(0, Qt::UserRole, q(mate.id));
        item->setData(0, kindRole, "mate");
        item->setToolTip(0, QString::fromUtf8(modeling::assembly::mateName(mate.kind)) + "\n" +
                                q(mate.diagnostic));
    }
    const auto desired = !m_selectedBelt.empty() ? q(m_selectedBelt)
                         : m_selected.empty()    ? current
                                                 : q(m_selected);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
    {
        const auto id = (*it)->data(0, Qt::UserRole).toString();
        if (expanded.contains(id))
            (*it)->setExpanded(true);
        if (id == desired)
            m_tree->setCurrentItem(*it);
    }
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, kindRole).toString() == "reference" ||
            (*it)->data(0, kindRole).toString() == "ground")
        {
            const auto& ref =
                m_treeReferences.at(std::size_t((*it)->data(0, referenceRole).toInt())).reference;
            if (std::any_of(m_preselection.begin(), m_preselection.end(), [&](const auto& option)
                            { return sameReference(option.reference, ref); }))
                (*it)->setSelected(true);
        }
}
void AssemblyWindow::selectComponent(const std::string& id, int face, TopAbs_ShapeEnum kind)
{
    if (m_magneticMovingId != id)
    {
        m_magneticMovingId = id;
        m_magneticMovingConnector = m_magneticStationaryConnector = -1;
        if (!id.empty())
            m_magneticRawPose = document().component(id).placement;
    }
    m_selectedBelt.clear();
    m_preselection.clear();
    m_viewport->setReferenceHighlights({});
    m_selected = id;
    m_viewport->setSelected(id, face, kind);
    {
        const QSignalBlocker block(m_tree);
        m_tree->clearSelection();
        m_tree->setCurrentItem(nullptr);
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString() == q(id))
            {
                m_tree->setCurrentItem(*it);
                break;
            }
    }
    m_placement->setComponent(id.empty() ? nullptr : &document().component(id));
    updateActions();
}
void AssemblyWindow::selectTree()
{
    auto* item = m_tree->currentItem();
    if (!item)
        return;
    if (m_workflow)
    {
        const auto kind = item->data(0, kindRole).toString();
        if (kind == "reference" || kind == "ground")
            m_workflow->pick(
                m_treeReferences.at(std::size_t(item->data(0, referenceRole).toInt())).reference);
        else if (kind == "component")
            m_workflow->pickComponent(item->data(0, Qt::UserRole).toString().toStdString());
        return;
    }
    if (m_mate)
    {
        if (item->data(0, kindRole).toString() == "reference" ||
            item->data(0, kindRole).toString() == "ground")
            m_mate->pick(
                m_treeReferences.at(std::size_t(item->data(0, referenceRole).toInt())).reference);
        else if (item->data(0, kindRole).toString() == "component")
            m_mate->pick({item->data(0, Qt::UserRole).toString().toStdString(), {}});
        return;
    }
    if (draftActive())
        return;
    m_selectedBelt = item->data(0, kindRole).toString() == "belt"
                         ? item->data(0, Qt::UserRole).toString().toStdString()
                         : std::string{};
    std::vector<document::AssemblyReferenceOption> selected;
    for (auto* selectedItem : m_tree->selectedItems())
        if (selectedItem->data(0, kindRole).toString() == "reference" ||
            selectedItem->data(0, kindRole).toString() == "ground")
            selected.push_back(
                m_treeReferences.at(std::size_t(selectedItem->data(0, referenceRole).toInt())));
    // Keep the user's selection order when Ctrl/Shift adds a second reference.
    std::vector<document::AssemblyReferenceOption> ordered;
    for (const auto& old : m_preselection)
    {
        const auto found = std::find_if(selected.begin(), selected.end(), [&](const auto& option)
                                        { return sameReference(old.reference, option.reference); });
        if (found != selected.end())
            ordered.push_back(*found);
        else if ((QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
                 int(old.reference.geometry.source) >= 6)
            ordered.push_back(old);
    }
    for (const auto& option : selected)
        if (std::none_of(ordered.begin(), ordered.end(), [&](const auto& old)
                         { return sameReference(old.reference, option.reference); }))
            ordered.push_back(option);
    m_preselection = std::move(ordered);
    if (item->data(0, kindRole).toString() == "component")
    {
        m_selected = item->data(0, Qt::UserRole).toString().toStdString();
        m_viewport->setSelected(m_selected);
        m_placement->setComponent(&document().component(m_selected));
    }
    else
    {
        m_selected.clear();
        m_viewport->setSelected({});
        m_placement->setComponent(nullptr);
    }
    render(document());
    updateActions();
}
document::AssemblyReferenceOption AssemblyWindow::faceReference(const std::string& id, int face,
                                                                TopAbs_ShapeEnum kind) const
{
    const auto& component = document().component(id);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(component.shape(), kind, faces);
    if (face > 0 && face <= faces.Extent())
        for (const auto& option : document().referenceOptions(id))
            if (option.shape.IsSame(faces(face)))
                return option;
    throw std::runtime_error(
        "Select a supported vertex, edge, face or reference in the component tree.");
}
void AssemblyWindow::picked(const QString& component, int face, Qt::KeyboardModifiers modifiers)
{
    pickedEntity(component, TopAbs_FACE, face, modifiers);
}
void AssemblyWindow::pickedEntity(const QString& component, TopAbs_ShapeEnum kind, int face,
                                  Qt::KeyboardModifiers modifiers)
{
    if (m_workflow)
    {
        if (!component.isEmpty())
            try
            {
                m_workflow->pick(faceReference(component.toStdString(), face, kind).reference);
            }
            catch (const std::exception& e)
            {
                m_workflow->page()->setStatus(QString::fromUtf8(e.what()), true);
            }
        return;
    }
    if (m_mate)
    {
        if (!component.isEmpty())
            try
            {
                m_mate->pick(faceReference(component.toStdString(), face, kind).reference);
            }
            catch (const std::exception& error)
            {
                m_mate->page()->setStatus(QString::fromUtf8(error.what()), true);
            }
        return;
    }
    if (!draftActive())
    {
        const bool additive = modifiers & (Qt::ControlModifier | Qt::ShiftModifier);
        auto selected =
            additive ? m_preselection : std::vector<document::AssemblyReferenceOption>{};
        if (!component.isEmpty())
            try
            {
                const auto option = faceReference(component.toStdString(), face, kind);
                const auto found =
                    std::find_if(selected.begin(), selected.end(), [&](const auto& old)
                                 { return sameReference(old.reference, option.reference); });
                if (found != selected.end())
                    selected.erase(found);
                else
                    selected.push_back(option);
            }
            catch (const std::exception&)
            {
            }
        if (component.isEmpty())
            selected.clear();
        auto selectedId = component.toStdString();
        int selectedFace = face;
        if (additive && selected.empty())
        {
            selectedId.clear();
            selectedFace = 0;
        }
        else if (additive && selected.size() == 1 &&
                 int(selected[0].reference.geometry.source) >= 6)
        {
            selectedId = selected[0].reference.componentId;
            TopTools_IndexedMapOfShape faces;
            kind = selected[0].shape.ShapeType();
            TopExp::MapShapes(document().component(selectedId).shape(), kind, faces);
            selectedFace = faces.FindIndex(selected[0].shape);
        }
        selectComponent(selectedId, selectedFace, kind);
        m_preselection = std::move(selected);
        if (m_preselection.size() > 1 ||
            (m_preselection.size() == 1 &&
             m_preselection[0].reference.geometry.source != modeling::ReferenceSource::BodyFace))
            m_viewport->setSelected({});
        render(document());
    }
}
void AssemblyWindow::previewComponentDrag(
    const QString& id, const std::function<void(document::AssemblyDocument&)>& operation)
{
    if (!m_dragCandidate)
        return;
    try
    {
        auto candidate = *m_dragCandidate;
        operation(candidate);
        m_magneticMovingId = id.toStdString();
        m_magneticRawPose = candidate.component(id.toStdString()).placement;
        if (m_magneticAction->isChecked())
            candidate.snapMagnetic(id.toStdString(), m_magneticDistance, m_magneticMovingConnector,
                                   m_magneticStationaryConnector);
        m_dragCandidate = std::move(candidate);
        render(*m_dragCandidate);
    }
    catch (const Standard_Failure& e)
    {
        statusBar()->showMessage(QString::fromUtf8(e.GetMessageString()));
    }
    catch (const std::exception& e)
    {
        statusBar()->showMessage(QString::fromUtf8(e.what()));
    }
}
void AssemblyWindow::applyPlacement()
{
    if (m_selected.empty() || draftActive())
        return;
    const auto id = m_selected;
    const auto placement = m_placement->placement();
    mutate([&](auto& doc) { doc.setPlacement(id, placement); });
}
bool AssemblyWindow::insertPartFile(const QString& path)
{
    if (draftActive())
        return false;
    try
    {
        auto candidate = document();
        const auto id = candidate.insertComponent(path, {}, false);
        m_preselection.clear();
        m_insertId = id;
        m_magneticMovingId = id;
        m_magneticRawPose = candidate.component(id).placement;
        m_magneticMovingConnector = m_magneticStationaryConnector = -1;
        m_insertCandidate = std::move(candidate);
        m_insertPage = new PropertyManagerPage(
            "Insert Components",
            "Move the pointer over the viewport and click to place. Enter confirms, Esc cancels.",
            m_panels);
        auto* form = new QFormLayout;
        m_insertPage->settings->addLayout(form);
        form->addRow(new QLabel(QFileInfo(path).fileName()));
        for (int i = 0; i < 3; ++i)
        {
            auto* field = new PropertyDimension;
            m_insertCoordinates[i] = field;
            field->setRange(-1e9, 1e9);
            field->setSuffix(" mm");
            field->setObjectName(QString("insertComponent%1").arg(QChar('X' + i)));
            form->addRow(QString(QChar('X' + i)), field);
            connect(field, &QDoubleSpinBox::valueChanged, this,
                    [this]
                    {
                        moveInsertion({m_insertCoordinates[0]->value(),
                                       m_insertCoordinates[1]->value(),
                                       m_insertCoordinates[2]->value()});
                    });
        }
        m_insertFixed = new QCheckBox("Fix component");
        m_insertFixed->setChecked(document().components().empty());
        form->addRow(m_insertFixed);
        m_insertPage->settings->addStretch();
        connect(m_insertPage->buttons, &QDialogButtonBox::accepted, this,
                [this] { finishInsertion(true); });
        connect(m_insertPage->buttons, &QDialogButtonBox::rejected, this,
                [this] { finishInsertion(false); });
        m_panels->addWidget(m_insertPage);
        m_panels->setCurrentWidget(m_insertPage);
        m_left->setSizes({260, 500});
        m_viewport->setSelected({});
        m_viewport->setInserting(true);
        render(*m_insertCandidate);
        if (document().components().empty())
            m_viewport->fitAll();
        m_viewport->setFocus();
        updateActions();
        return true;
    }
    catch (const Standard_Failure& error)
    {
        reportError(QString::fromUtf8(error.GetMessageString()));
    }
    catch (const std::exception& error)
    {
        reportError(QString::fromUtf8(error.what()));
    }
    return false;
}

bool AssemblyWindow::hasComponent(const std::string& id) const
{
    return std::any_of(document().components().begin(), document().components().end(),
                       [&](const auto& component) { return component.id == id; });
}

bool AssemblyWindow::createPartInAssembly(const QString& requestedPath)
{
    if (draftActive() || requestedPath.isEmpty())
        return false;
    if (m_preselection.size() > 1)
    {
        reportError("Select one plane or planar reference before creating a part, or clear the selection to use the assembly origin.");
        return false;
    }
    QString path = QDir::cleanPath(QFileInfo(requestedPath).absoluteFilePath());
    if (QFileInfo(path).suffix().isEmpty())
        path += ".thepart";
    if (QFileInfo(path).suffix().compare("thepart", Qt::CaseInsensitive) != 0)
    {
        reportError("New assembly components must be saved as .thepart documents.");
        return false;
    }
    if (QFileInfo::exists(path))
    {
        reportError("That file already exists. Choose a new name so an existing part is not overwritten.");
        return false;
    }

    bool fileCreated = false;
    bool assemblyCommitted = false;
    std::string componentId;
    const auto rollback = [&]
    {
        if (assemblyCommitted)
        {
            m_session.traverse(false);
            assemblyCommitted = false;
            m_selected.clear();
            m_preselection.clear();
            try
            {
                refresh();
            }
            catch (...)
            {
                render(document());
            }
        }
        if (fileCreated)
        {
            QFile::remove(path);
            fileCreated = false;
        }
    };
    try
    {
        // Reserve the path atomically so a concurrent create cannot be
        // overwritten between the existence check and the save.
        QFile reservation(path);
        if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            throw std::runtime_error(
                "The selected part path could not be created without overwriting an existing file.");
        reservation.close();
        fileCreated = true;
        document::PartDocument::createEmpty().save(path);
        auto candidate = document();
        componentId = candidate.insertComponent(path, gp_Trsf{}, false);
        if (m_preselection.size() == 1)
            candidate.linkInPlacePart(componentId, m_preselection.front().reference);
        if (!m_session.commit(std::move(candidate)))
            throw std::runtime_error("The assembly did not accept the new part instance.");
        assemblyCommitted = true;
        m_selected = componentId;
        m_preselection.clear();
        refresh();
        application::recordRecentDocument(path);
        if (!openComponentPart(componentId, true))
            throw std::runtime_error(
                "The new part was created, but its Edit In Place session could not be opened.");
        return true;
    }
    catch (const Standard_Failure& error)
    {
        rollback();
        reportError(QString::fromUtf8(error.GetMessageString()));
    }
    catch (const std::exception& error)
    {
        rollback();
        reportError(QString::fromUtf8(error.what()));
    }
    return false;
}

bool AssemblyWindow::openComponentPart(const std::string& id, bool editInPlace)
{
    if (!hasComponent(id))
        return false;
    const auto& component = document().component(id);
    if (component.path.isEmpty())
    {
        reportError("This generated component has no independent part file yet. Use Save Owned Part As… first.");
        return false;
    }
    if (editInPlace && m_editInPlaceWindow &&
        m_editInPlaceWindow->currentPath().compare(component.path, Qt::CaseInsensitive) != 0)
    {
        reportError("Finish the current Edit In Place session before editing another component.");
        return false;
    }

    MainWindow* partWindow =
        m_editInPlaceWindow &&
                m_editInPlaceWindow->currentPath().compare(component.path, Qt::CaseInsensitive) == 0
            ? m_editInPlaceWindow.data()
            : nullptr;
    if (!partWindow)
        for (auto* top : QApplication::topLevelWidgets())
            if (auto* candidate = dynamic_cast<MainWindow*>(top))
                if (candidate->currentPath().compare(component.path, Qt::CaseInsensitive) == 0)
                {
                    partWindow = candidate;
                    break;
                }
    if (partWindow && partWindow->hasEditInPlaceContext() &&
        (!editInPlace || partWindow != m_editInPlaceWindow))
    {
        reportError(editInPlace
                        ? "This part is already being edited in another assembly context."
                        : "Finish Edit In Place before opening this part independently.");
        return false;
    }
    if (!partWindow)
    {
        partWindow = new MainWindow;
        if (!partWindow->openPartFile(component.path))
        {
            delete partWindow;
            return false;
        }
    }

    if (editInPlace && !partWindow->hasEditInPlaceContext())
    {
        std::vector<TopoDS_Shape> surroundings, referenced;
        std::set<std::string> referencedIds;
        for (const auto& inPlace : document().inPlaceParts())
            if (inPlace.componentId == id && inPlace.linked &&
                !inPlace.support.componentId.empty())
                referencedIds.insert(inPlace.support.componentId);
        gp_Trsf worldToPart = component.placement;
        worldToPart.Invert();
        for (const auto& other : document().components())
        {
            if (other.id == id || !other.visible || other.shape().IsNull())
                continue;
            auto transform = worldToPart;
            transform.Multiply(other.placement);
            BRepBuilderAPI_Transform localShape(other.shape(), transform, Standard_True);
            if (!localShape.IsDone())
                continue;
            surroundings.push_back(localShape.Shape());
            if (referencedIds.count(other.id))
                referenced.push_back(localShape.Shape());
        }
        QPointer<AssemblyWindow> guard(this);
        setEnabled(false);
        partWindow->setEditInPlaceContext(
            QString::fromStdString(component.name), surroundings, referenced,
            [guard, id]
            {
                if (!guard)
                    return;
                guard->m_editInPlaceWindow.clear();
                guard->setEnabled(true);
                guard->m_selected = id;
                guard->refresh();
                guard->raise();
                guard->activateWindow();
            });
        m_editInPlaceWindow = partWindow;
    }
    partWindow->setAttribute(Qt::WA_DeleteOnClose);
    partWindow->showMaximized();
    partWindow->raise();
    partWindow->activateWindow();
    return true;
}

void AssemblyWindow::refreshLinkedPartSource(const QString& requestedPath)
{
    const auto path = QDir::cleanPath(QFileInfo(requestedPath).absoluteFilePath());
    const bool referenced = std::any_of(
        document().components().begin(), document().components().end(),
        [&](const auto& component)
        { return component.embeddedPart.isEmpty() &&
                 component.path.compare(path, Qt::CaseInsensitive) == 0; });
    if (!referenced)
        return;
    if (draftActive())
    {
        m_pendingSourceRefreshes.insert(path);
        statusBar()->showMessage("A linked part changed on disk. It will refresh when the current assembly operation ends.");
        return;
    }
    try
    {
        auto candidate = document();
        candidate.reloadSources();
        m_session.commit(std::move(candidate));
        refresh();
        statusBar()->showMessage("Linked part updated from its saved .thepart document.", 5000);
    }
    catch (const Standard_Failure& error)
    {
        reportError(QString("Linked part update failed; the last valid assembly state was kept.\n%1")
                        .arg(QString::fromUtf8(error.GetMessageString())));
    }
    catch (const std::exception& error)
    {
        reportError(QString("Linked part update failed; the last valid assembly state was kept.\n%1")
                        .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWindow::moveInsertion(const gp_Pnt& point)
{
    if (!m_insertCandidate)
        return;
    try
    {
        auto candidate = *m_insertCandidate;
        gp_Trsf pose = candidate.component(m_insertId).placement;
        pose.SetTranslationPart(point.XYZ());
        m_magneticRawPose = pose;
        candidate.setPlacement(m_insertId, pose);
        if (m_magneticAction->isChecked())
            candidate.snapMagnetic(m_insertId, m_magneticDistance, m_magneticMovingConnector,
                                   m_magneticStationaryConnector);
        m_insertCandidate = std::move(candidate);
        for (int i = 0; i < 3; ++i)
        {
            const QSignalBlocker block(m_insertCoordinates[i]);
            m_insertCoordinates[i]->setValue(
                m_insertCandidate->component(m_insertId).placement.TranslationPart().Coord(i + 1));
        }
        render(*m_insertCandidate);
    }
    catch (const std::exception& error)
    {
        m_insertPage->setStatus(QString::fromUtf8(error.what()), true);
    }
}
void AssemblyWindow::finishInsertion(bool accepted)
{
    if (!m_insertPage)
        return;
    if (accepted)
        try
        {
            auto candidate = *m_insertCandidate;
            candidate.setFixed(m_insertId, m_insertFixed->isChecked());
            m_session.commit(std::move(candidate));
            m_selected = m_insertId;
        }
        catch (const std::exception& error)
        {
            m_insertPage->setStatus(QString::fromUtf8(error.what()), true);
            return;
        }
    auto* page = m_insertPage;
    m_insertPage = nullptr;
    m_insertCandidate.reset();
    m_insertId.clear();
    m_viewport->setInserting(false);
    m_panels->setCurrentWidget(m_placement);
    m_panels->removeWidget(page);
    page->deleteLater();
    refresh();
    m_viewport->setSelected(m_selected);
}
void AssemblyWindow::startMate(const std::optional<document::AssemblyMate>& editing)
{
    if (draftActive() || document().components().empty())
        return;
    try
    {
        m_mate = new AssemblyMateEditor(document(), editing, m_panels);
        m_panels->addWidget(m_mate);
        m_panels->setCurrentWidget(m_mate);
        m_left->setSizes({260, 500});
        m_viewport->setSelected({});
        m_viewport->setMatePicking(true);
        connect(m_mate, &AssemblyMateEditor::previewChanged, this,
                &AssemblyWindow::updateMatePreview);
        connect(m_mate->page()->buttons, &QDialogButtonBox::accepted, this,
                [this] { finishMate(true); });
        connect(m_mate->page()->buttons, &QDialogButtonBox::rejected, this,
                [this] { finishMate(false); });
        if (!editing)
        {
            std::vector<document::AssemblyReference> refs;
            for (const auto& option : m_preselection)
                refs.push_back(option.reference);
            m_mate->preselect(refs);
        }
        updateMatePreview();
        updateActions();
    }
    catch (const std::exception& error)
    {
        reportError(QString::fromUtf8(error.what()));
    }
}
void AssemblyWindow::updateMatePreview()
{
    if (m_mate)
        render(m_mate->candidate() ? *m_mate->candidate() : document());
}
void AssemblyWindow::finishMate(bool accepted)
{
    if (!m_mate)
        return;
    if (accepted)
    {
        if (!m_mate->candidate())
            m_mate->refreshPreview();
        if (!m_mate->candidate())
            return;
        m_session.commit(*m_mate->candidate());
    }
    const bool again = accepted && m_mate->keepOpen();
    auto* editor = m_mate;
    m_mate = nullptr;
    m_viewport->setMatePicking(false);
    m_panels->setCurrentWidget(m_placement);
    m_panels->removeWidget(editor);
    editor->deleteLater();
    refresh();
    m_viewport->setSelected(m_selected);
    if (again)
    {
        m_preselection.clear();
        startMate();
        if (m_mate)
            m_mate->findChild<QCheckBox*>("assemblyMateKeepOpen")->setChecked(true);
    }
}
void AssemblyWindow::cancelDraft()
{
    if (m_workflow)
        finishWorkflow(false);
    else if (m_mate)
        finishMate(false);
    else if (m_insertPage)
        finishInsertion(false);
}
void AssemblyWindow::deleteSelected()
{
    if (draftActive())
        return;
    std::vector<std::string> components, mates, belts;
    bool ground = false;
    QStringList names, dependents;
    for (const auto* item : m_tree->selectedItems())
    {
        const auto id = item->data(0, Qt::UserRole).toString().toStdString();
        const auto kind = item->data(0, kindRole).toString();
        if (kind == "component")
        {
            components.push_back(id);
            names << q(document().component(id).name);
        }
        else if (kind == "mate")
        {
            mates.push_back(id);
            names << item->text(0);
        }
        else if (kind == "belt")
        {
            belts.push_back(id);
            for (const auto& belt : document().belts())
                if (belt.id == id)
                {
                    names << q(belt.name);
                    if (!belt.componentId.empty())
                    {
                        dependents << q(document().component(belt.componentId).name) +
                                          " (owned belt part)";
                        for (const auto& name :
                             document().componentDependencyNames({belt.componentId}))
                            dependents << q(name);
                    }
                }
        }
        else if (kind == "ground")
        {
            ground = true;
            names << "Assembly Ground Plane";
            for (const auto& mate : document().mates())
                if (mate.id.rfind("mate-ground-", 0) == 0)
                    dependents << q(mate.name);
        }
    }
    if (components.empty() && mates.empty() && belts.empty() && !ground)
        return;
    for (const auto& name : document().componentDependencyNames(components))
        dependents << q(name);
    dependents.removeDuplicates();
    if (!dependents.empty() && !confirmComponentDeletion(names, dependents))
        return;
    mutate(
        [&](auto& doc)
        {
            if (!mates.empty())
                doc.deleteMates(mates);
            for (const auto& id : belts)
                doc.deleteBelt(id, true);
            std::vector<std::string> remaining;
            for (const auto& id : components)
                if (std::any_of(doc.components().begin(), doc.components().end(),
                                [&](const auto& c) { return c.id == id; }))
                    remaining.push_back(id);
            if (!remaining.empty())
                doc.deleteComponents(remaining, true);
            if (ground)
                doc.setGroundPlane({});
        });
}
void AssemblyWindow::updateActions()
{
    const bool active = draftActive();
    const bool editor = m_mate || m_workflow || m_insertPage;
    const int panelHeight = editor ? QWIDGETSIZE_MAX : m_placement->sizeHint().height() + 12;
    const bool panelChanged = m_panels->maximumHeight() != panelHeight;
    m_panels->setMaximumHeight(panelHeight);
    if (panelChanged)
        m_left->setSizes(editor
                             ? QList<int>{230, qMax(450, m_left->height() - 230)}
                             : QList<int>{qMax(100, m_left->height() - panelHeight), panelHeight});
    const auto found = std::find_if(document().components().begin(), document().components().end(),
                                    [&](const auto& c) { return c.id == m_selected; });
    const bool selected = found != document().components().end();
    m_insertAction->setEnabled(!active);
    m_mateAction->setEnabled(!active && !document().components().empty());
    m_beltAction->setEnabled(!active && document().components().size() >= 2);
    m_assetAction->setEnabled(!active && !document().components().empty());
    m_groundAction->setEnabled(!active);
    m_contextAction->setEnabled(!active);
    m_magneticAction->setEnabled(!m_mate && !m_workflow);
    m_rebuild->setEnabled(m_commands.availability("assembly.refresh_references").available);
    m_undo->setEnabled(m_commands.availability("edit.undo").available);
    m_redo->setEnabled(m_commands.availability("edit.redo").available);
    for (auto* action : {m_new, m_open, m_save, m_saveAs})
        action->setEnabled(!active);
    m_fix->setEnabled(m_commands.availability("assembly.fix_float").available);
    m_hide->setEnabled(!active && selected);
    m_fix->setText(selected && found->fixed ? "Float" : "Fix");
    m_hide->setText(selected && found->visible ? "Hide" : "Show");
    bool deletable = false;
    for (const auto* item : m_tree->selectedItems())
        deletable |= item->data(0, kindRole).toString() == "component" ||
                     item->data(0, kindRole).toString() == "mate" ||
                     item->data(0, kindRole).toString() == "belt" ||
                     item->data(0, kindRole).toString() == "ground";
    m_delete->setEnabled(!active && deletable);
}
bool AssemblyWindow::eventFilter(QObject* object, QEvent* event)
{
    const auto* widget = qobject_cast<QWidget*>(object);
    if (event->type() == QEvent::KeyRelease && widget && widget->window() == this)
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat() && (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift))
            quickMates();
    }
    if (event->type() == QEvent::KeyPress && widget && widget->window() == this)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && (m_mate || m_workflow || m_insertPage))
        {
            cancelDraft();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (m_mate || m_workflow || m_insertPage))
        {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(QApplication::focusWidget()))
                spin->interpretText();
            if (m_workflow)
                finishWorkflow(true);
            else if (m_mate)
                finishMate(true);
            else
                finishInsertion(true);
            return true;
        }
        if ((key->key() == Qt::Key_BracketLeft || key->key() == Qt::Key_BracketRight) &&
            (widget == m_viewport || m_dragCandidate || m_insertCandidate))
        {
            cycleMagnetic(key->key() == Qt::Key_BracketLeft);
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
void showAssemblyWindow(const QString& path)
{
    launchWorkspace(application::LaunchDocument::Assembly, path);
}

void notifyOpenAssemblyWindowsPartSaved(const QString& path)
{
    const auto absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (auto* top : QApplication::topLevelWidgets())
        if (auto* assembly = dynamic_cast<AssemblyWindow*>(top))
            assembly->refreshLinkedPartSource(absolute);
}
} // namespace thecad::ui
