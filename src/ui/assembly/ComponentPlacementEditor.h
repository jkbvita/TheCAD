#pragma once
#include "document/AssemblyDocument.h"
#include <QWidget>
class QDoubleSpinBox;
class QLabel;
class QPushButton;
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

private:
    QDoubleSpinBox* m_values[6];
    QLabel* m_title;
    QPushButton* m_apply;
};
} // namespace thecad::ui
