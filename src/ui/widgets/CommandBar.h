#pragma once
#include <QIcon>
#include <QString>
class QWidget;
class QHBoxLayout;
class QGridLayout;
class QToolButton;
class QAction;
namespace thecad::ui
{
QIcon commandIcon(const QString& name);
QWidget* commandGroup(QHBoxLayout* row, const QString& name, QGridLayout*& grid);
QToolButton* commandButton(QAction* action, QGridLayout* grid, int row, int column,
                           bool large = false);
} // namespace thecad::ui
