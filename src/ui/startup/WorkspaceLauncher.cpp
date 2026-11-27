#include "ui/startup/WorkspaceLauncher.h"
#include "ui/startup/WelcomeWindow.h"
#include "ui/startup/LoadingScreen.h"
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/AssemblyViewport.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <Standard_Failure.hxx>
#include <memory>
#include <utility>

namespace thecad::ui {
namespace {
void connectReveal(QWidget* window, LoadingScreen* loading, std::function<void(bool)> completed) {
    auto done = std::make_shared<bool>(false);
    const auto reveal = [loading, done, completed] {
        if (std::exchange(*done, true)) return;
        loading->finish(); if (completed) completed(true);
    };
    // Even an initialization error must leave an accessible window, never a stuck cover.
    // Queued delivery returns from the native paint before revealing the window.
    if (auto* viewport = window->findChild<CadViewport*>()) {
        QObject::connect(viewport, &CadViewport::framePresented, loading, reveal, Qt::QueuedConnection);
        QObject::connect(viewport, &CadViewport::initializationFailed, loading, reveal, Qt::QueuedConnection);
    } else if (auto* assemblyViewport = window->findChild<AssemblyViewport*>()) {
        QObject::connect(assemblyViewport, &AssemblyViewport::framePresented, loading, reveal, Qt::QueuedConnection);
        QObject::connect(assemblyViewport, &AssemblyViewport::initializationFailed, loading, reveal, Qt::QueuedConnection);
    }
}
}
void launchWorkspace(application::LaunchDocument type, const QString& path, std::function<void(bool)> completed) {
    ThemeManager::instance();
    auto* loading = new LoadingScreen(path.isEmpty() ? "Preparing your workspace…" : "Opening your document…");
    loading->show(); loading->repaint();
    // Flush the splash's expose/paint before synchronous OCCT construction can
    // occupy the UI thread. Do not consume another click/shortcut during launch.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QTimer::singleShot(0, loading, [type, path, loading, completed] {
        std::unique_ptr<QWidget> window;
        try {
            bool opened = true;
            if (type == application::LaunchDocument::Assembly) {
                auto assembly = std::make_unique<AssemblyWindow>();
                if (!path.isEmpty()) opened = assembly->openAssemblyFile(path);
                window = std::move(assembly);
            } else {
                auto part = std::make_unique<MainWindow>();
                if (!path.isEmpty()) opened = part->openPartFile(path);
                window = std::move(part);
            }
            if (!opened) {
                if (completed) completed(false);
                loading->hide(); loading->deleteLater(); return;
            }
            window->setAttribute(Qt::WA_DeleteOnClose);
            loading->cover(window.get()); connectReveal(window.get(), loading, completed);
            window->showMaximized(); window.release();
        } catch (const Standard_Failure& error) {
            loading->hide(); QMessageBox::critical(nullptr,"TheCAD startup error",QString::fromUtf8(error.GetMessageString()));
            if (completed) completed(false); loading->hide(); loading->deleteLater();
        } catch (const std::exception& error) {
            loading->hide(); QMessageBox::critical(nullptr,"TheCAD startup error",QString::fromUtf8(error.what()));
            if (completed) completed(false); loading->hide(); loading->deleteLater();
        }
    });
}
void showWelcomeWindow(bool recent) {
    // One welcome window per process; document workspaces remain independent.
    for (auto* top : QApplication::topLevelWidgets()) {
        if (auto* welcome = qobject_cast<WelcomeWindow*>(top)) {
            if (!welcome->isEnabled()) return; // Its workspace is still being prepared.
            if (recent) welcome->showRecent(); welcome->show(); welcome->raise(); welcome->activateWindow(); return;
        }
    }
    ThemeManager::instance();
    auto* settings = new QSettings("TheCAD", "TheCAD");
    auto* welcome = new WelcomeWindow(*settings); settings->setParent(welcome);
    welcome->setAttribute(Qt::WA_DeleteOnClose);
    const auto launch = [welcome](application::LaunchDocument type, const QString& path) {
        welcome->setEnabled(false);
        launchWorkspace(type, path, [guard = QPointer<WelcomeWindow>(welcome)](bool success) {
            if (!guard) return;
            if (success) guard->deleteLater(); else { guard->setEnabled(true); guard->show(); guard->raise(); }
        });
    };
    QObject::connect(welcome, &WelcomeWindow::newDocumentRequested, welcome, [launch](auto type) { launch(type, {}); });
    QObject::connect(welcome, &WelcomeWindow::openDocumentRequested, welcome, [launch](const QString& path) {
        if (const auto type = application::documentTypeForPath(path)) launch(*type, path);
    });
    if (recent) welcome->showRecent(); welcome->show();
}
void addWelcomeActions(QMenu* fileMenu, QWidget* owner) {
    auto* welcome = fileMenu->addAction("Welcome…"); welcome->setObjectName("actionWelcome");
    QObject::connect(welcome, &QAction::triggered, owner, [] { showWelcomeWindow(); });
    auto* recent = fileMenu->addMenu("Open Recent"); recent->setObjectName("recentDocumentsMenu");
    QObject::connect(recent, &QMenu::aboutToShow, owner, [recent] {
        recent->clear(); QSettings settings("TheCAD", "TheCAD"); int count = 0;
        for (const auto& entry : application::RecentDocuments(settings).documents()) {
            if (count++ == 10) break;
            auto* action = recent->addAction(QFileInfo(entry.path).fileName()); action->setToolTip(entry.path); action->setEnabled(QFileInfo(entry.path).isFile());
            QObject::connect(action, &QAction::triggered, recent, [path = entry.path] { launchWorkspace(*application::documentTypeForPath(path), path); });
        }
        if (count) recent->addSeparator();
        auto* browse = recent->addAction("Browse Recent Documents and Folders…"); browse->setObjectName("actionBrowseRecent");
        QObject::connect(browse, &QAction::triggered, recent, [] { showWelcomeWindow(true); });
    });
}
} // namespace thecad::ui
