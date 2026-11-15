#include "ui/assembly/AssemblyPropertyEditor.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <Standard_Failure.hxx>
#include <gp_Quaternion.hxx>
#include <cmath>
namespace thecad::ui
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}
AssemblyTransformFields::AssemblyTransformFields(const QString& prefix, QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 6; ++i)
    {
        auto* field = new PropertyDimension;
        m_values[i] = field;
        field->setObjectName(prefix + (i < 3 ? "Offset" : "Rotation") + QChar('X' + i % 3));
        field->setRange(-1e9, 1e9);
        field->setSuffix(i < 3 ? " mm" : "°");
        form->addRow((i < 3 ? "Offset " : "Rotation ") + QString(QChar('X' + i % 3)), field);
        connect(field, &QDoubleSpinBox::valueChanged, this,
                [this]
                {
                    if (!m_loading && changed)
                        changed();
                });
    }
}
gp_Trsf AssemblyTransformFields::value() const
{
    gp_Quaternion q;
    q.SetEulerAngles(gp_Extrinsic_XYZ, m_values[3]->value() * pi / 180,
                     m_values[4]->value() * pi / 180, m_values[5]->value() * pi / 180);
    gp_Trsf t;
    t.SetRotation(q);
    t.SetTranslationPart({m_values[0]->value(), m_values[1]->value(), m_values[2]->value()});
    return t;
}
void AssemblyTransformFields::setValue(const gp_Trsf& t)
{
    m_loading = true;
    for (int i = 0; i < 3; ++i)
        m_values[i]->setValue(t.TranslationPart().Coord(i + 1));
    double x, y, z;
    t.GetRotation().GetEulerAngles(gp_Extrinsic_XYZ, x, y, z);
    m_values[3]->setValue(x * 180 / pi);
    m_values[4]->setValue(y * 180 / pi);
    m_values[5]->setValue(z * 180 / pi);
    m_loading = false;
}
AssemblyPropertyEditor::AssemblyPropertyEditor(document::AssemblyDocument source,
                                               const QString& title, const QString& description,
                                               QWidget* parent)
    : QWidget(parent), m_source(std::move(source))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_page = new PropertyManagerPage(title, description, this);
    m_page->useAssemblyHeader();
    layout->addWidget(m_page);
    const auto append = [&](const std::string& id)
    {
        auto options = m_source.referenceOptions(id);
        m_options.insert(m_options.end(), options.begin(), options.end());
    };
    append({});
    for (const auto& component : m_source.components())
        append(component.id);
}
int AssemblyPropertyEditor::optionIndex(const document::AssemblyReference& wanted) const
{
    for (std::size_t i = 0; i < m_options.size(); ++i)
    {
        const auto& ref = m_options[i].reference;
        const auto& a = ref.geometry;
        const auto& b = wanted.geometry;
        if (ref.componentId == wanted.componentId && ref.connectorId == wanted.connectorId &&
            a.source == b.source &&
            (!b.topologyName.empty() ? a.topologyName == b.topologyName
                                     : a.featureId == b.featureId && a.entityId == b.entityId &&
                                           a.role == b.role && a.index == b.index))
            return int(i);
    }
    return -1;
}
PropertyReference* AssemblyPropertyEditor::referenceField(
    QFormLayout* form, const char* id, const QString& label,
    const std::function<bool(const document::AssemblyReferenceOption&)>& filter)
{
    auto* box = new PropertyReference;
    box->setObjectName(id);
    box->addItem("");
    for (std::size_t i = 0; i < m_options.size(); ++i)
        if (filter(m_options[i]))
            box->addItem(m_options[i].label, int(i));
    form->addRow(label, box);
    box->pick = [this, box] { activate(box); };
    m_fields.push_back(box);
    if (!m_active)
        activate(box);
    connect(box, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (!m_building)
                    refreshPreview();
            });
    return box;
}
void AssemblyPropertyEditor::activate(PropertyReference* field)
{
    m_active = field;
    for (auto* box : m_fields)
        box->setPicking(box == field);
}
std::optional<document::AssemblyReference>
AssemblyPropertyEditor::selected(PropertyReference* field) const
{
    if (field->currentIndex() <= 0)
        return {};
    return m_options.at(std::size_t(field->currentData().toInt())).reference;
}
void AssemblyPropertyEditor::select(PropertyReference* field,
                                    const document::AssemblyReference& ref)
{
    const int index = optionIndex(ref);
    field->setCurrentIndex(index < 0 ? 0 : field->findData(index));
}
void AssemblyPropertyEditor::pick(const document::AssemblyReference& ref)
{
    if (!m_active)
        return;
    const int option = optionIndex(ref), index = m_active->findData(option);
    if (option < 0 || index < 0)
    {
        m_page->setStatus(
            "This entity does not fit the active selection box. Choose a compatible reference.",
            true);
        return;
    }
    m_active->setCurrentIndex(index);
}
void AssemblyPropertyEditor::preview(
    const std::function<void(document::AssemblyDocument&)>& operation)
{
    if (m_building)
        return;
    m_candidate.reset();
    m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    try
    {
        auto candidate = m_lastValid ? *m_lastValid : m_source;
        operation(candidate);
        m_candidate = std::move(candidate);
        m_lastValid = m_candidate;
        m_page->setStatus(
            "Preview ready. Confirm to apply, or cancel to retain the original assembly.");
        m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
    catch (const Standard_Failure& e)
    {
        m_page->setStatus(QString::fromUtf8(e.GetMessageString()), true);
    }
    catch (const std::exception& e)
    {
        m_page->setStatus(QString::fromUtf8(e.what()), true);
    }
    emit previewChanged();
}
} // namespace thecad::ui
