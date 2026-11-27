#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include "modeling/SketchContourEdits.h"
#include "modeling/SketchOffset.h"
#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace thecad::ui
{
namespace
{
using Modifier = modeling::SketchModifier;

class ExternalReferenceCommandSession final : public application::CommandSession
{
public:
    using Update = std::function<application::CommandResult(const application::CommandRequest&)>;
    using Finish = std::function<application::CommandResult()>;
    ExternalReferenceCommandSession(Update update, Finish confirm, Finish cancel)
        : m_update(std::move(update)), m_confirm(std::move(confirm)), m_cancel(std::move(cancel)) {}
    application::CommandResult update(const application::CommandRequest& request,
                                      const application::CommandContext&) override
    { return m_update(request); }
    application::CommandResult confirm(const application::CommandContext&) override
    { return m_confirm(); }
    application::CommandResult cancel() override
    { return m_cancel(); }
private:
    Update m_update;
    Finish m_confirm, m_cancel;
};

QString modifierTitle(Modifier kind)
{
    switch (kind)
    {
    case Modifier::Convert: return "Convert Entities";
    case Modifier::Mirror: return "Mirror Entities";
    case Modifier::LinearPattern: return "Linear Sketch Pattern";
    case Modifier::CircularPattern: return "Circular Sketch Pattern";
    case Modifier::Trim: return "Trim Entities";
    case Modifier::Extend: return "Extend Entities";
    case Modifier::Split: return "Split Entities";
    case Modifier::Offset: return "Offset Entities";
    case Modifier::ExternalReference: return "External References";
    }
    return {};
}
}

void MainWindow::bindExternalReferenceCommand()
{
    m_commands.bindInteractive("sketch.external_reference",
        [this](const application::CommandContext& context) {
            return hasEditInPlaceContext() && !m_inPlaceComponentId.empty() &&
                   bool(m_externalReferenceOptions) && !context.activeSketchId.empty() &&
                   m_mode == WorkspaceMode::EditingSketch && !featureDraftActive()
                ? application::CommandAvailability{true, {}, {}}
                : application::CommandAvailability{false,
                    "External references require an active Edit In Place sketch",
                    {"Edit In Place", "active sketch"}};
        },
        [this](const application::CommandRequest&, const application::CommandContext&) {
            startSketchModifier(Modifier::ExternalReference);
            if (!m_sketchModifier || m_sketchModifier->kind != Modifier::ExternalReference)
                return std::unique_ptr<application::CommandSession>{};
            return std::unique_ptr<application::CommandSession>(new ExternalReferenceCommandSession(
                [this](const auto& request) { return updateExternalReferenceCommand(request); },
                [this] { return confirmExternalReferenceCommand(); },
                [this] { return cancelExternalReferenceCommand(); }));
        });
    m_commands.bindImmediate("sketch.break_external_reference",
        [this](const application::CommandContext& context) {
            if (context.activeSketchId.empty() || !m_session.editingSketch())
                return application::CommandAvailability{false, "An active sketch is required", {"sketch"}};
            return application::CommandAvailability{true, {}, {}};
        },
        [this](const application::CommandRequest& request, const application::CommandContext&) {
            const auto sketchIt = request.parameters.find("sketch");
            const auto entityIt = request.parameters.find("entity");
            auto candidate = *m_session.editingSketch();
            const auto sketchId = std::get<std::string>(sketchIt->second);
            const auto entityId = std::get<std::string>(entityIt->second);
            if (sketchId != m_session.editingSketchId() ||
                !modeling::breakExternalSketchProjection(candidate, entityId))
                return application::CommandResult{application::CommandState::Rejected,
                    "The external sketch reference is no longer available"};
            try {
                acceptConstrainedSketch(candidate);
                m_viewport->setSketch(*m_session.editingSketch());
                setWindowModified(true);
                updateEntityList();
                updateActions();
                return application::CommandResult{application::CommandState::Completed,
                    "Reference link broken; copied geometry was retained", {}, {entityId}, true};
            } catch (const std::exception& error) {
                return application::CommandResult{application::CommandState::Failed, error.what()};
            }
        });
}

void MainWindow::startSketchModifier(Modifier kind)
{
    if (m_sketchModifier && m_sketchModifier->kind == kind)
    {
        finishSketchModifier(false);
        return;
    }
    if (!m_session.editingSketch() || featureDraftActive() || m_constraints ||
        (m_smartDimension && m_smartDimension->editor))
        return;
    if (kind == Modifier::ExternalReference &&
        (!hasEditInPlaceContext() || !m_externalReferenceOptions || m_inPlaceComponentId.empty()))
        return;
    closeSmartDimension();
    hideRelationsContext();
    m_viewport->selectSketchTool();
    m_sketchModifier = std::make_unique<SketchModifierDraft>();
    auto& draft = *m_sketchModifier;
    draft.kind = kind;
    draft.seeds = m_viewport->selectedSketchEntities();
    const bool convert = kind == Modifier::Convert;
    const bool lineEdit = modeling::isSketchLineEdit(kind);
    const bool externalReference = kind == Modifier::ExternalReference;
    if (lineEdit)
        m_viewport->clearSelection();
    draft.page = new PropertyManagerPage(modifierTitle(kind),
        externalReference ? "Select visible reference geometry from another component. Click adds it; Ctrl-click removes it. Choose whether the reference updates associatively or remains a fixed copy."
                : convert ? "Pick a planar face or edges in the viewport. Ctrl-click adds to the selection."
                : lineEdit ? "Hover over a straight line to preview. Each click applies one edit. Esc exits."
                : kind == Modifier::Mirror ? "First pick the line to mirror about, then pick entities to mirror. Click an entity again to remove it."
                : kind == Modifier::Offset ? "Select a circle or connected line chain. Set a distance and confirm the preview."
                : "Select seed geometry in the viewport. Ctrl-click adds or removes a seed.",
        m_managerStack);
    draft.page->setObjectName("sketchModifierPanel");
    draft.page->buttons->setObjectName("sketchModifierButtons");
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    draft.page->settings->addLayout(form);
    draft.selection = new QLabel;
    draft.selection->setWordWrap(true);
    form->addRow(draft.selection);
    const auto check = [&](const QString& label, const char* name) {
        auto* box = new QCheckBox(label);
        box->setObjectName(name);
        form->addRow(box);
        connect(box, &QCheckBox::toggled, this, [this] { refreshSketchModifier(); });
        return box;
    };
    const auto number = [&](const QString& label, const char* name, double minimum,
                            double maximum, double initial, const QString& suffix) {
        auto* input = new QDoubleSpinBox;
        input->setObjectName(name);
        input->setDecimals(6);
        input->setRange(minimum, maximum);
        input->setSuffix(suffix);
        input->setValue(initial);
        form->addRow(label, input);
        connect(input, &QDoubleSpinBox::valueChanged, this, [this] { refreshSketchModifier(); });
        return input;
    };
    if (lineEdit)
    {
        draft.page->buttons->button(QDialogButtonBox::Ok)->setText("Done");
        draft.page->buttons->button(QDialogButtonBox::Cancel)->hide();
        draft.page->setStatus("Hover over a line to preview the edit.");
    }
    else if (externalReference)
    {
        draft.externalOptions = m_externalReferenceOptions(m_session.editingSketchId(), m_session.document());
        draft.externalMode = new QComboBox;
        draft.externalMode->setObjectName("externalReferenceMode");
        draft.externalMode->addItem("Associative — update when the source changes", true);
        draft.externalMode->addItem("Copy — keep this geometry unchanged", false);
        form->addRow("Reference mode", draft.externalMode);
        auto* instruction = new QLabel("Hover reference geometry in the assembly context, then click to select. Ctrl-click toggles a reference.");
        instruction->setWordWrap(true);
        form->addRow(instruction);
        draft.externalReferences = new QListWidget;
        draft.externalReferences->setObjectName("externalReferenceSources");
        draft.externalReferences->setSelectionMode(QAbstractItemView::MultiSelection);
        draft.externalReferences->setMaximumHeight(190);
        for (const auto& option : draft.externalOptions)
        {
            auto* item = new QListWidgetItem(QString::fromStdString(option.label), draft.externalReferences);
            item->setData(Qt::UserRole, QString::fromStdString(option.id));
            item->setToolTip(QString::fromStdString(option.id));
        }
        form->addRow("Available source geometry", draft.externalReferences);
        auto* dependent = new QLabel(QString("Dependent sketch: %1")
            .arg(QString::fromStdString(m_session.editingSketchId())));
        dependent->setObjectName("externalReferenceDependentSketch");
        form->addRow(dependent);
        draft.existingExternalReferences = new QListWidget;
        draft.existingExternalReferences->setObjectName("existingExternalReferences");
        draft.existingExternalReferences->setMaximumHeight(120);
        const auto diagnostics = m_externalReferenceDiagnostics
            ? m_externalReferenceDiagnostics() : std::vector<document::ExternalReferenceDiagnostic>{};
        for (const auto& link : m_session.editingSketch()->externalReferences)
        {
            const auto option = std::find_if(draft.externalOptions.begin(), draft.externalOptions.end(),
                [&](const auto& item) {
                    const auto& ref = item.reference;
                    return ref.componentId == link.componentId && ref.sourceKind == link.sourceKind &&
                           ref.sourceFeatureId == link.sourceFeatureId && ref.sourceEntityId == link.sourceEntityId &&
                           ref.role == link.role;
                });
            QString label = option == draft.externalOptions.end()
                ? QString("Missing source · %1 · %2/%3").arg(QString::fromStdString(link.componentId),
                    QString::fromStdString(link.sourceFeatureId), QString::fromStdString(link.sourceEntityId))
                : QString::fromStdString(option->label);
            label += link.associative ? " · Associative" : " · Copy";
            const auto diagnostic = std::find_if(diagnostics.begin(), diagnostics.end(), [&](const auto& item) {
                return item.dependentComponentId == m_inPlaceComponentId &&
                       item.dependentFeatureId == m_session.editingSketchId() &&
                       item.projectedEntityId == link.entityId;
            });
            if (diagnostic != diagnostics.end())
            {
                const auto state = diagnostic->state == document::ExternalReferenceState::Valid ? "Valid" :
                    diagnostic->state == document::ExternalReferenceState::OutOfDate ? "Out of date" :
                    diagnostic->state == document::ExternalReferenceState::MissingSource ? "Missing source" :
                    diagnostic->state == document::ExternalReferenceState::AmbiguousReference ? "Ambiguous" :
                    diagnostic->state == document::ExternalReferenceState::FailedRegeneration ? "Regeneration failed" :
                    "Invalid";
                label += QString(" · %1").arg(state);
            }
            auto* item = new QListWidgetItem(label, draft.existingExternalReferences);
            item->setData(Qt::UserRole, QString::fromStdString(link.entityId));
            item->setToolTip(label);
        }
        form->addRow("References in this sketch", draft.existingExternalReferences);
        draft.breakExternalReference = new QPushButton("Break selected link (keep geometry)");
        draft.breakExternalReference->setObjectName("breakExternalSketchReference");
        draft.breakExternalReference->setEnabled(draft.existingExternalReferences->count() > 0);
        form->addRow(draft.breakExternalReference);
        connect(draft.externalReferences, &QListWidget::itemSelectionChanged, this,
                &MainWindow::updateExternalReferenceSession);
        connect(draft.externalMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &MainWindow::updateExternalReferenceSession);
        connect(draft.existingExternalReferences, &QListWidget::itemSelectionChanged, this, [this] {
            if (m_sketchModifier && m_sketchModifier->breakExternalReference)
                m_sketchModifier->breakExternalReference->setEnabled(
                    !m_sketchModifier->existingExternalReferences->selectedItems().empty());
        });
        connect(draft.breakExternalReference, &QPushButton::clicked, this,
                &MainWindow::breakSelectedExternalReference);
    }
    else if (convert)
    {
        draft.innerLoops = check("Include inner loops", "convertInnerLoops");
        draft.construction = check("For construction", "convertConstruction");
    }
    else if (kind == Modifier::Offset)
    {
        draft.spacing = number("Offset distance", "sketchOffsetDistance", 0.000002, 1e9, 5, " mm");
        draft.reverse = check("Reverse", "sketchOffsetReverse");
        draft.bothSides = check("Both sides", "sketchOffsetBothSides");
        draft.selectChain = check("Select chain", "sketchOffsetSelectChain");
        draft.construction = check("For construction", "sketchOffsetConstruction");
        const QSignalBlocker block(draft.selectChain);
        draft.selectChain->setChecked(true);
    }
    else if (kind == Modifier::Mirror)
    {
        draft.pickingAxis = true;
        draft.axis = new PropertyReference;
        draft.axis->setObjectName("mirrorSketchAxis");
        draft.axis->addItem("Select a sketch line");
        const auto& entities = m_session.editingSketch()->entities;
        for (std::size_t i = 0; i < entities.size(); ++i)
            if (entities[i].kind == modeling::EntityKind::Line)
                draft.axis->addItem(QString("Line %1%2").arg(i + 1).arg(
                    entities[i].construction ? " (construction)" : ""),
                    QString::fromStdString(entities[i].id));
        form->addRow("Mirror about", draft.axis);
        draft.axis->pick = [this] {
            m_sketchModifier->pickingAxis = true;
            m_viewport->setFocus();
            refreshSketchModifier();
        };
        draft.mirrorEntities = new PropertySelectionList;
        draft.mirrorEntities->setObjectName("mirrorSketchEntities");
        draft.mirrorEntities->placeholder = "Select entities in the sketch";
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            auto* member = new QListWidgetItem(QString("Entity %1").arg(i + 1), draft.mirrorEntities);
            member->setData(Qt::UserRole, QString::fromStdString(entities[i].id));
            member->setSelected(std::find(draft.seeds.begin(), draft.seeds.end(), entities[i].id) != draft.seeds.end());
        }
        draft.mirrorEntities->refreshMembers();
        form->addRow("Entities to mirror", draft.mirrorEntities);
        draft.mirrorEntities->pick = [this] {
            m_sketchModifier->pickingAxis = m_sketchModifier->axis->currentIndex() == 0;
            m_viewport->setFocus();
            refreshSketchModifier();
        };
        connect(draft.mirrorEntities, &QListWidget::itemSelectionChanged, this, [this] {
            if (!m_sketchModifier) return;
            auto& current = *m_sketchModifier;
            current.seeds.clear();
            for (const auto* item : current.mirrorEntities->selectedItems())
                current.seeds.push_back(item->data(Qt::UserRole).toString().toStdString());
            refreshSketchModifier();
        });
        connect(draft.axis, &QComboBox::currentIndexChanged, this, [this] {
            if (!m_sketchModifier) return;
            m_sketchModifier->pickingAxis = m_sketchModifier->axis->currentIndex() == 0;
            refreshSketchModifier();
        });
    }
    else
    {
        const bool linear = kind == Modifier::LinearPattern;
        if (linear)
            draft.spacing = number("Spacing", "sketchPatternSpacing", 0.000002, 1e9, 10, " mm");
        else
        {
            draft.centerX = number("Center X", "sketchPatternCenterX", -1e9, 1e9, 0, " mm");
            draft.centerY = number("Center Y", "sketchPatternCenterY", -1e9, 1e9, 0, " mm");
        }
        draft.angle = number(linear ? "Direction angle" : "Total angle", "sketchPatternAngle",
                             linear ? -360 : 0.000002, 360, linear ? 0 : 360, " deg");
        draft.count = new QSpinBox;
        draft.count->setObjectName("sketchPatternCount");
        draft.count->setRange(2, 100);
        draft.count->setValue(linear ? 2 : 4);
        form->addRow("Total instances", draft.count);
        connect(draft.count, &QSpinBox::valueChanged, this, [this] { refreshSketchModifier(); });
        draft.reverse = check("Reverse direction", "sketchPatternReverse");
    }
    draft.page->settings->addStretch();
    connect(draft.page->buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_sketchModifier && m_sketchModifier->kind == Modifier::ExternalReference &&
            !m_sketchModifier->commandSessionId.empty())
            (void)m_commands.confirm(m_sketchModifier->commandSessionId);
        else finishSketchModifier(true);
    });
    connect(draft.page->buttons, &QDialogButtonBox::rejected, this, [this] {
        if (m_sketchModifier && m_sketchModifier->kind == Modifier::ExternalReference &&
            !m_sketchModifier->commandSessionId.empty())
            (void)m_commands.cancel(m_sketchModifier->commandSessionId);
        else finishSketchModifier(false);
    });
    m_viewport->setSketchModifierPicking(true, convert, lineEdit);
    if (externalReference)
        syncExternalReferencePicker();
    m_managerStack->addWidget(draft.page);
    m_managerStack->setCurrentWidget(draft.page);
    m_historyDock->setWindowTitle("PropertyManager");
    refreshSketchModifier();
    updateActions();
    if (kind == Modifier::Offset && !draft.seeds.empty())
    {
        draft.spacing->setFocus();
        draft.spacing->selectAll();
    }
    else
        m_viewport->setFocus();
}

