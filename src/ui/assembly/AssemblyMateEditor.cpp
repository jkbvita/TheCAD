#include "ui/assembly/AssemblyMateEditor.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <Standard_Failure.hxx>
#include <QUuid>

namespace thecad::ui
{
using document::AssemblyMateKind;
AssemblyMateEditor::AssemblyMateEditor(document::AssemblyDocument source,
                                       const std::optional<document::AssemblyMate>& editing,
                                       QWidget* parent)
    : QWidget(parent), m_source(std::move(source))
{
    setObjectName("assemblyMateEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_page = new PropertyManagerPage("Mate",
                                     "Select two planar faces/planes or cylindrical faces/axes in "
                                     "the viewport or component tree.",
                                     this);
    layout->addWidget(m_page);
    auto* form = new QFormLayout;
    m_form = form;
    m_page->settings->addLayout(form);
    const auto append = [&](const std::string& id)
    {
        auto options = m_source.referenceOptions(id);
        m_options.insert(m_options.end(), options.begin(), options.end());
    };
    append({});
    for (const auto& component : m_source.components())
        append(component.id);
    auto* first = new PropertyReference;
    auto* second = new PropertyReference;
    m_first = first;
    m_second = second;
    first->setObjectName("mateFirstReference");
    second->setObjectName("mateSecondReference");
    for (auto* box : {m_first, m_second})
    {
        box->addItem("");
        for (int i = 0; i < int(m_options.size()); ++i)
            box->addItem(m_options[std::size_t(i)].label, i);
    }
    first->pick = [this] { activate(0); };
    second->pick = [this] { activate(1); };
    form->addRow("First reference", first);
    form->addRow("Second reference", second);
    m_kind = new QComboBox;
    m_kind->setObjectName("assemblyMateType");
    form->addRow("Mate type", m_kind);
    m_value = new PropertyDimension;
    m_value->setObjectName("assemblyMateValue");
    m_value->setRange(-1e9, 1e9);
    m_value->setValue(10);
    form->addRow("Value", m_value);
    m_flip = new QCheckBox("Flip alignment");
    m_flip->setObjectName("assemblyMateFlip");
    form->addRow(m_flip);
    m_page->settings->addStretch();
    m_mate.id = "mate-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    if (editing)
    {
        m_mate = *editing;
        m_first->setCurrentIndex(optionIndex(editing->first) + 1);
        m_second->setCurrentIndex(optionIndex(editing->second) + 1);
        m_value->setValue(editing->value);
        m_flip->setChecked(editing->alignment < 0);
    }
    for (auto* box : {m_first, m_second})
        connect(box, &QComboBox::currentIndexChanged, this,
                [this]
                {
                    if (m_initializing)
                        return;
                    const auto refs = references();
                    if (refs.size() == 2)
                    {
                        const QSignalBlocker block(m_flip);
                        const auto a = m_source.referenceGeometry(refs[0]),
                                   b = m_source.referenceGeometry(refs[1]);
                        m_flip->setChecked(a.frame.Direction().Dot(b.frame.Direction()) < 0);
                    }
                    refreshPreview();
                });
    connect(m_kind, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (m_initializing || m_refreshing)
                    return;
                const QSignalBlocker block(m_value);
                m_value->setValue(
                    m_kind->currentData().toInt() == int(AssemblyMateKind::Angle) ? 90 : 10);
                refreshPreview();
            });
    connect(m_value, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    connect(m_flip, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    m_initializing = false;
    activate(0);
    refreshPreview();
}
int AssemblyMateEditor::optionIndex(const document::AssemblyReference& reference) const
{
    for (int i = 0; i < int(m_options.size()); ++i)
    {
        const auto& ref = m_options[std::size_t(i)].reference;
        if (ref.componentId != reference.componentId ||
            ref.geometry.source != reference.geometry.source)
            continue;
        if (!reference.geometry.topologyName.empty())
        {
            if (ref.geometry.topologyName == reference.geometry.topologyName)
                return i;
        }
        else if (ref.geometry.featureId == reference.geometry.featureId &&
                 ref.geometry.entityId == reference.geometry.entityId &&
                 ref.geometry.role == reference.geometry.role &&
                 ref.geometry.index == reference.geometry.index)
            return i;
    }
    return -1;
}
void AssemblyMateEditor::activate(int index)
{
    m_active = index;
    static_cast<PropertyReference*>(m_first)->setPicking(index == 0);
    static_cast<PropertyReference*>(m_second)->setPicking(index == 1);
}
void AssemblyMateEditor::pick(const document::AssemblyReference& reference)
{
    const int option = optionIndex(reference);
    if (option < 0)
        return;
    auto* box = m_active == 0 ? m_first : m_second;
    box->setCurrentIndex(option + 1);
    if (m_active == 0 && m_second->currentIndex() == 0)
        activate(1);
}
std::vector<document::AssemblyReference> AssemblyMateEditor::references() const
{
    std::vector<document::AssemblyReference> refs;
    for (auto* box : {m_first, m_second})
        if (box->currentIndex() > 0)
            refs.push_back(m_options.at(std::size_t(box->currentData().toInt())).reference);
    return refs;
}
void AssemblyMateEditor::refreshPreview()
{
    if (m_initializing || m_refreshing)
        return;
    m_refreshing = true;
    m_candidate.reset();
    m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    try
    {
        const auto refs = references();
        const QSignalBlocker block(m_kind);
        const auto wanted = m_kind->currentIndex() < 0
                                ? m_mate.kind
                                : AssemblyMateKind(m_kind->currentData().toInt());
        m_kind->clear();
        for (auto kind : {AssemblyMateKind::Coincident, AssemblyMateKind::Concentric,
                          AssemblyMateKind::Distance, AssemblyMateKind::Angle})
        {
            bool valid = refs.size() < 2;
            if (refs.size() == 2)
                valid = modeling::assembly::validMateTypes(
                    kind, m_source.referenceGeometry(refs[0]).kind,
                    m_source.referenceGeometry(refs[1]).kind);
            if (valid)
                m_kind->addItem(modeling::assembly::mateName(kind), int(kind));
        }
        const int selected = m_kind->findData(int(wanted));
        m_kind->setCurrentIndex(selected < 0 ? 0 : selected);
        const auto kind = AssemblyMateKind(m_kind->currentData().toInt());
        {
            const QSignalBlocker valueBlock(m_value);
            m_value->setRange(kind == AssemblyMateKind::Angle ? 0 : -1e9,
                              kind == AssemblyMateKind::Angle ? 180 : 1e9);
            m_value->setSuffix(kind == AssemblyMateKind::Angle ? "°" : " mm");
        }
        const bool numeric = kind == AssemblyMateKind::Angle || kind == AssemblyMateKind::Distance;
        m_form->setRowVisible(m_value, numeric);
        m_flip->setVisible(kind != AssemblyMateKind::Angle);
        if (refs.size() != 2)
            m_page->setStatus("Select two mate references.");
        else if (m_kind->count() == 0)
            m_page->setStatus(
                "This reference combination has no supported mate. Select two planes or two axes.",
                true);
        else
        {
            auto mate = m_mate;
            mate.kind = kind;
            mate.first = refs[0];
            mate.second = refs[1];
            mate.value = numeric ? m_value->value() : 0;
            mate.alignment = m_flip->isChecked() ? -1 : 1;
            auto candidate = m_source;
            candidate.setMate(mate);
            m_candidate = std::move(candidate);
            m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
            m_page->setStatus(QString("Preview ready · %1 assembly degree(s) of freedom remain")
                                  .arg(m_candidate->solution().degreesOfFreedom));
        }
    }
    catch (const Standard_Failure& failure)
    {
        m_page->setStatus(QString::fromUtf8(failure.GetMessageString()), true);
    }
    catch (const std::exception& failure)
    {
        m_page->setStatus(QString::fromUtf8(failure.what()), true);
    }
    m_refreshing = false;
    emit previewChanged();
}
} // namespace thecad::ui
