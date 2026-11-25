#pragma once
#include "application/RecentDocuments.h"
#include <QString>
#include <functional>
class QMenu;
class QWidget;
namespace thecad::ui {
// Opens a separate workspace; never replaces unsaved work in another window.
void launchWorkspace(application::LaunchDocument type, const QString& path = {},
                     std::function<void(bool)> completed = {});
void showWelcomeWindow(bool recent = false);
void addWelcomeActions(QMenu* fileMenu, QWidget* owner);
}
