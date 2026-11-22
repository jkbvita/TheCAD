#pragma once
#include <QIcon>
namespace thecad::ui
{
// Original line art for the compact inspection toolbar.
enum class MeasureIcon
{
    Circular,
    Units,
    Points,
    Frame,
    Project,
    History,
    Pin
};
QIcon measureIcon(MeasureIcon);
} // namespace thecad::ui
