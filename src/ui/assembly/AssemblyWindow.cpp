#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/AssemblyViewport.h"
#include "ui/assembly/AssemblyMateEditor.h"
#include "ui/assembly/ComponentPlacementEditor.h"
#include "ui/MainWindow.h"
#include "ui/PropertyManagerPage.h"
#include "ui/Theme.h"
#include "ui/widgets/CommandBar.h"
#include "ui/widgets/FeatureCommandBar.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
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
} // namespace
AssemblyWindow::AssemblyWindow(QWidget* parent) : QMainWindow(parent)
{
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
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_panels = new QStackedWidget;
    m_panels->setObjectName("assemblyPropertyManager");
    m_placement = new ComponentPlacementEditor;
    m_panels->addWidget(m_placement);
    m_left->addWidget(m_tree);
    m_left->addWidget(m_panels);
    m_left->setSizes({500, 300});
    dock->setWidget(m_left);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    buildCommands();
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &AssemblyWindow::selectTree);
    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int)
            {
                if (draftActive() || item->data(0, kindRole).toString() != "component")
                    return;
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                const bool visible = item->checkState(0) == Qt::Checked;
                if (m_session.document().component(id).visible != visible)
                    mutate([&](auto& doc) { doc.setVisible(id, visible); });
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int)
            {
                if (!draftActive() && item->data(0, kindRole).toString() == "mate")
                    for (const auto& mate : document().mates())
                        if (q(mate.id) == item->data(0, Qt::UserRole).toString())
                        {
                            startMate(mate);
                            break;
                        }
            });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
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
                if (item->data(0, kindRole).toString() == "component")
                {
                    menu.addAction(m_fix);
                    menu.addAction(m_hide);
                    auto* open = menu.addAction("Open part");
                    const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                    connect(open, &QAction::triggered, this,
                            [this, id]
                            {
                                auto* window = new MainWindow;
                                window->setAttribute(Qt::WA_DeleteOnClose);
                                window->showMaximized();
                                if (!window->openPartFile(document().component(id).path))
                                    window->close();
                            });
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
                                        startMate(mate);
                                        break;
                                    }
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
    connect(m_viewport, &AssemblyViewport::picked, this, &AssemblyWindow::picked);
    connect(m_viewport, &AssemblyViewport::dragStarted, this,
            [this]
            {
                m_dragCandidate = document();
                updateActions();
            });
    connect(m_viewport, &AssemblyViewport::dragPlacement, this,
            [this](const QString& id, const gp_Trsf& pose)
            {
                if (!m_dragCandidate)
                    return;
                try
                {
                    auto candidate = document();
                    candidate.setPlacement(id.toStdString(), pose);
                    m_dragCandidate = std::move(candidate);
                    render(*m_dragCandidate);
                }
                catch (const std::exception& error)
                {
                    statusBar()->showMessage(QString::fromUtf8(error.what()));
                }
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
    return m_mate || m_insertPage || m_dragCandidate.has_value();
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
                           m_selected.clear();
                           refresh();
                       }
                   });
    m_new->setShortcut(QKeySequence::New);
    file->addAction(m_new);
    file->addAction(action("actionNewPart", "New Part",
                           []
                           {
                               auto* window = new MainWindow;
                               window->setAttribute(Qt::WA_DeleteOnClose);
                               window->showMaximized();
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
                    [this]
                    {
                        m_session.traverse(false);
                        refresh();
                    });
    m_undo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    edit->addAction(m_undo);
    m_redo = action("actionRedo", "Redo",
                    [this]
                    {
                        m_session.traverse(true);
                        refresh();
                    });
    m_redo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    edit->addAction(m_redo);
    m_delete = action("actionDeleteAssemblySelection", "Delete", [this] { deleteSelected(); });
    m_delete->setShortcut(QKeySequence::Delete);
    edit->addAction(m_delete);
    m_fix = action("actionFixFloatComponent", "Fix / Float",
                   [this]
                   {
                       if (!m_selected.empty())
                       {
                           const auto id = m_selected;
                           mutate([&](auto& doc) { doc.setFixed(id, !doc.component(id).fixed); });
                       }
                   });
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
    connect(preferences, &QAction::triggered, this,
            [this]
            {
                QDialog dialog(this);
                dialog.setWindowTitle("Preferences");
                auto* layout = new QFormLayout(&dialog);
                auto* theme = new QComboBox;
                theme->addItem("Dark", int(ThemeMode::Dark));
                theme->addItem("Light", int(ThemeMode::Light));
                theme->addItem("Follow system", int(ThemeMode::System));
                theme->setCurrentIndex(theme->findData(int(ThemeManager::instance().mode())));
                layout->addRow("Theme", theme);
                connect(
                    theme, &QComboBox::currentIndexChanged, &dialog, [theme]
                    { ThemeManager::instance().setMode(ThemeMode(theme->currentData().toInt())); });
                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
                layout->addRow(buttons);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                dialog.exec();
            });
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
    m_moveAction = action("actionMoveComponent", "Move Component",
                          [this]
                          {
                              m_viewport->setRotationMode(false);
                              m_placement->focusRotation(false);
                              updateActions();
                          });
    m_moveAction->setCheckable(true);
    m_rotateAction = action("actionRotateComponent", "Rotate Component",
                            [this]
                            {
                                m_viewport->setRotationMode(true);
                                m_placement->focusRotation(true);
                                updateActions();
                            });
    m_rotateAction->setCheckable(true);
    m_rebuild = action("actionRebuildAssembly", "Rebuild",
                       [this] { mutate([](auto& doc) { doc.reloadSources(); }); });
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
    commandButton(m_moveAction, grid, 0, 0);
    commandButton(m_rotateAction, grid, 1, 0);
    commandButton(m_rebuild, grid, 2, 0);
    row->addWidget(group);
    row->addStretch();
    tabs->addTab(page, "Assembly");
    toolbar->addWidget(tabs);
}
QString AssemblyWindow::chooseComponentPath()
{
    return QFileDialog::getOpenFileName(this, "Insert Components", {},
                                        "TheCAD Parts (*.thepart *.tcpart)");
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
    return QMessageBox::question(this, "Delete components and mates",
                                 "Delete:\n" + names.join('\n') +
                                     "\n\nThese dependent mates will also be deleted:\n" +
                                     mates.join('\n') + "\n\nExternal part files are retained.",
                                 QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel) == QMessageBox::Yes;
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
        visuals.push_back({c.id, c.part->shape(), c.placement, c.visible, c.fixed,
                           m_insertCandidate && c.id == m_insertId});
    m_viewport->setComponents(visuals);
    std::vector<AssemblyVisual> highlights;
    if (m_mate)
        for (const auto& ref : m_mate->references())
        {
            if (ref.componentId.empty())
            {
                for (const auto& option : doc.referenceOptions({}))
                    if (option.reference.geometry.source == ref.geometry.source &&
                        option.reference.geometry.index == ref.geometry.index)
                    {
                        highlights.push_back({{}, option.shape, {}});
                        break;
                    }
            }
            else
            {
                const auto& c = doc.component(ref.componentId);
                highlights.push_back(
                    {{}, c.part->currentReferenceShape(ref.geometry), c.placement});
            }
        }
    m_viewport->setReferenceHighlights(highlights);
}
void AssemblyWindow::refresh()
{
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
        for (const auto& option : document().referenceOptions(id))
        {
            const auto source = option.reference.geometry.source;
            if (source != modeling::ReferenceSource::BasePlane &&
                source != modeling::ReferenceSource::BaseAxis &&
                source != modeling::ReferenceSource::Datum)
                continue;
            const int index = int(m_treeReferences.size());
            m_treeReferences.push_back(option);
            const auto label = option.label.section(" / ", -1);
            auto* child = new QTreeWidgetItem(parent, {label});
            child->setIcon(0, commandIcon(option.geometry.kind == modeling::DatumKind::Plane
                                              ? "Reference Plane"
                                              : "Reference Axis"));
            child->setData(0, kindRole, "reference");
            child->setData(0, referenceRole, index);
            child->setData(0, Qt::UserRole,
                           QString("reference:%1:%2:%3")
                               .arg(q(id))
                               .arg(int(source))
                               .arg(source == modeling::ReferenceSource::Datum
                                        ? q(option.reference.geometry.featureId)
                                        : QString::number(option.reference.geometry.index)));
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
    }
    auto* mates = new QTreeWidgetItem(root, {"Mates"});
    mates->setData(0, Qt::UserRole, "assembly-mates");
    mates->setData(0, kindRole, "folder");
    mates->setExpanded(true);
    for (const auto& mate : document().mates())
    {
        auto* item = new QTreeWidgetItem(mates, {q(mate.name)});
        item->setData(0, Qt::UserRole, q(mate.id));
        item->setData(0, kindRole, "mate");
        item->setToolTip(0, modeling::assembly::mateName(mate.kind));
    }
    const auto desired = m_selected.empty() ? current : q(m_selected);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
    {
        const auto id = (*it)->data(0, Qt::UserRole).toString();
        if (expanded.contains(id))
            (*it)->setExpanded(true);
        if (id == desired)
            m_tree->setCurrentItem(*it);
    }
}
void AssemblyWindow::selectComponent(const std::string& id, int face)
{
    m_selected = id;
    m_viewport->setSelected(id, face);
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
    if (m_mate)
    {
        if (item->data(0, kindRole).toString() == "reference")
            m_mate->pick(
                m_treeReferences.at(std::size_t(item->data(0, referenceRole).toInt())).reference);
        return;
    }
    if (draftActive())
        return;
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
    updateActions();
}
document::AssemblyReferenceOption AssemblyWindow::faceReference(const std::string& id,
                                                                int face) const
{
    const auto& component = document().component(id);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(component.part->shape(), TopAbs_FACE, faces);
    if (face > 0 && face <= faces.Extent())
        for (const auto& option : document().referenceOptions(id))
            if (option.reference.geometry.source == modeling::ReferenceSource::BodyFace &&
                option.shape.IsSame(faces(face)))
                return option;
    throw std::runtime_error(
        "Select a planar or cylindrical face, or choose a plane/axis in the component tree.");
}
void AssemblyWindow::picked(const QString& component, int face)
{
    if (m_mate)
    {
        if (!component.isEmpty())
            try
            {
                m_mate->pick(faceReference(component.toStdString(), face).reference);
            }
            catch (const std::exception& error)
            {
                m_mate->page()->setStatus(QString::fromUtf8(error.what()), true);
            }
        return;
    }
    if (!draftActive())
        selectComponent(component.toStdString(), face);
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
        m_insertId = id;
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
void AssemblyWindow::moveInsertion(const gp_Pnt& point)
{
    if (!m_insertCandidate)
        return;
    try
    {
        auto candidate = *m_insertCandidate;
        gp_Trsf pose = candidate.component(m_insertId).placement;
        pose.SetTranslationPart(point.XYZ());
        candidate.setPlacement(m_insertId, pose);
        m_insertCandidate = std::move(candidate);
        for (int i = 0; i < 3; ++i)
        {
            const QSignalBlocker block(m_insertCoordinates[i]);
            m_insertCoordinates[i]->setValue(point.Coord(i + 1));
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
        m_mate->refreshPreview();
        if (!m_mate->candidate())
            return;
        m_session.commit(*m_mate->candidate());
    }
    auto* editor = m_mate;
    m_mate = nullptr;
    m_viewport->setMatePicking(false);
    m_panels->setCurrentWidget(m_placement);
    m_panels->removeWidget(editor);
    editor->deleteLater();
    refresh();
    m_viewport->setSelected(m_selected);
}
void AssemblyWindow::cancelDraft()
{
    if (m_mate)
        finishMate(false);
    else if (m_insertPage)
        finishInsertion(false);
}
void AssemblyWindow::deleteSelected()
{
    if (draftActive())
        return;
    std::vector<std::string> components, mates;
    QStringList names;
    for (const auto* item : m_tree->selectedItems())
    {
        const auto id = item->data(0, Qt::UserRole).toString().toStdString();
        if (item->data(0, kindRole).toString() == "component")
        {
            components.push_back(id);
            names << q(document().component(id).name);
        }
        if (item->data(0, kindRole).toString() == "mate")
            mates.push_back(id);
    }
    if (components.empty() && mates.empty())
        return;
    const auto dependents = document().componentDependents(components);
    QStringList dependentNames;
    for (const auto& mate : dependents)
        dependentNames << q(mate.name);
    if (!dependents.empty() && !confirmComponentDeletion(names, dependentNames))
        return;
    mutate(
        [&](auto& doc)
        {
            if (!mates.empty())
                doc.deleteMates(mates);
            if (!components.empty())
                doc.deleteComponents(components, true);
        });
}
void AssemblyWindow::updateActions()
{
    const bool active = draftActive();
    const auto found = std::find_if(document().components().begin(), document().components().end(),
                                    [&](const auto& c) { return c.id == m_selected; });
    const bool selected = found != document().components().end(),
               movable = selected && !found->fixed && !active;
    m_insertAction->setEnabled(!active);
    m_mateAction->setEnabled(!active && !document().components().empty());
    m_rebuild->setEnabled(!active && !document().components().empty());
    m_undo->setEnabled(!active && m_session.canUndo());
    m_redo->setEnabled(!active && m_session.canRedo());
    for (auto* action : {m_new, m_open, m_save, m_saveAs})
        action->setEnabled(!active);
    m_fix->setEnabled(!active && selected);
    m_hide->setEnabled(!active && selected);
    m_fix->setText(selected && found->fixed ? "Float" : "Fix");
    m_hide->setText(selected && found->visible ? "Hide" : "Show");
    const bool placementMode = selected && !found->fixed && !m_mate && !m_insertPage;
    m_moveAction->setEnabled(movable);
    m_rotateAction->setEnabled(movable);
    m_moveAction->setChecked(placementMode && !m_viewport->rotationMode());
    m_rotateAction->setChecked(placementMode && m_viewport->rotationMode());
    bool deletable = false;
    for (const auto* item : m_tree->selectedItems())
        deletable |= item->data(0, kindRole).toString() == "component" ||
                     item->data(0, kindRole).toString() == "mate";
    m_delete->setEnabled(!active && deletable);
}
bool AssemblyWindow::eventFilter(QObject* object, QEvent* event)
{
    const auto* widget = qobject_cast<QWidget*>(object);
    if (event->type() == QEvent::KeyPress && widget && widget->window() == this)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && (m_mate || m_insertPage))
        {
            cancelDraft();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (m_mate || m_insertPage))
        {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(QApplication::focusWidget()))
                spin->interpretText();
            if (m_mate)
                finishMate(true);
            else
                finishInsertion(true);
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
void showAssemblyWindow(const QString& path)
{
    auto* window = new AssemblyWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->showMaximized();
    if (!path.isEmpty() && !window->openAssemblyFile(path))
        window->close();
}
} // namespace thecad::ui
