#pragma once
#include <QString>
class QFileDialog;
class QWidget;
namespace thecad::ui
{
class NativeFileDialogs final
{
public:
    static void configureSave(QFileDialog& dialog, const QString& currentPath, bool allowExport);
    static QString normalizedSavePath(const QString& path, const QString& filter);
    static QString chooseSave(QWidget* parent, const QString& currentPath, bool allowExport);
};
} // namespace thecad::ui
