#pragma once

#include "document/PartDocument.h"
#include "document/UndoHistory.h"

#include <QMainWindow>
#include <optional>
#include <memory>

class QLabel;
class QDialog;
class QComboBox;
class QCheckBox;
class QWidget;
class QCloseEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QTabWidget;
class QAction;
class QGroupBox;
class QDoubleSpinBox;
class QSpinBox;
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
    void startSmartDimension();
    void closeSmartDimension();
    void smartReferencePicked(const QString& id,int role);
    void updateSmartPreview(modeling::Point2 point);
    void placeSmartDimension(modeling::Point2 point);
    void editSmartDimension(const std::string& id);
    void openDimensionEditor(const modeling::SketchDimension& dimension);
    void resetSmartSelection();
    struct SmartDimensionDraft {
        std::vector<modeling::SketchPointRef> points;
        std::vector<std::string> entities;
        QDialog* editor = nullptr;
        std::optional<modeling::SketchDimension> pending;
        bool radius = false;
        bool resume = true;
        bool editingExisting = false;
    };
    std::unique_ptr<SmartDimensionDraft> m_smartDimension;
    void startConstraints();
    void closeConstraints();
    void constraintEntitySelected(const QString& id);
    void constraintPointSelected(const modeling::SketchPointRef& point);
    void updateConstraintDraft();
    void applyRelation();
    void acceptConstrainedSketch(const modeling::Sketch& candidate);
    void constraintError(const QString& error);
    struct ConstraintDraft {
        QWidget* page = nullptr;
        QComboBox* relation = nullptr;
        QLabel *selection = nullptr, *error = nullptr;
        QListWidget* constraints = nullptr;
        std::vector<modeling::SketchPointRef> points;
        std::vector<std::string> entities;

    };
    std::unique_ptr<ConstraintDraft> m_constraints;
    std::optional<modeling::SketchPointRef> m_constraintPoint;
    bool maybeSave();
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    bool saveToPath(const QString& path);
    bool replaceDocument(document::PartDocument candidate, const QString& path);
    bool applyModel(document::PartDocument candidate, const std::string& selectedId,bool record=true);
    struct EditState {
        document::PartDocument document;
        std::optional<modeling::Sketch> sketch;
        std::string sketchId,selectedId;
        TopoDS_Shape displayedShape;
        std::size_t revision=0;
    };
    EditState editState() const;
    void rememberChange(EditState previous);
    void rememberSketchChange(EditState previous);
    void traverseHistory(bool redo);
    void updateHistoryActions();
    document::UndoHistory<EditState> m_editHistory{10};
    std::size_t m_revision=0,m_nextRevision=0,m_savedRevision=0;
    QAction* m_undo=nullptr;
    QAction* m_redo=nullptr;
    void updateDocumentUi();
    void updateActions();
    void updateSketchToolActions();
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
    void toggleConstruction();
    void editPolygon();
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
    QAction *m_point=nullptr,*m_centerline=nullptr,*m_polygon=nullptr,*m_construction=nullptr,*m_editPolygon=nullptr;
    QSpinBox* m_polygonSides=nullptr;
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
