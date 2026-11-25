#pragma once
#include "application/RecentDocuments.h"
#include <QDialog>
#include <QSettings>
class QTreeWidget;
class QLineEdit;
class QComboBox;
class QTabWidget;
class QLabel;
namespace thecad::ui {
class WelcomeWindow final : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeWindow(QSettings& settings, QWidget* parent = nullptr);
    void showRecent();
signals:
    void newDocumentRequested(thecad::application::LaunchDocument type);
    void openDocumentRequested(const QString& path);
private:
    void refresh();
    void populate(QTreeWidget* tree, bool folders, int limit = 0);
    void contextMenu(QTreeWidget* tree, bool folders, const QPoint& point);
    void browse(const QString& folder = {});
    void open(const QString& path);
    application::RecentDocuments m_recent;
    QTabWidget* m_tabs;
    QTreeWidget *m_homeDocuments, *m_homeFolders, *m_documents, *m_folders;
    QLineEdit* m_filter;
    QComboBox* m_type;
    QLabel* m_status;
};
}