void MainWindow::refreshSketchModifier(bool viewportSelection)
{
    if (!m_sketchModifier || !m_session.editingSketch())
        return;
    auto& draft = *m_sketchModifier;
    if (viewportSelection && draft.kind == Modifier::Mirror)
        return; // Mirror consumes the explicit pick, including Ctrl-removal.
    if (modeling::isSketchLineEdit(draft.kind))
        return;
    const auto& sketch = *m_session.editingSketch();
    draft.candidate.reset();
    try
    {
        if (draft.kind == Modifier::ExternalReference)
        {
            std::vector<std::string> selected;
            for (const auto* item : draft.externalReferences->selectedItems())
                selected.push_back(item->data(Qt::UserRole).toString().toStdString());
            const bool associative = draft.externalMode->currentData().toBool();
            draft.selection->setText(QString("%1 source reference(s) · %2")
                .arg(selected.size()).arg(associative ? "Associative" : "Copy"));
            if (selected.empty())
                throw std::runtime_error("Select one or more references from surrounding components.");
            draft.candidate = sketch;
            for (const auto& id : selected)
            {
                const auto option = std::find_if(draft.externalOptions.begin(), draft.externalOptions.end(),
                    [&](const auto& item) { return item.id == id; });
                if (option == draft.externalOptions.end())
                    throw std::runtime_error("A selected source reference is no longer available. Select it again.");
                auto link = option->reference;
                link.associative = associative;
                modeling::useExternalSketchProjection(*draft.candidate, option->projectedGeometry, std::move(link));
            }
            auto candidatePart = m_session.document();
            auto feature = std::find_if(candidatePart.features().begin(), candidatePart.features().end(),
                [&](const auto& item) { return item.id == m_session.editingSketchId() &&
                                               item.kind == document::FeatureKind::Sketch; });
            if (feature == candidatePart.features().end())
                throw std::runtime_error("The active sketch is no longer available.");
            auto replacement = *feature;
            replacement.sketch = *draft.candidate;
            candidatePart.replaceFeature(replacement);
            const auto regenerated = std::find_if(candidatePart.features().begin(), candidatePart.features().end(),
                [&](const auto& item) { return item.id == m_session.editingSketchId(); });
            if (regenerated == candidatePart.features().end())
                throw std::runtime_error("The referenced sketch could not be regenerated.");
            draft.candidate = regenerated->sketch;
            if (associative && m_externalReferenceValidator)
            {
                std::string diagnostic;
                if (!m_externalReferenceValidator(candidatePart, &diagnostic))
                    throw std::runtime_error(diagnostic.empty()
                        ? "The associative reference is invalid or circular." : diagnostic);
            }
            syncExternalReferencePicker();
        }
        else if (draft.kind == Modifier::Convert)
        {
            auto selection = m_viewport->selection();
            selection.erase(std::remove_if(selection.begin(), selection.end(), [](const auto& item) {
                return item.kind != modeling::SelectionKind::Face && item.kind != modeling::SelectionKind::Edge;
            }), selection.end());
            draft.selection->setText(selection.empty() ? (sketch.attachment ? "Using sketch support face" : "No model geometry selected")
                                                       : QString("%1 model references selected").arg(selection.size()));
            draft.candidate = m_session.document().convertSketchGeometry(m_session.editingSketchId(),
                sketch, selection, draft.innerLoops->isChecked(), draft.construction->isChecked());
        }
        else if (draft.kind == Modifier::Offset)
        {
            if (viewportSelection)
                draft.seeds = m_viewport->selectedSketchEntities();
            draft.selection->setText(QString("%1 source entities selected").arg(draft.seeds.size()));
            draft.reverse->setEnabled(!draft.bothSides->isChecked());
            if (!draft.spacing->hasAcceptableInput())
                throw std::runtime_error("Enter a valid offset distance.");
            draft.candidate = modeling::offsetSketchEntities(sketch, draft.seeds,
                {draft.spacing->value(), draft.reverse->isChecked(), draft.bothSides->isChecked(),
                 draft.selectChain->isChecked(), draft.construction->isChecked()});
        }
        else
        {
            if (viewportSelection && !draft.axis)
                draft.seeds = m_viewport->selectedSketchEntities();
            draft.selection->setText(QString("%1 seed entities selected").arg(draft.seeds.size()));
            modeling::SketchPattern parameters;
            parameters.kind = draft.kind;
            if (draft.axis)
            {
                draft.axis->setPicking(draft.pickingAxis);
                auto highlighted = draft.seeds;
                if (draft.axis->currentIndex() > 0)
                    highlighted.push_back(draft.axis->currentData().toString().toStdString());
                m_viewport->setDimensionSelection(highlighted, {});
                if (draft.pickingAxis)
                    throw std::runtime_error("Click a sketch line to set the mirror axis.");
                if (draft.axis->currentIndex() <= 0)
                    throw std::runtime_error("Select a sketch line to mirror about.");
                parameters.mirrorLine = draft.axis->currentData().toString().toStdString();
            }
            else
            {
                parameters.count = draft.count->value();
                parameters.angle = draft.angle->value();
                parameters.reverse = draft.reverse->isChecked();
                if (draft.spacing)
                    parameters.spacing = draft.spacing->value();
                else
                    parameters.center = {draft.centerX->value(), draft.centerY->value()};
            }
            draft.candidate = modeling::replicateSketchEntities(sketch, draft.seeds, parameters);
        }
        draft.page->setStatus("Preview ready. Enter confirms and Esc cancels.");
        m_viewport->setSketchModifierPreview(&*draft.candidate);
    }
    catch (const std::exception& error)
    {
        draft.candidate.reset();
        draft.page->setStatus(QString::fromUtf8(error.what()), true);
        m_viewport->setSketchModifierPreview(nullptr);
    }
    draft.page->buttons->button(QDialogButtonBox::Ok)->setEnabled(draft.candidate.has_value());
}

