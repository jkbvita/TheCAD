#include "ui/PreferencesDialog.h"
#include "ui/Theme.h"
#include "ui/input/NavigationInputSettings.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

namespace thecad::ui
{
void showPreferences(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setObjectName("preferencesDialog");
    dialog.setWindowTitle("Preferences");
    auto* layout = new QFormLayout(&dialog);
    auto* theme = new QComboBox;
    theme->setObjectName("appearanceTheme");
    theme->addItem("Dark", int(ThemeMode::Dark));
    theme->addItem("Light", int(ThemeMode::Light));
    theme->addItem("Follow system", int(ThemeMode::System));
    theme->setCurrentIndex(theme->findData(int(ThemeManager::instance().mode())));
    layout->addRow("Application theme", theme);
    QObject::connect(
        theme, &QComboBox::currentIndexChanged, &dialog,
        [theme] { ThemeManager::instance().setMode(ThemeMode(theme->currentData().toInt())); });
    auto& settings = NavigationInputSettings::instance();
    auto* input = new QComboBox;
    input->setObjectName("navigationInputMode");
    input->addItem("Automatic", int(NavigationInputMode::Automatic));
    input->addItem("Mouse", int(NavigationInputMode::Mouse));
    input->addItem("Touchpad", int(NavigationInputMode::Touchpad));
    input->setCurrentIndex(input->findData(int(settings.mode())));
    layout->addRow("Viewport controls", input);
    auto* status = new QLabel(settings.statusText());
    status->setObjectName("navigationDeviceStatus");
    status->setWordWrap(true);
    layout->addRow(status);
    auto* help =
        new QLabel("Touchpad: hold Shift and move to orbit; hold Ctrl and move to pan.\n"
                   "No click is needed. Release the key for ordinary pointing and selection.\n"
                   "Two-finger slide pans; pinch or Ctrl + two-finger slide zooms when provided by "
                   "the driver.\n"
                   "Automatic enables modifier movement when no mouse is detected.\n"
                   "If your driver reports a touchpad as a mouse, choose Touchpad.");
    help->setWordWrap(true);
    help->setMaximumWidth(470);
    layout->addRow(help);
    QObject::connect(input, &QComboBox::currentIndexChanged, &dialog, [input, &settings]
                     { settings.setMode(NavigationInputMode(input->currentData().toInt())); });
    QObject::connect(&settings, &NavigationInputSettings::changed, &dialog,
                     [&settings, status] { status->setText(settings.statusText()); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
} // namespace thecad::ui
