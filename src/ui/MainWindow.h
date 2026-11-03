#pragma once

#include "document/PartDocument.h"

#include <QMainWindow>
#include <optional>
#include <memory>

class QLabel;
class QCloseEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QTabWidget;
class QAction;
class QGroupBox;
class QDoubleSpinBox;
class QPushButton;
class QDockWidget;
class QStackedWidget;

namespace thecad::ui {

class CadViewport;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    const document::PartDocument& document() const;
    QString currentPath() const;

protected:
    enum class UnsavedChoice { Save, Discard, Cancel };
    virtual QString chooseOpenPath();
    virtual QString chooseSavePath();
    virtual UnsavedChoice confirmUnsaved();
    virtual void reportError(const QString& message);
    virtual bool chooseFeatureParameters(document::Feature& feature);
    virtual bool chooseEntityParameters(modeling::SketchEntity& entity);
    void closeEvent(QCloseEvent* event) override;

private:
    bool maybeSave();
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    bool saveToPath(const QString& path);
    bool replaceDocument(document::PartDocument candidate, const QString& path);
    bool applyModel(document::PartDocument candidate, const std::string& selectedId);
    void updateDocumentUi();
    void updateActions();
    void updateEntityList();
    void updateEntityProperties();
    void applySketchEntityEdit(const modeling::SketchEntity& entity);
    const document::Feature* selectedFeature() const;
    void selectFeature(const std::string& id);
    void selectionChanged();
    void createSketch();
    void createSketchOnPlane(modeling::SketchPlane plane);
    void createSketchOnFace(int index);
    void referencePlaneSelected(modeling::SketchPlane plane);
    void planeSelectionCanceled();
    void editSelectedFeature();
    void startSketchEditing(const document::Feature& feature);
    bool finishSketch();

    void editSketchEntity();
    void deleteSketchEntity();
    void extrude(bool cut);
    void startOperation(document::Feature feature, bool editing);
    void finishOperation(bool accepted);
    void operationSketchChanged();
    void updateOperationPreview();
    void operationRegionClicked(modeling::Point2 point);
    struct OperationDraft;

    enum class WorkspaceMode { Part, SelectingPlane, EditingSketch, FeatureDraft };
    WorkspaceMode m_mode = WorkspaceMode::Part;
    document::PartDocument m_document;
    QString m_path;
    CadViewport* m_viewport = nullptr;
    QLabel* m_dimensions = nullptr;
    QTreeWidget* m_tree = nullptr;
    QDockWidget* m_historyDock = nullptr;
    QStackedWidget* m_managerStack = nullptr;
    QListWidget* m_entities = nullptr;
    QTabWidget* m_tabs = nullptr;
    QAction* m_createSketch = nullptr;
    QAction* m_finishSketch = nullptr;
    QAction* m_extrude = nullptr;
    QAction* m_cut = nullptr;
    QAction* m_editFeature = nullptr;
    QAction* m_editEntity = nullptr;
    QAction* m_deleteEntity = nullptr;
    QAction* m_line = nullptr;
    QAction* m_rectangle = nullptr;
    QAction* m_circle = nullptr;
    QAction* m_selectEntity = nullptr;
    QGroupBox* m_propertyPanel = nullptr;
    QLabel* m_entitySummary = nullptr;
    QDoubleSpinBox* m_propertyFirstX = nullptr;
    QDoubleSpinBox* m_propertyFirstY = nullptr;
    QDoubleSpinBox* m_propertySecondX = nullptr;
    QDoubleSpinBox* m_propertySecondY = nullptr;
    QDoubleSpinBox* m_propertyRadius = nullptr;
    QPushButton* m_applyProperties = nullptr;
    std::optional<modeling::SketchPlane> m_selectedPlane;
    bool m_selectingPlane = false;
    int m_selectedFace = 0;
    std::optional<modeling::Sketch> m_editSketch;
    std::string m_editSketchId;
    std::string m_selectedId;
    std::unique_ptr<OperationDraft> m_operation;
};

} // namespace thecad::ui
