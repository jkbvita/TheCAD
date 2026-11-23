#include "ui/assembly/AssemblyMateEditor.h"
#include "ui/assembly/AssemblyMateParameters.h"
#include "ui/assembly/MateIcons.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include "document/storage/AssemblyMateData.h"
#include "modeling/assembly/MateEquations.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabBar>
#include <QVBoxLayout>
#include <Standard_Failure.hxx>
#include <QUuid>
#include <algorithm>
namespace thecad::ui
{
using namespace modeling::assembly;
AssemblyMateEditor::AssemblyMateEditor(document::AssemblyDocument source,
                                       const std::optional<document::AssemblyMate>& editing,
                                       QWidget* parent)
    : QWidget(parent), m_source(std::move(source))
{
    setObjectName("assemblyMateEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_page = new PropertyManagerPage("Mate",
                                     "Select entities in the viewport or component tree. Click a "
                                     "selection box to replace its references.",
                                     this);
    layout->addWidget(m_page);
    const auto append = [&](const std::string& id)
    {
        auto options = m_source.referenceOptions(id);
        m_options.insert(m_options.end(), options.begin(), options.end());
    };
    append({});
    for (const auto& component : m_source.components())
        append(component.id);
    m_family = new QTabBar;
    m_family->setObjectName("assemblyMateFamily");
    m_family->setExpanding(false);
    for (const auto* name : {"Standard", "Advanced", "Mechanical"})
        m_family->addTab(name);
    m_page->settings->addWidget(m_family);
    m_form = new QFormLayout;
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_page->settings->addLayout(m_form);
    m_kind = new QComboBox;
    m_kind->setObjectName("assemblyMateType");
    m_form->addRow("Mate type", m_kind);
    m_name = new QLineEdit;
    m_name->setObjectName("assemblyMateName");
    m_name->setPlaceholderText("Automatic name");
    m_form->addRow("Name", m_name);
    m_selectionPanel = new QWidget;
    m_selectionLayout = new QVBoxLayout(m_selectionPanel);
    m_selectionLayout->setContentsMargins(0, 0, 0, 0);
    m_page->settings->addWidget(m_selectionPanel);
    auto* values = new QFormLayout;
    m_page->settings->addLayout(values);
    m_value = new PropertyDimension;
    m_value->setObjectName("assemblyMateValue");
    m_value->setRange(-1e9, 1e9);
    m_value->setValue(10);
    values->addRow("Value", m_value);
    m_flip = new QCheckBox("Flip alignment");
    m_flip->setObjectName("assemblyMateFlip");
    values->addRow(m_flip);
    // Keep the value layout for hiding its label together with the field.
    m_form = values;
    m_parameters = new AssemblyMateParameters;
    m_page->settings->addWidget(m_parameters);
    m_keepOpen = new QCheckBox("Keep Mate open after confirmation");
    m_keepOpen->setObjectName("assemblyMateKeepOpen");
    m_page->settings->addWidget(m_keepOpen);
    m_multiple = new QCheckBox("Mate multiple selections to one common reference");
    m_multiple->setObjectName("assemblyMateMultiple");
    m_multiple->setEnabled(!editing);
    m_page->settings->addWidget(m_multiple);
    m_conflicts = new QWidget;
    auto* conflictLayout = new QVBoxLayout(m_conflicts);
    conflictLayout->setContentsMargins(0, 0, 0, 0);
    auto* keep = new QPushButton("Keep this mate unsolved");
    auto* forceButton = new QPushButton("Choose mates to leave unsolved…");
    conflictLayout->addWidget(keep);
    conflictLayout->addWidget(forceButton);
    m_page->settings->addWidget(m_conflicts);
    m_conflicts->hide();
    m_page->settings->addStretch();
    m_mate.id = "mate-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    if (editing)
        m_mate = *editing;
    m_name->setText(QString::fromStdString(m_mate.name));
    connect(m_name, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    m_parameters->setParameters(m_mate.definition ? m_mate.definition->parameters
                                                  : MateParameters{});
    m_value->setValue(editing ? editing->value : 10);
    m_flip->setChecked(m_mate.alignment < 0);
    m_family->setCurrentIndex(int(mateFamily(m_mate.kind)));
    changeFamily();
    m_kind->setCurrentIndex(m_kind->findData(int(m_mate.kind)));
    buildSelections(m_mate.definition ? m_mate.definition->groups
                    : editing
                        ? std::vector<document::AssemblyMate::Group>{{"first", {editing->first}},
                                                                     {"second", {editing->second}}}
                        : std::vector<document::AssemblyMate::Group>{});
    connect(m_family, &QTabBar::currentChanged, this,
            [this]
            {
                changeFamily();
                changeKind();
            });
    connect(m_kind, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (!m_initializing)
                    changeKind();
            });
    connect(m_value, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    connect(m_flip, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    connect(m_parameters, &AssemblyMateParameters::changed, this,
            [this]
            {
                if (m_initializing)
                    return;
                const bool hinge = MateKind(m_kind->currentData().toInt()) == MateKind::Hinge;
                if (hinge &&
                    int(m_selections.size()) != (m_parameters->parameters().hinge.limits ? 3 : 2))
                    buildSelections(groups());
                refreshPreview();
            });
    connect(keep, &QPushButton::clicked, this, &AssemblyMateEditor::keepUnsolved);
    connect(forceButton, &QPushButton::clicked, this, &AssemblyMateEditor::force);
    connect(m_multiple, &QCheckBox::toggled, this,
            [this]
            {
                const auto selected = references();
                buildSelections();
                m_initializing = true;
                for (const auto& ref : selected)
                    pick(ref);
                m_initializing = false;
                refreshPreview();
            });
    m_initializing = false;
    activate(0);
    refreshPreview();
}
void AssemblyMateEditor::changeFamily()
{
    const QSignalBlocker block(m_kind);
    const auto wanted = m_kind->currentData();
    m_kind->clear();
    for (int i = 0; i < 22; ++i)
        if (int(mateFamily(MateKind(i))) == m_family->currentIndex())
            m_kind->addItem(assemblyMateIcon(MateKind(i)), mateName(MateKind(i)), i);
    const int index = m_kind->findData(wanted);
    m_kind->setCurrentIndex(index < 0 ? 0 : index);
}
void AssemblyMateEditor::changeKind()
{
    if (m_initializing)
        return;
    const auto old = references();
    const auto kind = MateKind(m_kind->currentData().toInt());
    auto params = m_parameters->parameters();
    if (kind == MateKind::Gear)
        params.coupler.useGeometryRatio = true;
    params.position.mode = (kind == MateKind::Path || kind == MateKind::Cam)
                               ? PositionMode::Free
                               : PositionMode::Centered;
    m_parameters->setParameters(params);
    buildSelections();
    m_initializing = true;
    for (const auto& ref : old)
        pick(ref);
    m_initializing = false;
    {
        const QSignalBlocker block(m_value);
        m_value->setValue(kind == MateKind::Angle ? 90 : 10);
    }
    refreshPreview();
}
int AssemblyMateEditor::optionIndex(const document::AssemblyReference& reference) const
{
    for (int i = 0; i < int(m_options.size()); ++i)
    {
        const auto& ref = m_options[std::size_t(i)].reference;
        if (ref.componentId != reference.componentId || ref.connectorId != reference.connectorId ||
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
void AssemblyMateEditor::buildSelections(const std::vector<document::AssemblyMate::Group>& selected)
{
    const bool initializing = m_initializing;
    m_initializing = true;
    while (auto* item = m_selectionLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    m_selections.clear();
    for (auto role : mateRoles(MateKind(m_kind->currentData().toInt()),
                               m_parameters->parameters().hinge.limits))
    {
        if (m_multiple->isChecked() && m_family->currentIndex() == 0 && role.key == "second")
        {
            role.maximum = 64;
            role.label = "Mate these references to the common reference";
        }
        const int index = int(m_selections.size());
        m_selectionLayout->addWidget(new QLabel(QString::fromStdString(role.label)));
        QWidget* widget = nullptr;
        const auto found = std::find_if(selected.begin(), selected.end(),
                                        [&](const auto& group) { return group.role == role.key; });
        if (role.maximum == 1)
        {
            auto* box = new PropertyReference;
            widget = box;
            box->addItem("");
            for (int i = 0; i < int(m_options.size()); ++i)
                box->addItem(m_options[std::size_t(i)].label, i);
            box->pick = [this, index] { activate(index); };
            if (found != selected.end() && !found->references.empty())
                box->setCurrentIndex(optionIndex(found->references[0]) + 1);
            connect(box, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
        }
        else
        {
            auto* list = new PropertySelectionList;
            widget = list;
            list->placeholder = QString::fromStdString(role.label);
            list->setMaximumHeight(100);
            list->pick = [this, index] { activate(index); };
            for (int i = 0; i < int(m_options.size()); ++i)
            {
                auto* item = new QListWidgetItem(m_options[std::size_t(i)].label, list);
                item->setData(Qt::UserRole, i);
                if (found != selected.end())
                    for (const auto& ref : found->references)
                        if (optionIndex(ref) == i)
                            item->setSelected(true);
            }
            list->refreshMembers();
            connect(list, &QListWidget::itemSelectionChanged, this, [this] { refreshPreview(); });
        }
        widget->setObjectName(
            role.key == "first" ? "mateFirstReference"
            : role.key == "second"
                ? "mateSecondReference"
                : QString("mateSelection_%1").arg(QString::fromStdString(role.key)));
        m_selections.push_back({role, widget});
        m_selectionLayout->addWidget(widget);
    }
    m_initializing = initializing;
    activate(0);
}
std::vector<document::AssemblyMate::Group> AssemblyMateEditor::groups() const
{
    std::vector<document::AssemblyMate::Group> result;
    for (const auto& selection : m_selections)
    {
        document::AssemblyMate::Group group;
        group.role = selection.role.key;
        if (auto* box = qobject_cast<QComboBox*>(selection.widget))
        {
            if (box->currentIndex() > 0)
                group.references.push_back(
                    m_options.at(std::size_t(box->currentData().toInt())).reference);
        }
        else if (auto* list = qobject_cast<QListWidget*>(selection.widget))
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->isSelected())
                    group.references.push_back(
                        m_options.at(std::size_t(list->item(i)->data(Qt::UserRole).toInt()))
                            .reference);
        if (!group.references.empty() || selection.role.minimum > 0)
            result.push_back(std::move(group));
    }
    return result;
}
std::vector<document::AssemblyReference> AssemblyMateEditor::references() const
{
    std::vector<document::AssemblyReference> result;
    for (const auto& group : groups())
        result.insert(result.end(), group.references.begin(), group.references.end());
    return result;
}
void AssemblyMateEditor::activate(int index)
{
    m_active = index;
    for (int i = 0; i < int(m_selections.size()); ++i)
    {
        auto* widget = m_selections[std::size_t(i)].widget;
        if (auto* box = dynamic_cast<PropertyReference*>(widget))
            box->setPicking(i == index);
        else
        {
            widget->setProperty("picking", i == index);
            widget->setStyleSheet(i == index ? "QListWidget{border:1px solid #438bcc;}" : "");
        }
    }
}
void AssemblyMateEditor::pick(const document::AssemblyReference& ref)
{
    const int option = optionIndex(ref);
    if (option < 0 || m_active < 0 || m_active >= int(m_selections.size()))
        return;
    const bool initializing = m_initializing;
    m_initializing = true;
    auto& selection = m_selections[std::size_t(m_active)];
    int count = 0;
    if (auto* box = qobject_cast<QComboBox*>(selection.widget))
    {
        box->setCurrentIndex(option + 1);
        count = 1;
    }
    else
    {
        auto* list = static_cast<QListWidget*>(selection.widget);
        for (int i = 0; i < list->count(); ++i)
            count += list->item(i)->isSelected();
        auto* item = list->item(option);
        if (item->isSelected())
        {
            item->setSelected(false);
            --count;
        }
        else if (count < selection.role.maximum)
        {
            item->setSelected(true);
            ++count;
        }
        if (!initializing && selection.role.key == "cam" && count == 1 && item->isSelected() &&
            ref.geometry.source == modeling::ReferenceSource::BodyFace)
        {
            std::vector<MateGeometry> faces;
            std::vector<int> options;
            std::size_t seed = 0;
            for (int i = 0; i < int(m_options.size()); ++i)
                if (m_options[std::size_t(i)].reference.componentId == ref.componentId &&
                    m_options[std::size_t(i)].reference.geometry.source ==
                        modeling::ReferenceSource::BodyFace)
                {
                    if (i == option)
                        seed = faces.size();
                    faces.push_back(m_options[std::size_t(i)].mateGeometry);
                    options.push_back(i);
                }
            try
            {
                const auto chain = camFaceChain(faces, seed);
                for (auto i : chain)
                    list->item(options[i])->setSelected(true);
                count = int(chain.size());
                if (m_active + 1 < int(m_selections.size()))
                    activate(m_active + 1);
            }
            catch (const std::exception&)
            { /* Keep the user's explicit face selection for repair. */
            }
        }
    }
    if (count >= selection.role.maximum && m_active + 1 < int(m_selections.size()))
        activate(m_active + 1);
    const auto selected = references();
    if (selected.size() == 2)
    {
        const auto a = m_source.mateGeometry(selected[0]), b = m_source.mateGeometry(selected[1]);
        const auto transform = [&](const auto& ref)
        {
            return ref.componentId.empty() ? gp_Trsf()
                                           : m_source.component(ref.componentId).placement;
        };
        const QSignalBlocker block(m_flip);
        m_flip->setChecked(a.frame.Direction()
                               .Transformed(transform(selected[0]))
                               .Dot(b.frame.Direction().Transformed(transform(selected[1]))) < 0);
    }
    if (MateKind(m_kind->currentData().toInt()) == MateKind::Cam)
    {
        try
        {
            std::vector<MateGeometry> chain;
            std::optional<document::AssemblyReference> carrier, follower;
            for (const auto& group : groups())
                for (const auto& reference : group.references)
                    if (group.role == "cam")
                    {
                        chain.push_back(m_source.mateGeometry(reference));
                        carrier = reference;
                    }
                    else if (group.role == "follower")
                        follower = reference;
            if (carrier && follower)
            {
                gp_Dir normal;
                (void)camPath(chain, normal);
                if (!carrier->componentId.empty())
                    normal.Transform(m_source.component(carrier->componentId).placement);
                auto direction = m_source.mateGeometry(*follower).frame.Direction();
                if (!follower->componentId.empty())
                    direction.Transform(m_source.component(follower->componentId).placement);
                const QSignalBlocker block(m_flip);
                m_flip->setChecked(normal.Dot(direction) < 0);
            }
        }
        catch (const std::exception&)
        { /* The preview reports incomplete or invalid chains. */
        }
    }
    m_initializing = initializing;
    refreshPreview();
}
void AssemblyMateEditor::preselect(const std::vector<document::AssemblyReference>& selected)
{
    if (selected.empty())
        return;
    m_initializing = true;
    if (selected.size() > 2)
    {
        const QSignalBlocker block(m_multiple);
        m_multiple->setChecked(true);
    }
    if (selected.size() == 2)
    {
        const auto a = m_source.mateGeometry(selected[0]), b = m_source.mateGeometry(selected[1]);
        MateKind wanted = compatibleMate(MateKind::Coincident, a, b)   ? MateKind::Coincident
                          : compatibleMate(MateKind::Concentric, a, b) ? MateKind::Concentric
                                                                       : MateKind::Distance;
        if ((a.kind == MateGeometryKind::Cylinder || a.kind == MateGeometryKind::Line) &&
            (b.kind == MateGeometryKind::Cylinder || b.kind == MateGeometryKind::Line))
            wanted = MateKind::Concentric;
        m_family->setCurrentIndex(int(mateFamily(wanted)));
        changeFamily();
        m_kind->setCurrentIndex(m_kind->findData(int(wanted)));
        m_flip->setChecked(
            a.frame.Direction()
                .Transformed(selected[0].componentId.empty()
                                 ? gp_Trsf()
                                 : m_source.component(selected[0].componentId).placement)
                .Dot(b.frame.Direction().Transformed(
                    selected[1].componentId.empty()
                        ? gp_Trsf()
                        : m_source.component(selected[1].componentId).placement)) < 0);
    }
    buildSelections();
    for (const auto& ref : selected)
        pick(ref);
    m_initializing = false;
    refreshPreview();
}
bool AssemblyMateEditor::keepOpen() const
{
    return m_keepOpen->isChecked();
}
void AssemblyMateEditor::refreshPreview()
{
    if (m_initializing || m_refreshing)
        return;
    m_refreshing = true;
    auto previous = std::move(m_candidate);
    m_candidate.reset();
    m_conflicts->hide();
    m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    try
    {
        const auto selected = references();
        if (m_family->currentIndex() == 0)
        {
            auto* model = qobject_cast<QStandardItemModel*>(m_kind->model());
            int fallback = -1;
            for (int i = 0; i < m_kind->count(); ++i)
            {
                const bool valid =
                    selected.size() != 2 || compatibleMate(MateKind(m_kind->itemData(i).toInt()),
                                                           m_source.mateGeometry(selected[0]),
                                                           m_source.mateGeometry(selected[1]));
                model->item(i)->setEnabled(valid);
                if (valid && fallback < 0 &&
                    MateKind(m_kind->itemData(i).toInt()) != MateKind::Lock)
                    fallback = i;
            }
            if (m_kind->currentIndex() >= 0 && !model->item(m_kind->currentIndex())->isEnabled() &&
                fallback >= 0)
            {
                const QSignalBlocker block(m_kind);
                m_kind->setCurrentIndex(fallback);
            }
        }
        if (MateKind(m_kind->currentData().toInt()) == MateKind::Gear && selected.size() == 2)
            m_parameters->geometryRatio(m_source.mateGeometry(selected[0]).radius,
                                        m_source.mateGeometry(selected[1]).radius);
        if (selected.size() == 2)
            m_parameters->coordinateFrames(
                m_source.mateGeometry(selected[0]).kind == MateGeometryKind::Frame &&
                m_source.mateGeometry(selected[1]).kind == MateGeometryKind::Frame);
        auto mate = m_mate;
        mate.name = m_name->text().trimmed().toStdString();
        mate.kind = MateKind(m_kind->currentData().toInt());
        mate.alignment = m_flip->isChecked() ? -1 : 1;
        mate.status = MateStatus::Active;
        mate.diagnostic.clear();
        const bool angular = mate.kind == MateKind::Angle,
                   numeric = angular || mate.kind == MateKind::Distance;
        {
            const QSignalBlocker block(m_value);
            m_value->setRange(angular ? 0 : -1e9, angular ? 180 : 1e9);
            m_value->setSuffix(angular ? "°" : " mm");
        }
        m_form->setRowVisible(m_value, numeric);
        mate.value = numeric ? m_value->value() : 0;
        {
            const QSignalBlocker block(m_parameters);
            m_parameters->configure(mate.kind);
        }
        mate.definition = document::AssemblyMate::Definition{};
        mate.definition->groups = groups();
        std::vector<std::pair<std::string, MateGeometry>> selectedGeometry;
        for (const auto& group : mate.definition->groups)
            for (const auto& ref : group.references)
                selectedGeometry.emplace_back(group.role, m_source.mateGeometry(ref));
        m_parameters->selectionGeometry(mate.kind, selectedGeometry);
        mate.definition->parameters = m_parameters->parameters();
        if (m_mate.definition && m_mate.kind == mate.kind &&
            QJsonDocument(document::writeMateDefinition(*m_mate.definition)).object()["groups"] ==
                QJsonDocument(document::writeMateDefinition(*mate.definition)).object()["groups"])
            mate.definition->motion = m_mate.definition->motion;
        const auto refs = references();
        if (refs.size() >= 2)
        {
            mate.first = refs[0];
            mate.second = refs[1];
        }
        m_proposed = mate;
        const auto expected = mateRoles(mate.kind, mate.definition->parameters.hinge.limits);
        for (const auto& role : expected)
        {
            const auto found =
                std::find_if(mate.definition->groups.begin(), mate.definition->groups.end(),
                             [&](const auto& g) { return g.role == role.key; });
            const int count =
                found == mate.definition->groups.end() ? 0 : int(found->references.size());
            if (count < role.minimum ||
                (count > role.maximum &&
                 !(m_multiple->isChecked() && m_family->currentIndex() == 0 &&
                   role.key == "second" && count <= 64)))
                throw std::runtime_error(
                    role.label + ": select " + std::to_string(role.minimum) +
                    (role.maximum == role.minimum ? "" : " to " + std::to_string(role.maximum)) +
                    " reference(s).");
        }
        const auto key =
            QJsonDocument(document::writeAssemblyMate(mate)).toJson(QJsonDocument::Compact);
        const bool reused = previous && m_previewKey == key;
        if (reused)
            m_candidate = std::move(previous);
        else
        {
            auto candidate = m_source;
            if (m_multiple->isChecked() && m_family->currentIndex() == 0)
            {
                const auto targets = mate.definition->groups.at(1).references;
                for (std::size_t i = 0; i < targets.size(); ++i)
                {
                    auto member = mate;
                    member.id += "-" + std::to_string(i);
                    member.name.clear();
                    member.second = targets[i];
                    member.definition->groups[1].references = {targets[i]};
                    member.definition->motion = {};
                    candidate.setMate(member);
                }
            }
            else
                candidate.setMate(mate);
            m_candidate = std::move(candidate);
            m_previewKey = key;
        }
        m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        m_page->setStatus(QString("Preview ready · %1 assembly degree(s) of freedom remain")
                              .arg(m_candidate->solution().degreesOfFreedom));
        if (reused)
        {
            m_refreshing = false;
            return;
        }
    }
    catch (const Standard_Failure& error)
    {
        m_page->setStatus(QString::fromUtf8(error.GetMessageString()), true);
    }
    catch (const std::exception& error)
    {
        m_page->setStatus(QString::fromUtf8(error.what()), true);
        const auto message = QString::fromUtf8(error.what());
        m_conflicts->setVisible(!m_multiple->isChecked() &&
                                message.startsWith("Conflicting or unsolved mates:"));
    }
    m_refreshing = false;
    emit previewChanged();
}
void AssemblyMateEditor::keepUnsolved()
{
    try
    {
        auto candidate = m_source;
        candidate.keepUnsolvedMate(m_proposed);
        m_candidate = std::move(candidate);
        m_page->setStatus(
            "This mate will be retained as unsolved. Existing mates and positions are preserved.",
            true);
        m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        m_conflicts->hide();
        emit previewChanged();
    }
    catch (const std::exception& e)
    {
        m_page->setStatus(e.what(), true);
    }
}
void AssemblyMateEditor::force()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Leave selected mates unsolved");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(
        new QLabel("Only the checked mates will be marked unsolved. No mates will be deleted."));
    auto* list = new QListWidget;
    layout->addWidget(list);
    for (const auto& mate : m_source.mates())
        if (mate.id != m_proposed.id && mate.status == MateStatus::Active)
        {
            auto* item = new QListWidgetItem(QString::fromStdString(mate.name), list);
            item->setData(Qt::UserRole, QString::fromStdString(mate.id));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::vector<std::string> ids;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            ids.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
    try
    {
        auto candidate = m_source;
        candidate.forceMate(m_proposed, ids);
        m_candidate = std::move(candidate);
        m_page->setStatus("Preview ready. Checked mates will be retained as unsolved.", true);
        m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        m_conflicts->hide();
        emit previewChanged();
    }
    catch (const std::exception& e)
    {
        m_page->setStatus(e.what(), true);
    }
}
} // namespace thecad::ui
