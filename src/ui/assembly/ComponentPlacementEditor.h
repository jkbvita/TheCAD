#pragma once
#include "document/AssemblyDocument.h"
#include <QWidget>
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QToolButton;
namespace thecad::ui
{
class ComponentPlacementEditor final : public QWidget
{
    Q_OBJECT
public:
    explicit ComponentPlacementEditor(QWidget* parent = nullptr);
    void setComponent(const document::AssemblyComponent* component);
    gp_Trsf placement();
    void focusRotation(bool rotate);
signals:
    void applyRequested();
    void layoutChanged();

private:
    QDoubleSpinBox* m_values[6];
    QLabel* m_title;
    QPushButton* m_apply;
    QWidget* m_numbers;
    QToolButton* m_details;
    QLabel* m_hint;
};
} // namespace thecad::ui
