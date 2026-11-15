#include "ui/assembly/AssemblyMagneticEditor.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QVBoxLayout>
namespace thecad::ui
{
AssemblyMagneticEditor::AssemblyMagneticEditor(document::AssemblyDocument source,
                                               const document::AssemblyMate& mate, QWidget* parent)
    : AssemblyPropertyEditor(std::move(source), "Magnetic Mate",
                             "Choose published connectors. Locked connections stay joined; moving "
                             "an unlocked asset disconnects its magnetic mate.",
                             parent),
      m_mate(mate)
{
    setObjectName("assemblyMagneticEditor");
    auto* form = new QFormLayout;
    m_page->settings->addLayout(form);
    const auto filter = [](const auto& o) { return !o.reference.connectorId.empty(); };
    m_first = referenceField(form, "magneticFirstConnector", "First connector", filter);
    m_second = referenceField(form, "magneticSecondConnector", "Second connector", filter);
    select(m_first, mate.first);
    select(m_second, mate.second);
    m_locked = new QCheckBox("Lock magnetic mate");
    m_locked->setObjectName("magneticLocked");
    m_locked->setChecked(mate.definition && mate.definition->parameters.magnetic.locked);
    form->addRow(m_locked);
    m_page->settings->addStretch();
    connect(m_locked, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    m_building = false;
    activate(m_first);
    refreshPreview();
}
std::vector<document::AssemblyReference> AssemblyMagneticEditor::references() const
{
    std::vector<document::AssemblyReference> refs;
    for (auto* field : {m_first, m_second})
        if (const auto ref = selected(field))
            refs.push_back(*ref);
    return refs;
}
void AssemblyMagneticEditor::refreshPreview()
{
    if (m_building)
        return;
    preview(
        [&](auto& doc)
        {
            const auto first = selected(m_first), second = selected(m_second);
            if (!first || !second)
                throw std::runtime_error("Choose two published component connectors.");
            auto mate = m_mate;
            mate.first = *first;
            mate.second = *second;
            mate.status = modeling::assembly::MateStatus::Active;
            mate.diagnostic.clear();
            mate.definition = document::AssemblyMate::Definition{};
            mate.definition->groups = {{"first", {*first}}, {"second", {*second}}};
            mate.definition->parameters.magnetic.locked = m_locked->isChecked();
            doc.setMate(mate);
        });
}
} // namespace thecad::ui
