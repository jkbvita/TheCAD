#pragma once

#include "document/PartDocument.h"

#include <QMainWindow>

class QLabel;
class QCloseEvent;

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
    void closeEvent(QCloseEvent* event) override;

private:
    bool maybeSave();
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    bool saveToPath(const QString& path);
    bool replaceDocument(document::PartDocument candidate, const QString& path);
    void updateDocumentUi();

    document::PartDocument m_document;
    QString m_path;
    CadViewport* m_viewport = nullptr;
    QLabel* m_dimensions = nullptr;
};

} // namespace thecad::ui