void MainWindow::pickSketchMirrorEntity(const QString& id)
{
    if (!m_sketchModifier || m_sketchModifier->kind != Modifier::Mirror)
        return;
    auto& draft = *m_sketchModifier;
    if (draft.pickingAxis)
    {
        const int index = draft.axis->findData(id);
        if (index <= 0)
        {
            draft.page->setStatus("The mirror reference must be a straight sketch line.", true);
            return;
        }
        const QSignalBlocker block(draft.axis);
        draft.axis->setCurrentIndex(index);
        draft.pickingAxis = false;
        draft.seeds.erase(std::remove(draft.seeds.begin(), draft.seeds.end(), id.toStdString()), draft.seeds.end());
    }
    else if (id != draft.axis->currentData().toString())
    {
        const auto found = std::find(draft.seeds.begin(), draft.seeds.end(), id.toStdString());
        if (found == draft.seeds.end()) draft.seeds.push_back(id.toStdString());
        else draft.seeds.erase(found);
    }
    {
        const QSignalBlocker block(draft.mirrorEntities);
        for (int i = 0; i < draft.mirrorEntities->count(); ++i)
        {
            auto* member = draft.mirrorEntities->item(i);
            member->setSelected(std::find(draft.seeds.begin(), draft.seeds.end(),
                member->data(Qt::UserRole).toString().toStdString()) != draft.seeds.end());
        }
        draft.mirrorEntities->refreshMembers();
    }
    refreshSketchModifier();
}

