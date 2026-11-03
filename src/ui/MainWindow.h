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
    virtual bool chooseSketchPlane(modeling::SketchPlane& plane);
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
    const document::Feature* selectedFeature() const;
    void selectFeature(const std::string& id);
    void selectionChanged();
    void createSketch();
    void editSelectedFeature();
    void startSketchEditing(const document::Feature& feature);
    bool finishSketch();
    void addSketchEntity(const modeling::SketchEntity& entity);
    void editSketchEntity();
    void deleteSketchEntity();
    void extrude(bool cut);
    void startOperation(document::Feature feature, bool editing);
    void operationSketchChanged();
    void updateOperationPreview();
    void operationRegionClicked(modeling::Point2 point);
    struct OperationDraft;

    document::PartDocument m_document;
    QString m_path;
    CadViewport* m_viewport = nullptr;
    QLabel* m_dimensions = nullptr;
    QTreeWidget* m_tree = nullptr;
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
    std::optional<modeling::Sketch> m_editSketch;
    std::string m_editSketchId;
    std::string m_selectedId;
    std::unique_ptr<OperationDraft> m_operation;
};

} // namespace thecad::ui
