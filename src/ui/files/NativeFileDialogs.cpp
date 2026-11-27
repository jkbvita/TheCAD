#include "ui/files/NativeFileDialogs.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
namespace thecad::ui
{
namespace
{
const QString partFilter = "TheCAD Part (*.thepart)";
const QString stepFilter = "STEP (*.step *.stp)";
const QString stlFilter = "STL Mesh (*.stl)";
} // namespace
void NativeFileDialogs::configureSave(QFileDialog& dialog, const QString& currentPath,
                                      bool allowExport)
{
    dialog.setOption(QFileDialog::DontUseNativeDialog, false);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, false);
    dialog.setWindowTitle("Save As");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(allowExport ? QStringList{partFilter, stepFilter, stlFilter}
                                      : QStringList{partFilter});
    dialog.selectNameFilter(partFilter);
    dialog.setDefaultSuffix("thepart");
    QString suggestion = currentPath.isEmpty() ? "Untitled.thepart" : currentPath;
    if (QFileInfo(suggestion).suffix().compare("tcpart", Qt::CaseInsensitive) == 0)
        suggestion = suggestion.left(suggestion.size() - 6) + "thepart";
    dialog.selectFile(suggestion);
}
QString NativeFileDialogs::normalizedSavePath(const QString& path, const QString& filter)
{
    if (path.isEmpty())
        return {};
    const auto suffix = QFileInfo(path).suffix().toLower();
    const auto expected = filter == stepFilter  ? QString("step")
                          : filter == stlFilter ? QString("stl")
                                                : QString("thepart");
    if (suffix == expected || (expected == "step" && suffix == "stp"))
        return path;
    if (suffix == "thepart" || suffix == "tcpart" || suffix == "step" || suffix == "stp" ||
        suffix == "stl")
        return path.left(path.size() - suffix.size()) + expected;
    return path + "." + expected;
}
QString NativeFileDialogs::chooseSave(QWidget* parent, const QString& currentPath, bool allowExport)
{
    QFileDialog dialog(parent);
    configureSave(dialog, currentPath, allowExport);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    const auto selected = dialog.selectedFiles().first();
    const auto target = normalizedSavePath(selected, dialog.selectedNameFilter());
    // Native overwrite confirmation applies to its returned path. Confirm again
    // only if extension normalization resolves to a different existing target.
    if (QFileInfo(selected).absoluteFilePath() != QFileInfo(target).absoluteFilePath() &&
        QFileInfo::exists(target))
    {
        if (QMessageBox::question(parent, "Confirm Save As",
                                  QFileInfo(target).fileName() + " already exists. Replace it?",
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return {};
    }
    return target;
}
} // namespace thecad::ui