void MainWindow::updateSketchLineEdit(const QString& id, modeling::Point2 point, bool apply)
{
    if (!m_sketchModifier || !m_session.editingSketch() ||
        !modeling::isSketchLineEdit(m_sketchModifier->kind))
        return;
    auto& draft = *m_sketchModifier;
    const auto& sketch = *m_session.editingSketch();
    if (id.isEmpty())
    {
        m_viewport->setSketchModifierPreview(nullptr);
        draft.page->setStatus("Hover over a line to preview the edit.");
        return;
    }
    try
    {
        const auto entityId = id.toStdString();
        auto candidate = modeling::editSketchLine(sketch, draft.kind, entityId, point);
        if (apply)
        {
            acceptConstrainedSketch(candidate);
            m_viewport->setSketch(*m_session.editingSketch());
            m_viewport->setSketchModifierPreview(nullptr);
            setWindowModified(true);
            updateEntityList();
            updateActions();
            draft.page->setStatus("Edit applied. Pick another line or press Esc.");
            return;
        }
        const auto source = std::find_if(sketch.entities.begin(), sketch.entities.end(),
                                        [&](const auto& item) { return item.id == entityId; });
        const auto& changed = candidate.entities[std::size_t(source - sketch.entities.begin())];
        std::optional<modeling::SketchEntity> removed;
        std::optional<modeling::Point2> split;
        if (draft.kind == Modifier::Trim)
        {
            removed = *source;
            if (candidate.entities.size() > sketch.entities.size())
            {
                removed->first = changed.second;
                removed->second = candidate.entities.back().first;
            }
            else if (changed.first.x != source->first.x || changed.first.y != source->first.y)
                removed->second = changed.first;
            else
                removed->first = changed.second;
        }
        else if (draft.kind == Modifier::Split)
            split = changed.second;
        m_viewport->setSketchModifierPreview(&candidate, removed, split);
        draft.page->setStatus(removed ? "Red shows the portion to remove. Click to trim."
                                      : "Click to apply the preview.");
    }
    catch (const std::exception& error)
    {
        m_viewport->setSketchModifierPreview(nullptr);
        draft.page->setStatus(QString::fromUtf8(error.what()), true);
    }
}

