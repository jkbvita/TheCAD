#include "ui/input/NavigationInputSettings.h"
#include <QCoreApplication>
#include <QSettings>
#include <QPromise>
#include <QThreadPool>
#include <memory>

namespace thecad::ui
{
NavigationInputSettings& NavigationInputSettings::instance()
{
    static auto* settings = new NavigationInputSettings(QCoreApplication::instance());
    return *settings;
}
NavigationInputSettings::NavigationInputSettings(QObject* parent, Probe probe)
    : QObject(parent), m_probe(std::move(probe))
{
    const auto saved = qEnvironmentVariable(
        "THECAD_TEST_NAVIGATION",
        QSettings("TheCAD", "TheCAD").value("navigation/inputMode", "automatic").toString());
    m_mode = saved == "mouse"      ? NavigationInputMode::Mouse
             : saved == "touchpad" ? NavigationInputMode::Touchpad
                                   : NavigationInputMode::Automatic;
    connect(&m_pending, &QFutureWatcher<MousePresence>::finished, this,
            [this]
            {
                m_scanPending = false;
                const auto presence = m_pending.result();
                if (presence != m_presence)
                {
                    m_presence = presence;
                    emit changed();
                }
            });
    connect(&m_timer, &QTimer::timeout, this, &NavigationInputSettings::refreshDevices);
    m_timer.start(2000);
    refreshDevices();
}
bool NavigationInputSettings::touchpadEnabled() const
{
    return m_mode == NavigationInputMode::Touchpad ||
           (m_mode == NavigationInputMode::Automatic && m_presence == MousePresence::Absent);
}
void NavigationInputSettings::setMode(NavigationInputMode mode, bool persist)
{
    if (persist)
        QSettings("TheCAD", "TheCAD")
            .setValue("navigation/inputMode", mode == NavigationInputMode::Mouse ? "mouse"
                                              : mode == NavigationInputMode::Touchpad
                                                  ? "touchpad"
                                                  : "automatic");
    if (m_mode != mode)
    {
        m_mode = mode;
        emit changed();
    }
}
void NavigationInputSettings::refreshDevices()
{
    if (m_scanPending)
        return;
    m_scanPending = true;
    auto promise = std::make_shared<QPromise<MousePresence>>();
    promise->start();
    m_pending.setFuture(promise->future());
    QThreadPool::globalInstance()->start(
        [promise, probe = m_probe]
        {
            MousePresence result = MousePresence::Unknown;
            try
            {
                result = probe();
            }
            catch (...)
            { /* Preserve usable manual modes. */
            }
            promise->addResult(result);
            promise->finish();
        });
}
QString NavigationInputSettings::statusText() const
{
    if (m_presence == MousePresence::Unknown)
        return "Mouse detection unavailable or pending. Choose Touchpad if needed.";
    return m_presence == MousePresence::Present
               ? "Connected mouse detected."
               : "No connected mouse detected. Automatic uses touchpad controls.";
}
} // namespace thecad::ui
