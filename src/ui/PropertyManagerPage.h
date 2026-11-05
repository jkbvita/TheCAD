#pragma once
#include <QWidget>
class QDialogButtonBox;
class QLabel;
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
};
}
