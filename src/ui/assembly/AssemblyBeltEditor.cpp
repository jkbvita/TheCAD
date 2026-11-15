#include "ui/assembly/AssemblyBeltEditor.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QUuid>
#include <algorithm>
namespace thecad::ui
{
using namespace modeling::assembly;
AssemblyBeltEditor::AssemblyBeltEditor(document::AssemblyDocument source,
                                       const std::optional<document::AssemblyBelt>& editing,
                                       QWidget* parent)
    : AssemblyPropertyEditor(
          std::move(source), "Belt/Chain",
          "Select pulley cylinders, circular edges or axes in route order. Enter a diameter for an "
          "axis, or use zero to keep the measured diameter.",
          parent)
{
    setObjectName("assemblyBeltEditor");
    if (editing)
        m_belt = *editing;
    else
        m_belt.id = "belt-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    auto* form = new QFormLayout;
    m_page->settings->addLayout(form);
    m_name = new QLineEdit(QString::fromStdString(m_belt.name));
    m_name->setObjectName("beltFeatureName");
    m_name->setPlaceholderText("Automatic name");
    form->addRow("Name", m_name);
    m_page->settings->addWidget(new QLabel("Pulley components"));
    m_list = new PropertySelectionList;
    m_list->setObjectName("beltMembers");
    m_list->placeholder = "Select cylindrical faces, circular edges or axes";
    m_page->settings->addWidget(m_list);
    auto* order = new QHBoxLayout;
    m_up = new QPushButton("Move up");
    m_down = new QPushButton("Move down");
    auto* remove = new QPushButton("Remove");
    order->addWidget(m_up);
    order->addWidget(m_down);
    order->addWidget(remove);
    m_page->settings->addLayout(order);
    auto* memberForm = new QFormLayout;
    m_page->settings->addLayout(memberForm);
    m_diameter = new PropertyDimension;
    m_diameter->setObjectName("beltPulleyDiameter");
    m_diameter->setRange(0, 1e9);
    m_diameter->setSuffix(" mm");
    memberForm->addRow("Diameter (0 = measured)", m_diameter);
    m_measured = new QLabel;
    memberForm->addRow(m_measured);
    m_flip = new QCheckBox("Flip belt side of selected pulley");
    m_flip->setObjectName("beltFlipSide");
    memberForm->addRow(m_flip);
    m_location = referenceField(memberForm, "beltLocationPlane", "Belt location (optional)",
                                [](const auto& o)
                                {
                                    return o.mateGeometry.kind == MateGeometryKind::Point ||
                                           o.mateGeometry.kind == MateGeometryKind::Frame ||
                                           o.mateGeometry.kind == MateGeometryKind::Plane;
                                });
    m_driving = new QCheckBox("Driving belt length");
    m_driving->setObjectName("beltDrivingLength");
    m_length = new PropertyDimension;
    m_length->setObjectName("beltLength");
    m_length->setRange(0.000001, 1e9);
    m_length->setSuffix(" mm");
    m_length->setValue(m_belt.length);
    memberForm->addRow(m_driving);
    memberForm->addRow("Belt length", m_length);
    m_useThickness = new QCheckBox("Use belt thickness");
    m_useThickness->setObjectName("beltUseThickness");
    m_thickness = new PropertyDimension;
    m_thickness->setObjectName("beltThickness");
    m_thickness->setRange(0.000001, 1e9);
    m_thickness->setSuffix(" mm");
    m_thickness->setValue(m_belt.thickness);
    memberForm->addRow(m_useThickness);
    memberForm->addRow("Thickness", m_thickness);
    m_engage = new QCheckBox("Engage belt");
    m_engage->setObjectName("beltEngaged");
    m_createPart = new QCheckBox("Create belt sketch part");
    m_createPart->setObjectName("beltCreatePart");
    m_createPart->setToolTip("The assembly owns an associative part containing the belt path "
                             "sketch. Linked source parts are retained.");
    memberForm->addRow(m_engage);
    memberForm->addRow(m_createPart);
    m_lockPart = new QCheckBox("Lock generated part to belt sketch");
    m_lockPart->setObjectName("beltLockPart");
    memberForm->addRow(m_lockPart);
    m_page->settings->addStretch();
    for (std::size_t i = 0; i < m_belt.pulleys.size(); ++i)
        m_members.push_back({m_belt.pulleys[i], m_belt.diameters.at(i), m_belt.sides.at(i)});
    if (m_belt.location)
        select(m_location, *m_belt.location);
    m_driving->setChecked(m_belt.driving);
    m_useThickness->setChecked(m_belt.useThickness);
    m_engage->setChecked(m_belt.engaged);
    m_createPart->setChecked(m_belt.createPart);
    m_lockPart->setChecked(m_belt.lockPart);
    m_lockPart->setEnabled(m_belt.createPart);
    m_length->setEnabled(m_belt.driving);
    m_thickness->setEnabled(m_belt.useThickness);
    m_list->pick = [this]
    {
        m_active = nullptr;
        for (auto* box : m_fields)
            box->setPicking(false);
        m_list->setStyleSheet("QListWidget{border:1px solid #438bcc;}");
    };
    m_location->pick = [this]
    {
        activate(m_location);
        m_list->setStyleSheet({});
    };
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { loadMember(); });
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            [this]
            {
                if (m_building)
                    return;
                for (int i = m_list->count() - 1; i >= 0; --i)
                    if (!m_list->item(i)->isSelected())
                        m_members.erase(m_members.begin() + i);
                rebuildMembers();
                refreshPreview();
            });
    connect(m_up, &QPushButton::clicked, this,
            [this]
            {
                const int row = m_list->currentRow();
                if (row > 0)
                {
                    std::swap(m_members[std::size_t(row)], m_members[std::size_t(row - 1)]);
                    rebuildMembers(row - 1);
                    refreshPreview();
                }
            });
    connect(m_down, &QPushButton::clicked, this,
            [this]
            {
                const int row = m_list->currentRow();
                if (row >= 0 && row + 1 < int(m_members.size()))
                {
                    std::swap(m_members[std::size_t(row)], m_members[std::size_t(row + 1)]);
                    rebuildMembers(row + 1);
                    refreshPreview();
                }
            });
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                const int row = m_list->currentRow();
                if (row >= 0)
                {
                    m_members.erase(m_members.begin() + row);
                    rebuildMembers();
                    refreshPreview();
                }
            });
    connect(m_diameter, &QDoubleSpinBox::valueChanged, this, [this] { memberChanged(); });
    connect(m_flip, &QCheckBox::toggled, this, [this] { memberChanged(); });
    connect(m_name, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    for (auto* box : {m_driving, m_useThickness, m_engage, m_createPart, m_lockPart})
        connect(box, &QCheckBox::toggled, this,
                [this]
                {
                    m_length->setEnabled(m_driving->isChecked());
                    m_thickness->setEnabled(m_useThickness->isChecked());
                    m_lockPart->setEnabled(m_createPart->isChecked());
                    refreshPreview();
                });
    for (auto* value : {m_length, m_thickness})
        connect(value, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    rebuildMembers();
    m_active = nullptr;
    m_location->setPicking(false);
    m_list->pick();
    m_building = false;
    refreshPreview();
}
void AssemblyBeltEditor::rebuildMembers(int current)
{
    const bool building = m_building;
    m_building = true;
    const QSignalBlocker block(m_list);
    m_list->clear();
    for (const auto& member : m_members)
    {
        const int index = optionIndex(member.reference);
        auto* item = new QListWidgetItem(
            index < 0 ? "Missing pulley reference" : m_options[std::size_t(index)].label, m_list);
        item->setSelected(true);
    }
    m_list->refreshMembers();
    if (!m_members.empty())
        m_list->setCurrentRow(current < 0 ? int(m_members.size()) - 1
                                          : std::min(current, int(m_members.size()) - 1),
                              QItemSelectionModel::NoUpdate);
    m_building = building;
    loadMember();
}
void AssemblyBeltEditor::loadMember()
{
    const bool building = m_building;
    m_building = true;
    const int row = m_list->currentRow();
    const bool valid = row >= 0 && row < int(m_members.size());
    m_diameter->setEnabled(valid);
    m_flip->setEnabled(valid);
    m_up->setEnabled(valid && row > 0);
    m_down->setEnabled(valid && row + 1 < int(m_members.size()));
    if (valid)
    {
        const auto& member = m_members[std::size_t(row)];
        m_diameter->setValue(member.diameter);
        m_flip->setChecked(member.side < 0);
        const int index = optionIndex(member.reference);
        m_measured->setText(
            index < 0
                ? "Missing reference"
                : QString("Measured diameter: %1 mm")
                      .arg(m_options[std::size_t(index)].mateGeometry.radius * 2, 0, 'g', 10));
        auto font = m_diameter->font();
        font.setBold(member.diameter > 0);
        m_diameter->setFont(font);
    }
    else
        m_measured->clear();
    m_building = building;
}
void AssemblyBeltEditor::memberChanged()
{
    if (m_building)
        return;
    const int row = m_list->currentRow();
    if (row >= 0 && row < int(m_members.size()))
    {
        auto& member = m_members[std::size_t(row)];
        member.diameter = m_diameter->value();
        member.side = m_flip->isChecked() ? -1 : 1;
        refreshPreview();
    }
}
void AssemblyBeltEditor::pick(const document::AssemblyReference& ref)
{
    if (m_active)
    {
        AssemblyPropertyEditor::pick(ref);
        return;
    }
    const int index = optionIndex(ref);
    if (index < 0)
        return;
    const auto kind = m_options[std::size_t(index)].mateGeometry.kind;
    if (kind != MateGeometryKind::Line && kind != MateGeometryKind::Cylinder &&
        kind != MateGeometryKind::Circle)
    {
        m_page->setStatus("Choose a cylindrical face, circular edge or axis for a pulley.", true);
        return;
    }
    const auto old = std::find_if(m_members.begin(), m_members.end(),
                                  [&](const auto& m) { return optionIndex(m.reference) == index; });
    if (old == m_members.end())
        m_members.push_back({ref, 0, 1});
    else
        m_members.erase(old);
    rebuildMembers();
    refreshPreview();
}
std::vector<document::AssemblyReference> AssemblyBeltEditor::references() const
{
    std::vector<document::AssemblyReference> refs;
    for (const auto& member : m_members)
        refs.push_back(member.reference);
    if (const auto ref = selected(m_location))
        refs.push_back(*ref);
    return refs;
}
void AssemblyBeltEditor::refreshPreview()
{
    if (m_building)
        return;
    preview(
        [&](auto& doc)
        {
            auto belt = m_belt;
            belt.name = m_name->text().trimmed().toStdString();
            belt.pulleys.clear();
            belt.diameters.clear();
            belt.sides.clear();
            for (const auto& member : m_members)
            {
                belt.pulleys.push_back(member.reference);
                belt.diameters.push_back(member.diameter);
                belt.sides.push_back(member.side);
            }
            belt.location = selected(m_location);
            belt.engaged = m_engage->isChecked();
            belt.driving = m_driving->isChecked();
            belt.useThickness = m_useThickness->isChecked();
            belt.createPart = m_createPart->isChecked();
            belt.lockPart = m_lockPart->isChecked();
            belt.length = m_length->value();
            belt.thickness = m_thickness->value();
            doc.setBelt(belt);
            const auto current = std::find_if(doc.belts().begin(), doc.belts().end(),
                                              [&](const auto& b) { return b.id == belt.id; });
            m_belt = *current;
            if (!belt.driving)
            {
                const QSignalBlocker block(m_length);
                m_length->setValue(m_belt.length);
            }
        });
}
} // namespace thecad::ui
