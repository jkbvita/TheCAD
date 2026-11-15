#include "ui/assembly/AssemblyWindow.h"
#include "ui/assembly/AssemblyPropertyEditor.h"
#include "ui/assembly/AssemblyBeltEditor.h"
#include "ui/assembly/AssemblyAssetEditor.h"
#include "ui/assembly/AssemblyReferenceWorkflowEditors.h"
#include "ui/assembly/AssemblyMagneticEditor.h"
#include "ui/assembly/AssemblyViewport.h"
#include "ui/assembly/ComponentPlacementEditor.h"
#include "ui/PropertyManagerPage.h"
#include <QCheckBox>
#include <QAction>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <algorithm>
namespace thecad::ui
{
void AssemblyWindow::startWorkflow(Workflow type, const std::string& id)
{
    if (draftActive())
        return;
    try
    {
        AssemblyPropertyEditor* editor = nullptr;
        if (type == Workflow::Belt)
        {
            std::optional<document::AssemblyBelt> editing;
            for (const auto& belt : document().belts())
                if (belt.id == id)
                    editing = belt;
            editor = new AssemblyBeltEditor(document(), editing, m_panels);
        }
        else if (type == Workflow::Asset)
            editor = new AssemblyAssetEditor(document(), id.empty() ? m_selected : id, m_panels);
        else if (type == Workflow::Ground)
            editor = new AssemblyGroundEditor(document(), m_panels);
        else
        {
            std::optional<document::InPlacePart> editing;
            for (const auto& part : document().inPlaceParts())
                if (part.id == id)
                    editing = part;
            editor = new AssemblyInPlaceEditor(document(), editing, m_panels);
        }
        showWorkflow(editor);
        if (id.empty() && type != Workflow::Asset)
            for (const auto& option : m_preselection)
                editor->pick(option.reference);
        updateWorkflowPreview();
    }
    catch (const std::exception& e)
    {
        reportError(QString::fromUtf8(e.what()));
    }
}
void AssemblyWindow::showWorkflow(AssemblyPropertyEditor* editor)
{
    m_workflow = editor;
    m_panels->addWidget(editor);
    m_panels->setCurrentWidget(editor);
    m_left->setSizes({260, 500});
    m_viewport->setSelected({});
    m_viewport->setMatePicking(true);
    connect(editor, &AssemblyPropertyEditor::previewChanged, this,
            &AssemblyWindow::updateWorkflowPreview);
    connect(editor->page()->buttons, &QDialogButtonBox::accepted, this,
            [this] { finishWorkflow(true); });
    connect(editor->page()->buttons, &QDialogButtonBox::rejected, this,
            [this] { finishWorkflow(false); });
    updateActions();
}
void AssemblyWindow::updateWorkflowPreview()
{
    if (m_workflow)
        render(m_workflow->candidate() ? *m_workflow->candidate() : document());
}
void AssemblyWindow::finishWorkflow(bool accepted)
{
    if (!m_workflow)
        return;
    if (accepted)
    {
        m_workflow->refreshPreview();
        if (!m_workflow->candidate())
            return;
        m_session.commit(*m_workflow->candidate());
    }
    auto* editor = m_workflow;
    if (accepted)
    {
        m_preselection.clear();
        m_selected.clear();
        m_selectedBelt.clear();
        if (auto* belt = dynamic_cast<AssemblyBeltEditor*>(editor))
            m_selectedBelt = belt->definition().id;
        if (auto* part = dynamic_cast<AssemblyInPlaceEditor*>(editor))
            m_selected = part->definition().componentId;
    }
    m_workflow = nullptr;
    m_viewport->setMatePicking(false);
    m_panels->setCurrentWidget(m_placement);
    m_panels->removeWidget(editor);
    editor->deleteLater();
    refresh();
    m_viewport->setSelected(m_selected);
}
void AssemblyWindow::editMate(const document::AssemblyMate& mate)
{
    if (draftActive())
        return;
    using K = document::AssemblyMateKind;
    if (mate.kind == K::Magnetic)
    {
        showWorkflow(new AssemblyMagneticEditor(document(), mate, m_panels));
        updateWorkflowPreview();
        return;
    }
    for (const auto& belt : document().belts())
        if (mate.id == "mate-rotation-" + belt.id || mate.id == "mate-length-" + belt.id ||
            mate.id == "mate-lock-" + belt.id)
        {
            startWorkflow(Workflow::Belt, belt.id);
            return;
        }
    for (const auto& part : document().inPlaceParts())
        if (mate.id == "mate-inplace-" + part.id)
        {
            startWorkflow(Workflow::InPlace, part.id);
            return;
        }
    if (mate.id.rfind("mate-ground-", 0) == 0)
    {
        startWorkflow(Workflow::Asset, mate.second.componentId);
        return;
    }
    startMate(mate);
}
void AssemblyWindow::cycleMagnetic(bool moving)
{
    if (!m_magneticAction->isChecked() || m_mate || m_workflow)
        return;
    const auto id = m_insertCandidate ? m_insertId
                    : m_dragCandidate ? m_magneticMovingId
                                      : m_selected;
    if (id.empty())
        return;
    const auto& source = m_insertCandidate ? *m_insertCandidate
                         : m_dragCandidate ? *m_dragCandidate
                                           : document();
    int count = 0;
    for (const auto& asset : source.assets())
        if ((asset.componentId == id) == moving)
            count += int(asset.connectors.size());
    if (!count)
        return;
    auto& cursor = moving ? m_magneticMovingConnector : m_magneticStationaryConnector;
    cursor = (cursor + 1) % count;
    if (m_insertCandidate || m_dragCandidate)
    {
        try
        {
            auto candidate = source;
            candidate.setPlacement(id, m_magneticRawPose);
            candidate.snapMagnetic(id, m_magneticDistance, m_magneticMovingConnector,
                                   m_magneticStationaryConnector);
            if (m_insertCandidate)
                m_insertCandidate = std::move(candidate);
            else
                m_dragCandidate = std::move(candidate);
            render(m_insertCandidate ? *m_insertCandidate : *m_dragCandidate);
        }
        catch (const std::exception& e)
        {
            statusBar()->showMessage(e.what());
        }
    }
    statusBar()->showMessage(QString("%1 connection point %2 · [ moving / ] stationary")
                                 .arg(moving ? "Moving" : "Stationary")
                                 .arg(cursor + 1));
}
} // namespace thecad::ui
