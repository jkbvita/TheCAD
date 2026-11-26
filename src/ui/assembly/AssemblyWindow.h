#pragma once
#include "application/AssemblySession.h"
#include "application/CommandSystem.h"
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <functional>
class QAction;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QSplitter;
class QCheckBox;
class QDoubleSpinBox;
class QMenu;
namespace thecad::ui
{
class AssemblyViewport;
class MainWindow;
class AssemblyMateEditor;
class AssemblyPropertyEditor;
class ComponentPlacementEditor;
class PropertyManagerPage;
class AssemblyWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AssemblyWindow(QWidget* parent = nullptr);
    const document::AssemblyDocument& document() const { return m_session.document(); }
    bool openAssemblyFile(const QString& path);
    bool insertPartFile(const QString& path);
    void refreshLinkedPartSource(const QString& path);
    bool hasComponent(const std::string& id) const;
    application::CommandDispatcher& commands() { return m_commands; }

protected:
    virtual QString chooseComponentPath();
    virtual QString chooseNewPartPath();
    virtual QString chooseAssemblyPath(bool save);
    virtual bool confirmDiscard();
    virtual bool confirmComponentDeletion(const QStringList& componentNames,
                                          const QStringList& mates);
    virtual void reportError(const QString& message);
    bool eventFilter(QObject*, QEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    friend class AssemblyWorkflowTest;
    void buildCommands();
    void refresh();
    void refreshTree();
    void render(const document::AssemblyDocument& document);
    void refreshPendingLinkedParts();
    void selectTree();
    void picked(const QString& component, int face, Qt::KeyboardModifiers modifiers);
    void pickedEntity(const QString&, TopAbs_ShapeEnum, int, Qt::KeyboardModifiers);
    void selectComponent(const std::string& id, int face = 0, TopAbs_ShapeEnum kind = TopAbs_FACE);
    void startMate(const std::optional<document::AssemblyMate>& editing = {});
    void quickMates();
    enum class Workflow
    {
        Belt,
        Asset,
        Ground,
        InPlace
    };
    void startWorkflow(Workflow, const std::string& editingId = {});
    void showWorkflow(AssemblyPropertyEditor*);
    void finishWorkflow(bool accepted);
    void updateWorkflowPreview();
    void editMate(const document::AssemblyMate&);
    void addWorkflowTree(QTreeWidgetItem* root);
    void workflowContextMenu(QMenu&, QTreeWidgetItem*);
    void cycleMagnetic(bool moving);
    void finishMate(bool accepted);
    void updateMatePreview();
    void finishInsertion(bool accepted);
    void moveInsertion(const gp_Pnt& point);
    void applyPlacement();
    void previewComponentDrag(const QString&,
                              const std::function<void(document::AssemblyDocument&)>&);
    void deleteSelected();
    bool save(bool saveAs = false);
    bool createPartInAssembly(const QString& path);
    bool openComponentPart(const std::string& id, bool editInPlace);
    bool maybeSave();
    void cancelDraft();
    void updateActions();
    application::CommandContext commandContext() const;
    void bindCommandHandlers();
    void mutate(const std::function<void(document::AssemblyDocument&)>& operation);
    bool draftActive() const;
    document::AssemblyReferenceOption faceReference(const std::string& id, int face,
                                                    TopAbs_ShapeEnum kind = TopAbs_FACE) const;
    application::AssemblySession m_session;
    application::CommandDispatcher m_commands;
    AssemblyViewport* m_viewport;
    QTreeWidget* m_tree;
    QStackedWidget* m_panels;
    QSplitter* m_left;
    ComponentPlacementEditor* m_placement;
    AssemblyMateEditor* m_mate = nullptr;
    AssemblyPropertyEditor* m_workflow = nullptr;
    PropertyManagerPage* m_insertPage = nullptr;
    std::optional<document::AssemblyDocument> m_insertCandidate, m_dragCandidate;
    std::string m_insertId, m_selected, m_selectedBelt, m_magneticMovingId;
    gp_Trsf m_magneticRawPose;
    int m_magneticMovingConnector = -1, m_magneticStationaryConnector = -1;
    double m_magneticDistance = 10;
    QCheckBox* m_insertFixed = nullptr;
    QDoubleSpinBox* m_insertCoordinates[3]{};
    std::vector<document::AssemblyReferenceOption> m_treeReferences;
    std::vector<document::AssemblyReferenceOption> m_preselection;
    QAction *m_insertAction, *m_mateAction, *m_undo, *m_redo, *m_delete, *m_fix, *m_hide,
        *m_save, *m_saveAs, *m_open, *m_new, *m_rebuild;
    QAction *m_beltAction, *m_assetAction, *m_groundAction, *m_contextAction, *m_magneticAction;
    QPointer<MainWindow> m_editInPlaceWindow;
    QSet<QString> m_pendingSourceRefreshes;
    bool m_pendingSourceRefreshQueued = false;
};
void showAssemblyWindow(const QString& path = {});
void notifyOpenAssemblyWindowsPartSaved(const QString& path);
} // namespace thecad::ui
