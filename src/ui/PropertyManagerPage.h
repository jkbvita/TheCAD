#pragma once
#include <QWidget>
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
namespace thecad::ui {
// Shared visual shell for feature editors; commands own their actual controls.
class PropertyManagerPage final : public QWidget {
public:
    PropertyManagerPage(const QString& title,const QString& description,QWidget* parent=nullptr);
    QVBoxLayout* settings;
    QDialogButtonBox* buttons;
    QLabel* status;
    void setStatus(const QString& message,bool error=false);
    void useCompactFeatureHeader(QPushButton* preview);
private:
    QLabel* m_heading;
    QLabel* m_description;
    QLabel* m_shortcuts;
};
}
