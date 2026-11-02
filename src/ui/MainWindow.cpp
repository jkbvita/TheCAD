#include "ui/MainWindow.h"
#include "ui/CadViewport.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <exception>

namespace thecad::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_document(document::PartDocument::createBox())
{
    // A standard resizable Qt top-level window; startup maximization is in main.
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

    statusBar()->showMessage("Orbit: left drag  |  Pan: middle / right drag  |  Zoom: wheel  |  Fit: F");
    m_dimensions = new QLabel(this);
    statusBar()->addPermanentWidget(m_dimensions);
    setWindowModified(true);
    updateDocumentUi();
    connect(m_viewport, &CadViewport::initializationFailed, this, [this](const QString& message) {
        statusBar()->showMessage("3D viewport initialization failed");
        QTimer::singleShot(0, this, [this, message] {
            reportError(message + "\n\nCheck your graphics driver and OpenGL support.");
        });
    });
}

MainWindow::~MainWindow()
{
    // Release OCCT's WGL context while the top-level HWND still exists. QObject
    // child cleanup in the base destructor happens after native window teardown.
    delete takeCentralWidget();
    m_viewport = nullptr;
}

const document::PartDocument& MainWindow::document() const
{
    return m_document;
}

QString MainWindow::currentPath() const
{
    return m_path;
}

QString MainWindow::chooseOpenPath()
{
    return QFileDialog::getOpenFileName(this, "Open TheCAD part", m_path,
                                        "TheCAD Part (*.tcpart)");
}

QString MainWindow::chooseSavePath()
{
    QFileDialog dialog(this, "Save TheCAD part",
                       m_path.isEmpty() ? "Untitled.tcpart" : m_path,
                       "TheCAD Part (*.tcpart)");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix("tcpart");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return {};
    }
    return dialog.selectedFiles().first();
}

MainWindow::UnsavedChoice MainWindow::confirmUnsaved()
{
    const auto answer = QMessageBox::warning(this, "Unsaved part",
        "Save changes to this part before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return UnsavedChoice::Save;
    }
    return answer == QMessageBox::Discard ? UnsavedChoice::Discard : UnsavedChoice::Cancel;
}

void MainWindow::reportError(const QString& message)
{
    QMessageBox::critical(this, "TheCAD document error", message);
}

bool MainWindow::maybeSave()
{
    if (!isWindowModified()) {
        return true;
    }
    switch (confirmUnsaved()) {
    case UnsavedChoice::Save:
        return saveDocument();
    case UnsavedChoice::Discard:
        return true;
    case UnsavedChoice::Cancel:
        return false;
    }
    return false;
}

void MainWindow::newDocument()
{
    try {
        auto candidate = document::PartDocument::createBox();
        if (maybeSave() && replaceDocument(std::move(candidate), {})) {
            setWindowModified(true);
        }
    } catch (const std::exception& error) {
        reportError(QString::fromUtf8(error.what()));
    }
}

void MainWindow::openDocument()
{
    const QString path = chooseOpenPath();
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().compare("tcpart", Qt::CaseInsensitive) != 0) {
        reportError("Only TheCAD part documents (.tcpart) can be opened in this milestone.");
        return;
    }
    try {
        // Validate before asking to discard or save the current document.
        auto candidate = document::PartDocument::load(path);
        if (!maybeSave()) {
            return;
        }
        // Saving the old document may have updated this same file. Re-read it
        // before committing the replacement, preserving the current model on failure.
        candidate = document::PartDocument::load(path);
        if (replaceDocument(std::move(candidate), QFileInfo(path).absoluteFilePath())) {
            setWindowModified(false);
        }
    } catch (const std::exception& error) {
        reportError(QString::fromUtf8(error.what()));
    }
}

bool MainWindow::saveDocument()
{
    return m_path.isEmpty() ? saveDocumentAs() : saveToPath(m_path);
}

bool MainWindow::saveDocumentAs()
{
    QString path = chooseSavePath();
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += ".tcpart";
    }
    if (QFileInfo(path).suffix().compare("tcpart", Qt::CaseInsensitive) != 0) {
        reportError("Save part documents with the .tcpart extension. Assembly and drawing documents are not supported yet.");
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
    } catch (const std::exception& error) {
        reportError(QString::fromUtf8(error.what()));
        return false;
    }
}

bool MainWindow::replaceDocument(document::PartDocument candidate, const QString& path)
{
    if (!m_viewport->setShape(candidate.shape())) {
        reportError("The part could not be displayed. The current document has been retained.");
        return false;
    }
    m_document = std::move(candidate);
    m_path = path;
    updateDocumentUi();
    return true;
}

void MainWindow::updateDocumentUi()
{
    const QString name = m_path.isEmpty() ? "Untitled.tcpart" : QFileInfo(m_path).fileName();
    setWindowTitle(name + "[*] - TheCAD");
    setWindowFilePath(m_path);
    const auto& parameters = m_document.parameters();
    m_dimensions->setText(QString("Box: %1 x %2 x %3 mm")
                             .arg(parameters.width).arg(parameters.depth).arg(parameters.height));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        // Closing can destroy the native surface before C++ destruction runs.
        m_viewport->releaseGraphics();
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace thecad::ui
