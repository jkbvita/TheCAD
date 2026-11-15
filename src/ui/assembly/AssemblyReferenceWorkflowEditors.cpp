#include "ui/assembly/AssemblyReferenceWorkflowEditors.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QFileDialog>
#include <QCheckBox>
#include <Standard_Failure.hxx>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QUuid>
namespace thecad::ui
{
using namespace modeling::assembly;
AssemblyGroundEditor::AssemblyGroundEditor(document::AssemblyDocument source, QWidget* parent)
    : AssemblyPropertyEditor(std::move(source), "Ground Plane",
                             "Select a plane or planar face for published assets. You can select "
                             "hidden origin planes from the tree.",
                             parent)
{
    setObjectName("assemblyGroundEditor");
    auto* form = new QFormLayout;
    m_page->settings->addLayout(form);
    m_plane = referenceField(form, "assemblyGroundPlane", "Ground plane", [](const auto& o)
                             { return o.mateGeometry.kind == MateGeometryKind::Plane; });
    if (m_source.groundPlane())
        select(m_plane, *m_source.groundPlane());
    auto* remove = new QPushButton("Remove assembly ground plane");
    remove->setEnabled(m_source.groundPlane().has_value());
    m_page->settings->addWidget(remove);
    m_page->settings->addStretch();
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                m_remove = true;
                {
                    const QSignalBlocker block(m_plane);
                    m_plane->setCurrentIndex(0);
                }
                refreshPreview();
            });
    m_building = false;
    refreshPreview();
}
std::vector<document::AssemblyReference> AssemblyGroundEditor::references() const
{
    if (const auto ref = selected(m_plane))
        return {*ref};
    return {};
}
void AssemblyGroundEditor::refreshPreview()
{
    if (m_building)
        return;
    preview(
        [&](auto& doc)
        {
            const auto ref = selected(m_plane);
            if (ref)
                m_remove = false;
            if (!ref && !m_remove)
                throw std::runtime_error("Select a plane or planar face, or choose Remove to "
                                         "remove the existing ground plane.");
            doc.setGroundPlane(ref);
        });
    if (m_candidate && m_remove)
        m_page->setStatus(
            "Preview: the assembly ground plane and its generated ground mates will be removed.");
}
AssemblyInPlaceEditor::AssemblyInPlaceEditor(document::AssemblyDocument source,
                                             const std::optional<document::InPlacePart>& editing,
                                             QWidget* parent)
    : AssemblyPropertyEditor(std::move(source), "New Part in Context",
                             "Choose a part template and a support plane. The assembly owns the "
                             "new copy, which follows its support. The template file is retained.",
                             parent)
{
    setObjectName("assemblyInPlaceEditor");
    if (editing)
    {
        m_definition = *editing;
        m_part = *m_source.component(editing->componentId).part;
    }
    else
        m_definition.id =
            "inplace-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    m_template = new QLabel(editing ? "Assembly-owned part" : "Choose a .thepart template");
    m_template->setObjectName("inContextTemplate");
    m_template->setWordWrap(true);
    m_page->settings->addWidget(m_template);
    auto* browse =
        new QPushButton(editing ? "Replace part from template…" : "Choose part template…");
    browse->setObjectName("chooseInContextTemplate");
    m_page->settings->addWidget(browse);
    auto* form = new QFormLayout;
    m_page->settings->addLayout(form);
    m_support = referenceField(form, "inContextSupport", "Support plane or origin",
                               [](const auto& o)
                               {
                                   return o.mateGeometry.kind == MateGeometryKind::Plane ||
                                          o.mateGeometry.kind == MateGeometryKind::Frame;
                               });
    m_linked = new QCheckBox("Keep in-context relationship");
    m_linked->setObjectName("inContextLinked");
    m_linked->setChecked(m_definition.linked);
    form->addRow(m_linked);
    m_support->setEnabled(m_definition.linked);
    connect(m_linked, &QCheckBox::toggled, this,
            [this](bool linked)
            {
                m_support->setEnabled(linked);
                refreshPreview();
            });
    m_offset = new AssemblyTransformFields("inContext");
    m_offset->setValue(m_definition.offset);
    m_page->settings->addWidget(m_offset);
    m_page->settings->addStretch();
    if (editing)
        select(m_support, editing->support);
    connect(browse, &QPushButton::clicked, this,
            [this]
            {
                const auto path = chooseTemplatePath();
                if (!path.isEmpty())
                    setTemplateFile(path);
            });
    m_offset->changed = [this] { refreshPreview(); };
    m_building = false;
    refreshPreview();
}
QString AssemblyInPlaceEditor::chooseTemplatePath()
{
    return QFileDialog::getOpenFileName(this, "Choose part template", {},
                                        "TheCAD Part (*.thepart *.tcpart)");
}
bool AssemblyInPlaceEditor::setTemplateFile(const QString& path)
{
    try
    {
        m_part = document::PartDocument::load(path);
        m_template->setText(QFileInfo(path).fileName());
        refreshPreview();
        return true;
    }
    catch (const Standard_Failure& e)
    {
        m_page->setStatus(QString::fromUtf8(e.GetMessageString()), true);
        return false;
    }
    catch (const std::exception& e)
    {
        m_page->setStatus(QString::fromUtf8(e.what()), true);
        return false;
    }
}
std::vector<document::AssemblyReference> AssemblyInPlaceEditor::references() const
{
    if (const auto ref = selected(m_support))
        return {*ref};
    return {};
}
void AssemblyInPlaceEditor::refreshPreview()
{
    if (m_building)
        return;
    preview(
        [&](auto& doc)
        {
            if (!m_part)
                throw std::runtime_error(
                    "Choose a .thepart template for the new assembly-owned component.");
            const auto ref = selected(m_support);
            if (!ref && m_linked->isChecked())
                throw std::runtime_error("Select the support plane, planar face or origin.");
            auto definition = m_definition;
            definition.support = ref.value_or(document::AssemblyReference{});
            definition.offset = m_offset->value();
            definition.linked = m_linked->isChecked();
            const auto id = doc.setInPlacePart(definition, *m_part);
            for (const auto& part : doc.inPlaceParts())
                if (part.componentId == id)
                {
                    m_definition = part;
                    if (!part.linked)
                    {
                        m_offset->setValue(part.offset);
                        const QSignalBlocker block(m_support);
                        select(m_support, part.support);
                    }
                    break;
                }
        });
}
} // namespace thecad::ui