void MainWindow::finishSketchModifier(bool accepted)
{
    if (m_sketchModifier && m_sketchModifier->kind == Modifier::ExternalReference &&
        !m_sketchModifier->commandSessionId.empty())
    {
        if (accepted) (void)m_commands.confirm(m_sketchModifier->commandSessionId);
        else (void)m_commands.cancel(m_sketchModifier->commandSessionId);
        return;
    }
    finishSketchModifierLocally(accepted);
}

void MainWindow::finishSketchModifierLocally(bool accepted)
{
    if (!m_sketchModifier)
        return;
    const bool lineEdit = modeling::isSketchLineEdit(m_sketchModifier->kind);
    if (accepted && m_sketchModifier->kind == Modifier::Offset)
        refreshSketchModifier();
    if (accepted && !lineEdit && !m_sketchModifier->candidate)
        return;
    if (accepted && !lineEdit)
    {
        try
        {
            acceptConstrainedSketch(*m_sketchModifier->candidate);
        }
        catch (const std::exception& error)
        {
            m_sketchModifier->page->setStatus(QString::fromUtf8(error.what()), true);
            return;
        }
    }
    auto* page = m_sketchModifier->page;
    const auto title = modifierTitle(m_sketchModifier->kind);
    m_sketchModifier.reset();
    m_viewport->setSketchModifierPicking(false);
    m_viewport->setExternalReferencePicking(false);
    m_viewport->setDimensionSelection({}, {});
    m_viewport->setSketchModifierPreview(nullptr);
    m_viewport->clearSelection();
    m_managerStack->setCurrentIndex(0);
    m_managerStack->removeWidget(page);
    page->hide();
    page->setParent(nullptr);
    page->deleteLater();
    m_historyDock->setWindowTitle("FeatureManager");
    if (accepted && !lineEdit)
    {
        m_viewport->setSketch(*m_session.editingSketch());
        setWindowModified(true);
        updateEntityList();
    }
    updateActions();
    m_viewport->setFocus();
    statusBar()->showMessage(lineEdit ? title + " closed."
                                    : accepted ? title + " applied." : title + " canceled.", 3000);
}

