#include "ui/startup/LoadingScreen.h"
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>
namespace thecad::ui {
LoadingScreen::LoadingScreen(const QString& message) : QDialog(nullptr, Qt::SplashScreen) {
    setObjectName("workspaceLoadingScreen"); setWindowTitle("TheCAD — Loading");
    setAttribute(Qt::WA_QuitOnClose, true);
    setFixedSize(420, 190);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(32, 24, 32, 24); layout->setSpacing(12);
    auto* logo = new QLabel; logo->setPixmap(QPixmap(":/branding/thecad-mark.png").scaled(44,44,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    auto* name = new QLabel("TheCAD"); auto font = name->font(); font.setPointSize(20); font.setBold(true); name->setFont(font); name->setAlignment(Qt::AlignCenter); layout->addWidget(name);
    auto* status = new QLabel(message); status->setAlignment(Qt::AlignCenter); layout->addWidget(status);
    if (screen()) move(screen()->availableGeometry().center() - rect().center());
}
void LoadingScreen::cover(QWidget* workspace) {
    Q_ASSERT(workspace);
    m_workspace = workspace; workspace->setWindowOpacity(0);
    connect(workspace, &QObject::destroyed, this, [this] { hide(); deleteLater(); });
}
void LoadingScreen::finish() {
    if (m_workspace) { m_workspace->setWindowOpacity(1); m_workspace->raise(); m_workspace->activateWindow(); }
    hide(); deleteLater();
}
}
