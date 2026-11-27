#pragma once
#include <QString>
namespace thecad::ui
{
enum class MousePresence
{
    Unknown,
    Present,
    Absent
};
// Driver names and HID usage IDs distinguish integrated touchpads from mice.
bool isTouchpadDescriptor(const QString& descriptor);
MousePresence connectedMousePresence();
} // namespace thecad::ui