void MainWindow::syncExternalReferencePicker()
{
    if (!m_sketchModifier || m_sketchModifier->kind != Modifier::ExternalReference)
        return;
    std::vector<ExternalReferencePickItem> candidates;
    std::vector<std::string> selected;
    for (const auto& option : m_sketchModifier->externalOptions)
    {
        if (option.selectionShape.IsNull()) continue;
        const auto kind = option.selectionShape.ShapeType() == TopAbs_FACE
            ? modeling::DatumKind::Plane
            : option.projectedGeometry.kind == modeling::EntityKind::Point
                ? modeling::DatumKind::Point : modeling::DatumKind::Axis;
        candidates.push_back({option.id, kind, option.selectionShape});
    }
    for (const auto* item : m_sketchModifier->externalReferences->selectedItems())
        selected.push_back(item->data(Qt::UserRole).toString().toStdString());
    m_viewport->setExternalReferencePicking(true, std::move(candidates), std::move(selected));
}

void MainWindow::updateExternalReferenceSession()
{
    if (!m_sketchModifier || m_sketchModifier->kind != Modifier::ExternalReference)
        return;
    if (m_sketchModifier->commandSessionId.empty())
    {
        refreshSketchModifier();
        syncExternalReferencePicker();
        return;
    }
    application::CommandRequest request{"sketch.external_reference"};
    std::vector<std::string> sources;
    for (const auto* item : m_sketchModifier->externalReferences->selectedItems())
        sources.push_back(item->data(Qt::UserRole).toString().toStdString());
    request.parameters["sources"] = std::move(sources);
    request.parameters["associative"] = m_sketchModifier->externalMode->currentData().toBool();
    const auto result = m_commands.update(m_sketchModifier->commandSessionId, request);
    if (result.state == application::CommandState::Rejected || result.state == application::CommandState::Failed)
        m_sketchModifier->page->setStatus(QString::fromStdString(result.message), true);
}

