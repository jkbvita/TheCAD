#pragma once
#include "ui/input/PointerDevices.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <functional>

namespace thecad::ui
{
enum class NavigationInputMode
{
    Automatic,
    Mouse,
    Touchpad
};
class NavigationInputSettings final : public QObject
{
    Q_OBJECT
public:
    using Probe = std::function<MousePresence()>;
    static NavigationInputSettings& instance();
    explicit NavigationInputSettings(QObject* parent = nullptr,
                                     Probe probe = connectedMousePresence);
    NavigationInputMode mode() const { return m_mode; }
    MousePresence mousePresence() const { return m_presence; }
    bool touchpadEnabled() const;
    void setMode(NavigationInputMode mode, bool persist = true);
    void refreshDevices();
    QString statusText() const;
signals:
    void changed();

private:
    NavigationInputMode m_mode = NavigationInputMode::Automatic;
    MousePresence m_presence = MousePresence::Unknown;
    Probe m_probe;
    QTimer m_timer;
    QFutureWatcher<MousePresence> m_pending;
    bool m_scanPending = false;
};
} // namespace thecad::ui
