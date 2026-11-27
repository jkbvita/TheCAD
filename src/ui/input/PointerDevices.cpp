#include "ui/input/PointerDevices.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <vector>
#endif

namespace thecad::ui
{
bool isTouchpadDescriptor(const QString& descriptor)
{
    const auto text = descriptor.toUpper();
    return text.contains("TOUCHPAD") || text.contains("TOUCH PAD") || text.contains("TRACKPAD") ||
           text.contains("CLICKPAD") || text.contains("HID_DEVICE_UP:000D_U:0005") ||
           text.contains("ACPI\\SYN") || text.contains("ACPI\\ELAN") || text.contains("ACPI\\ETD");
}

MousePresence connectedMousePresence()
{
#ifdef Q_OS_WIN
    // Present, started mouse-class devices, including USB and Bluetooth mice.
    // Touchpads often expose a mouse compatibility collection; inspect its
    // ancestors too, where the I2C/PS2 touchpad driver keeps its identity.
    const GUID mouseClass = {
        0x4d36e96f, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    const auto devices = SetupDiGetClassDevsW(&mouseClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
        return MousePresence::Unknown;
    struct Cleanup
    {
        HDEVINFO value;
        ~Cleanup() { SetupDiDestroyDeviceInfoList(value); }
    } cleanup{devices};
    const auto property = [](DEVINST node, ULONG key)
    {
        ULONG bytes = 0, type = 0;
        if (CM_Get_DevNode_Registry_PropertyW(node, key, &type, nullptr, &bytes, 0) !=
                CR_BUFFER_SMALL ||
            bytes == 0 || bytes > 65536)
            return QString{};
        std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 2, L'\0');
        if (CM_Get_DevNode_Registry_PropertyW(node, key, &type, value.data(), &bytes, 0) !=
            CR_SUCCESS)
            return QString{};
        auto text = QString::fromWCharArray(value.data(), int(bytes / sizeof(wchar_t)));
        text.replace(QChar(0), QChar(' '));
        return text;
    };
    bool uncertain = false;
    for (DWORD index = 0;; ++index)
    {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices, index, &device))
        {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                uncertain = true;
            break;
        }
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) != CR_SUCCESS)
        {
            uncertain = true;
            continue;
        }
        if (!(status & DN_STARTED))
            continue;
        QString description;
        DEVINST node = device.DevInst;
        for (int depth = 0; depth < 6; ++depth)
        {
            for (const auto key : {CM_DRP_DEVICEDESC, CM_DRP_FRIENDLYNAME, CM_DRP_HARDWAREID,
                                   CM_DRP_COMPATIBLEIDS, CM_DRP_SERVICE})
                description += property(node, key) + '\n';
            DEVINST parent = 0;
            if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
                break;
            node = parent;
        }
        if (isTouchpadDescriptor(description))
            continue;
        // A remote desktop compatibility mouse is not a connected local mouse.
        if (description.contains("RDP_MOU", Qt::CaseInsensitive))
            continue;
        return MousePresence::Present;
    }
    return uncertain ? MousePresence::Unknown : MousePresence::Absent;
#else
    return MousePresence::Unknown;
#endif
}
} // namespace thecad::ui