application::CommandResult MainWindow::updateExternalReferenceCommand(
    const application::CommandRequest& request)
{
    if (!m_sketchModifier || m_sketchModifier->kind != Modifier::ExternalReference)
        return {application::CommandState::Failed, "The External References PropertyManager is unavailable"};
    const auto selected = request.parameters.find("sources");
    if (selected != request.parameters.end())
    {
        const auto& ids = std::get<std::vector<std::string>>(selected->second);
        for (const auto& id : ids)
            if (std::none_of(m_sketchModifier->externalOptions.begin(), m_sketchModifier->externalOptions.end(),
                             [&](const auto& option) { return option.id == id; }))
                return {application::CommandState::Rejected, "A requested source reference is unavailable"};
        const QSignalBlocker blocker(m_sketchModifier->externalReferences);
        for (int row = 0; row < m_sketchModifier->externalReferences->count(); ++row)
        {
            auto* item = m_sketchModifier->externalReferences->item(row);
            const auto id = item->data(Qt::UserRole).toString().toStdString();
            item->setSelected(std::find(ids.begin(), ids.end(), id) != ids.end());
        }
    }
    const auto associative = request.parameters.find("associative");
    if (associative != request.parameters.end())
    {
        const QSignalBlocker blocker(m_sketchModifier->externalMode);
        m_sketchModifier->externalMode->setCurrentIndex(std::get<bool>(associative->second) ? 0 : 1);
    }
    refreshSketchModifier();
    syncExternalReferencePicker();
    if (!m_sketchModifier->candidate)
        return {application::CommandState::WaitingForSelection,
                m_sketchModifier->page->status->text().toStdString()};
    return {application::CommandState::PreviewReady, "External reference preview ready"};
}

