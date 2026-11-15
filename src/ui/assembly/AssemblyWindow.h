#pragma once
#include "application/AssemblySession.h"
#include <QMainWindow>
#include <functional>
class QAction;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QSplitter;
class QCheckBox;
class QDoubleSpinBox;
namespace thecad::ui
{
class AssemblyViewport;
class AssemblyMateEditor;
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

protected:
    virtual QString chooseComponentPath();
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
    void selectTree();
    void picked(const QString& component, int face, Qt::KeyboardModifiers modifiers);
    void selectComponent(const std::string& id, int face = 0);
    void startMate(const std::optional<document::AssemblyMate>& editing = {});
    void finishMate(bool accepted);
    void updateMatePreview();
    void finishInsertion(bool accepted);
    void moveInsertion(const gp_Pnt& point);
    void applyPlacement();
    void deleteSelected();
    bool save(bool saveAs = false);
    bool maybeSave();
    void cancelDraft();
    void updateActions();
    void mutate(const std::function<void(document::AssemblyDocument&)>& operation);
    bool draftActive() const;
    document::AssemblyReferenceOption faceReference(const std::string& id, int face) const;
    application::AssemblySession m_session;
    AssemblyViewport* m_viewport;
    QTreeWidget* m_tree;
    QStackedWidget* m_panels;
    QSplitter* m_left;
    ComponentPlacementEditor* m_placement;
    AssemblyMateEditor* m_mate = nullptr;
    PropertyManagerPage* m_insertPage = nullptr;
    std::optional<document::AssemblyDocument> m_insertCandidate, m_dragCandidate;
    std::string m_insertId, m_selected;
    QCheckBox* m_insertFixed = nullptr;
    QDoubleSpinBox* m_insertCoordinates[3]{};
    std::vector<document::AssemblyReferenceOption> m_treeReferences;
    std::vector<document::AssemblyReferenceOption> m_preselection;
    QAction *m_insertAction, *m_mateAction, *m_moveAction, *m_rotateAction, *m_undo, *m_redo,
        *m_delete, *m_fix, *m_hide, *m_save, *m_saveAs, *m_open, *m_new, *m_rebuild;
};
void showAssemblyWindow(const QString& path = {});
} // namespace thecad::ui
