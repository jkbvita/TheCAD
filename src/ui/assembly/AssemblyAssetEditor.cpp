#include "ui/assembly/AssemblyAssetEditor.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QUuid>
#include <gp_Quaternion.hxx>
#include <algorithm>
namespace thecad::ui
{
using namespace modeling::assembly;
namespace
{
bool pointOption(const document::AssemblyReferenceOption& o)
{
    const auto& g = o.mateGeometry;
    return o.reference.connectorId.empty() &&
           (g.kind == MateGeometryKind::Point || g.kind == MateGeometryKind::Frame ||
            g.kind == MateGeometryKind::Circle ||
            (g.kind == MateGeometryKind::Line && !g.curve.IsNull()));
}
} // namespace
AssemblyAssetEditor::AssemblyAssetEditor(document::AssemblyDocument source,
                                         const std::string& componentId, QWidget* parent)
    : AssemblyPropertyEditor(
          std::move(source), "Asset Publisher",
          "Publish connection points and directions for magnetic mating. A straight edge uses its "
          "midpoint. References remain associated with this component.",
          parent)
{
    setObjectName("assemblyAssetPublisher");
    auto* form = new QFormLayout;
    m_page->settings->addLayout(form);
    m_owner = new QComboBox;
    m_owner->setObjectName("assetComponent");
    for (const auto& component : m_source.components())
        m_owner->addItem(QString::fromStdString(component.name),
                         QString::fromStdString(component.id));
    const int selectedOwner = m_owner->findData(QString::fromStdString(componentId));
    if (selectedOwner >= 0)
        m_owner->setCurrentIndex(selectedOwner);
    form->addRow("Component", m_owner);
    const auto belongs = [this](const auto& o)
    {
        return o.reference.componentId == m_owner->currentData().toString().toStdString() &&
               o.reference.connectorId.empty();
    };
    m_ground =
        referenceField(form, "assetGroundPlane", "Ground plane (optional)", [&](const auto& o)
                       { return belongs(o) && o.mateGeometry.kind == MateGeometryKind::Plane; });
    m_grounded = new QCheckBox("Use assembly ground plane");
    m_grounded->setObjectName("assetGrounded");
    m_grounded->setChecked(true);
    form->addRow(m_grounded);
    connect(m_grounded, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    m_list = new QListWidget;
    m_list->setObjectName("assetConnectors");
    m_list->setMaximumHeight(100);
    m_page->settings->addWidget(m_list);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("Add connector");
    auto* remove = new QPushButton("Remove connector");
    buttons->addWidget(add);
    buttons->addWidget(remove);
    m_page->settings->addLayout(buttons);
    auto* connectorForm = new QFormLayout;
    m_page->settings->addLayout(connectorForm);
    m_name = new QLineEdit;
    m_name->setObjectName("assetConnectorName");
    connectorForm->addRow("Connector name", m_name);
    m_point = referenceField(connectorForm, "assetConnectionPoint", "Connection point",
                             [&](const auto& o) { return belongs(o) && pointOption(o); });
    m_direction = referenceField(connectorForm, "assetConnectionDirection", "Connection direction",
                                 [&](const auto& o)
                                 { return belongs(o) && mateGeometryDirection(o.mateGeometry); });
    m_offset = new AssemblyTransformFields("connector");
    m_page->settings->addWidget(m_offset);
    auto* reverse = new QPushButton("Reverse connection direction");
    m_page->settings->addWidget(reverse);
    m_page->settings->addStretch();
    connect(m_owner, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (!m_building)
                    loadOwner();
            });
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int row)
            {
                if (m_building)
                    return;
                saveConnector();
                m_current = row;
                loadConnector();
                refreshPreview();
            });
    connect(m_name, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    m_offset->changed = [this] { refreshPreview(); };
    connect(add, &QPushButton::clicked, this,
            [this]
            {
                saveConnector();
                Draft draft;
                draft.connector.id =
                    "connector-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                draft.connector.name = "Connector" + std::to_string(m_connectors.size() + 1);
                m_connectors.push_back(draft);
                rebuildConnectors(int(m_connectors.size()) - 1);
                activate(m_point);
                refreshPreview();
            });
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                if (m_current >= 0 && m_current < int(m_connectors.size()))
                    m_connectors.erase(m_connectors.begin() + m_current);
                rebuildConnectors();
                refreshPreview();
            });
    connect(reverse, &QPushButton::clicked, this,
            [this]
            {
                auto value = m_offset->value();
                value.SetRotation(value.GetRotation() *
                                  gp_Quaternion(gp_Vec(1, 0, 0), 3.141592653589793));
                m_offset->setValue(value);
                refreshPreview();
            });
    m_building = false;
    loadOwner();
}
void AssemblyAssetEditor::refillReferences()
{
    const auto owner = m_owner->currentData().toString().toStdString();
    for (auto* box : {m_point, m_direction, m_ground})
    {
        const QSignalBlocker block(box);
        box->clear();
        box->addItem("");
        for (std::size_t i = 0; i < m_options.size(); ++i)
        {
            const auto& o = m_options[i];
            if (o.reference.componentId != owner || !o.reference.connectorId.empty())
                continue;
            const bool valid = box == m_point ? pointOption(o)
                               : box == m_direction
                                   ? mateGeometryDirection(o.mateGeometry)
                                   : o.mateGeometry.kind == MateGeometryKind::Plane;
            if (valid)
                box->addItem(o.label, int(i));
        }
    }
}
void AssemblyAssetEditor::loadOwner()
{
    const bool building = m_building;
    m_building = true;
    m_lastValid.reset();
    m_connectors.clear();
    m_current = -1;
    refillReferences();
    const auto id = m_owner->currentData().toString().toStdString();
    const auto asset = std::find_if(m_source.assets().begin(), m_source.assets().end(),
                                    [&](const auto& a) { return a.componentId == id; });
    if (asset != m_source.assets().end())
    {
        m_grounded->setChecked(asset->grounded);
        if (asset->groundPlane)
            select(m_ground, {id, *asset->groundPlane});
        for (const auto& connector : asset->connectors)
            m_connectors.push_back({connector, optionIndex({id, connector.point}),
                                    optionIndex({id, connector.direction})});
    }
    else
    {
        m_grounded->setChecked(true);
        Draft connector;
        connector.connector.id =
            "connector-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        connector.connector.name = "Connector1";
        m_connectors.push_back(connector);
    }
    rebuildConnectors();
    m_building = building;
    activate(m_connectors.empty() ? m_ground : m_point);
    refreshPreview();
}
void AssemblyAssetEditor::rebuildConnectors(int current)
{
    const bool building = m_building;
    m_building = true;
    const QSignalBlocker block(m_list);
    m_list->clear();
    for (const auto& draft : m_connectors)
        m_list->addItem(QString::fromStdString(draft.connector.name));
    m_current = m_connectors.empty() ? -1 : std::clamp(current, 0, int(m_connectors.size()) - 1);
    m_list->setCurrentRow(m_current);
    loadConnector();
    m_building = building;
}
void AssemblyAssetEditor::saveConnector()
{
    if (m_current < 0 || m_current >= int(m_connectors.size()))
        return;
    auto& draft = m_connectors[std::size_t(m_current)];
    draft.point = m_point->currentIndex() > 0 ? m_point->currentData().toInt() : -1;
    draft.direction = m_direction->currentIndex() > 0 ? m_direction->currentData().toInt() : -1;
    draft.connector.name = m_name->text().trimmed().toStdString();
    draft.connector.offset = m_offset->value();
    if (draft.point >= 0)
        draft.connector.point = m_options[std::size_t(draft.point)].reference.geometry;
    if (draft.direction >= 0)
        draft.connector.direction = m_options[std::size_t(draft.direction)].reference.geometry;
    if (auto* item = m_list->item(m_current))
        item->setText(QString::fromStdString(draft.connector.name));
}
void AssemblyAssetEditor::loadConnector()
{
    const bool building = m_building;
    m_building = true;
    const bool valid = m_current >= 0 && m_current < int(m_connectors.size());
    for (auto* field : {static_cast<QWidget*>(m_name), static_cast<QWidget*>(m_point),
                        static_cast<QWidget*>(m_direction), static_cast<QWidget*>(m_offset)})
        field->setEnabled(valid);
    if (valid)
    {
        const auto& draft = m_connectors[std::size_t(m_current)];
        m_name->setText(QString::fromStdString(draft.connector.name));
        m_point->setCurrentIndex(draft.point < 0 ? 0 : m_point->findData(draft.point));
        m_direction->setCurrentIndex(draft.direction < 0 ? 0
                                                         : m_direction->findData(draft.direction));
        m_offset->setValue(draft.connector.offset);
    }
    else
    {
        m_name->clear();
        m_point->setCurrentIndex(0);
        m_direction->setCurrentIndex(0);
    }
    m_building = building;
}
void AssemblyAssetEditor::pickComponent(const std::string& id)
{
    const int index = m_owner->findData(QString::fromStdString(id));
    if (index >= 0)
        m_owner->setCurrentIndex(index);
}
void AssemblyAssetEditor::pick(const document::AssemblyReference& ref)
{
    auto* active = m_active;
    AssemblyPropertyEditor::pick(ref);
    if (active == m_point && selected(m_point))
        activate(m_direction);
}
std::vector<document::AssemblyReference> AssemblyAssetEditor::references() const
{
    std::vector<document::AssemblyReference> refs;
    const auto id = m_owner->currentData().toString().toStdString();
    if (const auto ground = selected(m_ground))
        refs.push_back(*ground);
    if (m_candidate)
    {
        const auto asset = std::find_if(m_candidate->assets().begin(), m_candidate->assets().end(),
                                        [&](const auto& a) { return a.componentId == id; });
        if (asset != m_candidate->assets().end())
            for (const auto& connector : asset->connectors)
                refs.push_back({id, {}, connector.id});
    }
    return refs;
}
void AssemblyAssetEditor::refreshPreview()
{
    if (m_building)
        return;
    m_grounded->setEnabled(selected(m_ground).has_value());
    saveConnector();
    preview(
        [&](auto& doc)
        {
            document::PublishedAsset asset;
            asset.componentId = m_owner->currentData().toString().toStdString();
            asset.grounded = m_grounded->isChecked();
            if (const auto ground = selected(m_ground))
                asset.groundPlane = ground->geometry;
            for (const auto& draft : m_connectors)
            {
                if (draft.point < 0 || draft.direction < 0)
                    throw std::runtime_error("Select a connection point and direction for each "
                                             "connector, or remove the unfinished connector.");
                asset.connectors.push_back(draft.connector);
            }
            doc.publishAsset(asset);
        });
}
} // namespace thecad::ui