application::CommandResult MainWindow::confirmExternalReferenceCommand()
{
    if (!m_sketchModifier || m_sketchModifier->kind != Modifier::ExternalReference)
        return {application::CommandState::Failed, "The External References PropertyManager is unavailable"};
    try
    {
        // Refresh the source snapshot immediately before commit so an edit made
        // while the PropertyManager was open cannot attach stale copied points.
        m_sketchModifier->externalOptions = m_externalReferenceOptions(m_session.editingSketchId(), m_session.document());
        std::set<std::string> available;
        for (const auto& option : m_sketchModifier->externalOptions) available.insert(option.id);
        for (const auto* item : m_sketchModifier->externalReferences->selectedItems())
            if (!available.count(item->data(Qt::UserRole).toString().toStdString()))
                throw std::runtime_error("A selected source reference changed while the command was active. Reselect it.");
        refreshSketchModifier();
        if (!m_sketchModifier->candidate)
            throw std::runtime_error("Select one or more valid source references before confirming.");
        const auto mode = m_sketchModifier->externalMode->currentData().toBool();
        if (mode && m_externalReferenceValidator)
        {
            auto candidatePart = m_session.document();
            const auto feature = std::find_if(candidatePart.features().begin(), candidatePart.features().end(),
                [&](const auto& item) { return item.id == m_session.editingSketchId(); });
            if (feature == candidatePart.features().end())
                throw std::runtime_error("The active sketch is no longer available.");
            auto replacement = *feature;
            replacement.sketch = *m_sketchModifier->candidate;
            candidatePart.replaceFeature(replacement);
            std::string diagnostic;
            if (!m_externalReferenceValidator(candidatePart, &diagnostic))
                throw std::runtime_error(diagnostic.empty()
                    ? "The references are invalid or would create a circular dependency." : diagnostic);
        }
        finishSketchModifierLocally(true);
        return {application::CommandState::Committed,
                mode ? "Associative sketch references created" : "Reference copies created", {}, {}, true};
    }
    catch (const std::exception& error)
    {
        if (m_sketchModifier) m_sketchModifier->page->setStatus(QString::fromUtf8(error.what()), true);
        return {application::CommandState::Failed, error.what()};
    }
}

application::CommandResult MainWindow::cancelExternalReferenceCommand()
{
    finishSketchModifierLocally(false);
    return {application::CommandState::Canceled, "External reference command canceled"};
}

void MainWindow::breakSelectedExternalReference()
{
    if (!m_sketchModifier || m_sketchModifier->kind != Modifier::ExternalReference ||
        m_sketchModifier->existingExternalReferences->selectedItems().empty())
        return;
    const auto entityId = m_sketchModifier->existingExternalReferences->selectedItems().front()
        ->data(Qt::UserRole).toString().toStdString();
    const auto sketchId = m_session.editingSketchId();
    if (!m_sketchModifier->commandSessionId.empty())
        (void)m_commands.cancel(m_sketchModifier->commandSessionId);
    application::CommandRequest request{"sketch.break_external_reference"};
    request.parameters["sketch"] = sketchId;
    request.parameters["entity"] = entityId;
    const auto result = m_commands.execute(request);
    if (!result && !result.message.empty())
        statusBar()->showMessage(QString::fromStdString(result.message), 5000);
}

bool MainWindow::sketchModifierEvent(QObject* watched, QEvent* event)
{
    if (!m_sketchModifier || event->type() != QEvent::KeyPress)
        return false;
    const auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != this)
        return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape)
        finishSketchModifier(false);
    else if ((key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return) &&
             (widget == m_viewport || (m_sketchModifier->kind == Modifier::Offset &&
                 (widget == m_sketchModifier->spacing || m_sketchModifier->spacing->isAncestorOf(widget)))))
        finishSketchModifier(true);
    else
        return false;
    event->accept();
    return true;
}
}
