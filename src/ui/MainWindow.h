#pragma once

#include "document/PartDocument.h"
#include "application/DocumentSession.h"

#include <QMainWindow>
#include <QPointer>
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
class QFrame;
class QEvent;
class QMenu;

namespace thecad::ui {

class CadViewport;
class FeatureTreePresenter;
class SolidFeaturePanel;
class PropertyManagerPage;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    const document::PartDocument& document() const;
    QString currentPath() const;
    bool openPartFile(const QString& path);

protected:
    enum class UnsavedChoice { Save, Discard, Cancel };
    virtual QString chooseOpenPath();
    virtual QString chooseSavePath(bool allowExport);
    virtual UnsavedChoice confirmUnsaved();
    virtual void reportError(const QString& message);
    virtual bool confirmReferenceDeletion(const std::vector<std::string>& ids,const std::vector<document::Feature>& dependents);
    virtual bool chooseFeatureParameters(document::Feature& feature);
    virtual bool chooseEntityParameters(modeling::SketchEntity& entity);
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched,QEvent* event) override;

private:
    void startSmartDimension();
    void closeSmartDimension();
    void smartReferencePicked(const QString& id,int role);
    void smartBodyReferencePicked(const modeling::SketchProjection& projection,int role);
    const modeling::Sketch& smartDimensionSketch() const;
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
        std::optional<modeling::Sketch> projectedSketch;
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
    void showRelationsContext(QPoint position);
    void hideRelationsContext();
    void applyContextRelation(modeling::RelationKind,const std::vector<std::string>&);
    void deleteRelation(const std::string& id);
    void applyContextConstruction(const std::vector<std::string>&);
    void initializeSelectionUi(QMenu* viewMenu);
    void updateConstructionControls();
    void setSelectionConstruction(bool construction);
    void toggleReferenceVisibility();
    std::vector<std::string> selectedReferenceIds() const;
    void deleteSelectedReferences();
    QAction* m_deleteReferences=nullptr;
    QCheckBox* m_forConstruction=nullptr;
    QLabel* m_constructionSummary=nullptr;
    QGroupBox* m_constructionPanel=nullptr;
    QAction* m_referenceVisibility=nullptr;
    QPointer<QFrame> m_relationsToolbar;
    QPoint m_relationsAnchor;
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
    bool openPartFileImpl(const QString& path,bool promptForEmptyDocument);
    bool saveDocument();
    bool saveDocumentAs(bool allowExport=true);
    bool saveToPath(const QString& path);
    bool replaceDocument(document::PartDocument candidate, const QString& path);
    bool applyModel(document::PartDocument candidate, const std::string& selectedId,bool record=true);
    using EditState=application::DocumentSession::State;
    EditState editState() const;
    void rememberChange(EditState previous);
    void rememberSketchChange(EditState previous);
    void traverseHistory(bool redo);
    void updateHistoryActions();

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
    void startSketchModifier(modeling::SketchModifier kind);
    void refreshSketchModifier(bool viewportSelection = false);
    void finishSketchModifier(bool accepted);
    bool sketchModifierEvent(QObject* watched, QEvent* event);
    struct SketchModifierDraft {
        modeling::SketchModifier kind = modeling::SketchModifier::Convert;
        PropertyManagerPage* page = nullptr;
        QLabel* selection = nullptr;
        QComboBox* axis = nullptr;
        QPushButton* pickAxis = nullptr;
        QDoubleSpinBox *spacing = nullptr, *angle = nullptr, *centerX = nullptr, *centerY = nullptr;
        QSpinBox* count = nullptr;
        QCheckBox *reverse = nullptr, *innerLoops = nullptr, *construction = nullptr;
        std::vector<std::string> seeds;
        std::optional<modeling::Sketch> candidate;
    };
    std::unique_ptr<SketchModifierDraft> m_sketchModifier;

    void editSketchEntity();
    void deleteSketchEntity();
    void toggleConstruction();
    void extrude(bool cut);
    bool featureDraftActive() const {return bool(m_operation)||bool(m_referenceDraft)||m_solidFeature||bool(m_sketchModifier);}
    void startSolidFeature(document::FeatureKind kind,const document::Feature* editing=nullptr);
    void finishSolidFeature(bool accepted);
    void syncSolidFeaturePicking();
    void syncSolidFeaturePreview();
    bool solidFeatureEvent(QObject*,QEvent*);
    SolidFeaturePanel* m_solidFeature=nullptr; // Owned by the PropertyManager stack.
    bool m_solidFromSketch=false;
    bool m_solidPreviousPlanes=false;
    QAction *m_revolve=nullptr,*m_revolvedCut=nullptr,*m_fillet=nullptr,*m_chamfer=nullptr;
    QAction *m_linearPattern=nullptr,*m_mirror=nullptr;
    QAction *m_sweep=nullptr,*m_sweptCut=nullptr;
    void startOperation(document::Feature feature, bool editing);
    void finishOperation(bool accepted);
    void operationSketchChanged();
    void updateOperationPreview();
    void operationRegionClicked(modeling::Point2 point);
    void focusOperationDepth();
    bool operationKeyEvent(QObject* watched,QEvent* event);
    struct OperationDraft;
    void startReferenceGeometry(modeling::DatumKind kind,const document::Feature* editing=nullptr);
    void updateReferenceGeometryForm();
    void updateReferenceGeometryPreview();
    void activateReferenceGeometryPick(int field);
    void referenceGeometryPicked(int option);
    void referenceGeometryTreePicked(const QString& id);
    void finishReferenceGeometry(bool accepted);
    void refreshReferenceGeometry();
    void createSketchOnReferencePlane(const std::string& id);
    bool referenceGeometryEvent(QObject*,QEvent*);
    struct ReferenceDraft;
    std::unique_ptr<ReferenceDraft> m_referenceDraft;

    enum class WorkspaceMode { Part, SelectingPlane, EditingSketch, FeatureDraft };
    WorkspaceMode m_mode = WorkspaceMode::Part;
    application::DocumentSession m_session;
    CadViewport* m_viewport = nullptr;
    QLabel* m_dimensions = nullptr;
    QTreeWidget* m_tree = nullptr;
    std::unique_ptr<FeatureTreePresenter> m_treePresenter;
    QDockWidget* m_historyDock = nullptr;
    QStackedWidget* m_managerStack = nullptr;
    QListWidget* m_entities = nullptr;
    QTabWidget* m_tabs = nullptr;
    QAction* m_createSketch = nullptr;
    QAction* m_finishSketch = nullptr;
    QAction* m_extrude = nullptr;
    QAction* m_cut = nullptr;
    QAction *m_referencePoint=nullptr,*m_referenceAxis=nullptr,*m_referencePlane=nullptr;
    QAction* m_editFeature = nullptr;
    QAction* m_editEntity = nullptr;
    QAction* m_deleteEntity = nullptr;
    QAction* m_line = nullptr;
    QAction* m_rectangle = nullptr;
    QAction* m_circle = nullptr;
    QAction *m_point=nullptr,*m_centerline=nullptr,*m_polygon=nullptr,*m_construction=nullptr;
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
    std::unique_ptr<OperationDraft> m_operation;
};

} // namespace thecad::ui
