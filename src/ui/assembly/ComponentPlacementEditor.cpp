#include "ui/assembly/ComponentPlacementEditor.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <gp_Quaternion.hxx>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace thecad::ui
{
ComponentPlacementEditor::ComponentPlacementEditor(QWidget* parent) : QWidget(parent)
{
    setObjectName("componentPlacementPanel");
    auto* layout = new QVBoxLayout(this);
    m_title = new QLabel("Select a component");
    m_title->setWordWrap(true);
    layout->addWidget(m_title);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    const char* labels[]{"X", "Y", "Z", "Rotation X", "Rotation Y", "Rotation Z"};
    const char* names[]{"componentX",         "componentY",         "componentZ",
                        "componentRotationX", "componentRotationY", "componentRotationZ"};
    for (int i = 0; i < 6; ++i)
    {
        auto* value = new PropertyDimension;
        m_values[i] = value;
        value->setObjectName(names[i]);
        value->setRange(i < 3 ? -1e9 : -360, i < 3 ? 1e9 : 360);
        value->setSuffix(i < 3 ? " mm" : "°");
        value->setKeyboardTracking(false);
        form->addRow(labels[i], value);
    }
    m_apply = new QPushButton("Apply placement");
    m_apply->setObjectName("applyComponentPlacement");
    layout->addWidget(m_apply);
    layout->addStretch();
    connect(m_apply, &QPushButton::clicked, this, &ComponentPlacementEditor::applyRequested);
    setComponent(nullptr);
}
void ComponentPlacementEditor::setComponent(const document::AssemblyComponent* component)
{
    m_title->setText(component ? QString::fromStdString(component->name) +
                                     (component->fixed ? " · Fixed" : " · Floating")
                               : "Select a component");
    double values[6]{};
    if (component)
    {
        const auto t = component->placement.TranslationPart();
        values[0] = t.X();
        values[1] = t.Y();
        values[2] = t.Z();
        component->placement.GetRotation().GetEulerAngles(gp_Extrinsic_XYZ, values[3], values[4],
                                                          values[5]);
        for (int i = 3; i < 6; ++i)
            values[i] *= 180 / 3.14159265358979323846;
    }
    for (int i = 0; i < 6; ++i)
    {
        m_values[i]->setValue(values[i]);
        m_values[i]->setEnabled(component && !component->fixed);
    }
    m_apply->setEnabled(component && !component->fixed);
}
gp_Trsf ComponentPlacementEditor::placement()
{
    for (auto* value : m_values)
        value->interpretText();
    gp_Quaternion q;
    q.SetEulerAngles(gp_Extrinsic_XYZ, m_values[3]->value() * 3.14159265358979323846 / 180,
                     m_values[4]->value() * 3.14159265358979323846 / 180,
                     m_values[5]->value() * 3.14159265358979323846 / 180);
    gp_Trsf result;
    result.SetRotation(q);
    result.SetTranslationPart({m_values[0]->value(), m_values[1]->value(), m_values[2]->value()});
    return result;
}
void ComponentPlacementEditor::focusRotation(bool rotate)
{
    m_values[rotate ? 3 : 0]->setFocus();
    m_values[rotate ? 3 : 0]->selectAll();
}
} // namespace thecad::ui
