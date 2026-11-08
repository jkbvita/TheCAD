#include "ui/MainWindow.h"
#include "ui/features/SolidFeaturePanel.h"
#include "ui/PropertyManagerPage.h"
#include "ui/CadViewport.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <algorithm>
namespace thecad::ui
{
void MainWindow::startSolidFeature(document::FeatureKind kind, const document::Feature* editing)
{
    if (featureDraftActive() || m_selectingPlane || m_constraints ||
        (m_smartDimension && m_smartDimension->editor))
        return;
    try
    {
        document::Feature feature;
        if (editing)
            feature = *editing;
        else
            feature.kind = kind;
        auto source = m_session.document();
        if (m_session.editingSketch())
        {
            if (!document::isRevolveFeature(kind) && !document::isSweepFeature(kind))
                return;
            auto found =
                std::find_if(source.features().begin(), source.features().end(),
                             [this](const auto& f) { return f.id == m_session.editingSketchId(); });
            if (found == source.features().end())
                return;
            auto sketch = *found;
            sketch.sketch = *m_session.editingSketch();
            source.replaceFeature(sketch);
            feature.sketchId = sketch.id;
        }
        else if (!editing)
        {
            if (const auto* selected = selectedFeature())
            {
                if (document::isPatternFeature(kind) && document::isSolidFeature(selected->kind))
                    feature.pattern.seeds = {selected->id};
                if ((document::isRevolveFeature(kind) || document::isSweepFeature(kind)) &&
                    selected->kind == document::FeatureKind::Sketch)
                    feature.sketchId = selected->id;
            }
        }
        const auto preselection = m_viewport->selection();
        auto* panel =
            new SolidFeaturePanel(std::move(source), feature, editing != nullptr, m_managerStack);
        closeSmartDimension();
        m_solidFeature = panel;
        m_solidFromSketch = m_session.editingSketch().has_value();
        m_solidPreviousPlanes = m_viewport->referencePlanesVisible();
        m_mode = WorkspaceMode::FeatureDraft;
        hideRelationsContext();
        m_viewport->clearSelection();
        m_selectedFace = 0;
        m_selectedPlane.reset();
        m_viewport->setSelectedReferencePlane({});
        m_viewport->endSketch();
        if (!m_viewport->displayedShape().IsEqual(panel->support().shape()) &&
            !m_viewport->setShape(panel->support().shape()))
        {
            finishSolidFeature(false);
            return;
        }
        std::vector<DatumVisual> datums;
        for (const auto& f : panel->support().features())
            if (f.kind == document::FeatureKind::ReferenceGeometry)
                datums.push_back({f.id, f.name, f.datum, f.referenceVisible});
        m_viewport->setDatums(datums);
        if (kind == document::FeatureKind::Mirror)
            m_viewport->showReferencePlanes(true);
        connect(panel, &SolidFeaturePanel::pickingChanged, this,
                &MainWindow::syncSolidFeaturePicking);
        connect(panel, &SolidFeaturePanel::previewChanged, this,
                &MainWindow::syncSolidFeaturePreview);
        connect(panel->page()->buttons, &QDialogButtonBox::accepted, this,
                [this] { finishSolidFeature(true); });
        connect(panel->page()->buttons, &QDialogButtonBox::rejected, this,
                [this] { finishSolidFeature(false); });
        m_managerStack->addWidget(panel);
        m_managerStack->setCurrentWidget(panel);
        m_historyDock->setWindowTitle("PropertyManager");
        if (!editing)
            panel->preselect(preselection);
        syncSolidFeaturePicking();
        syncSolidFeaturePreview();
        updateActions();
        panel->focusParameter();
    }
    catch (const std::exception& error)
    {
        reportError(QString::fromUtf8(error.what()));
    }
}
void MainWindow::syncSolidFeaturePicking()
{
    if (!m_solidFeature)
        return;
    m_viewport->setReferenceCandidates({}, {});
    m_viewport->endContourSelection();
    if (m_solidFeature->pickingContours())
    {
        if (const auto* sketch = m_solidFeature->sketch())
            m_viewport->beginContourSelection(*sketch);
    }
    else
    {
        std::vector<ReferencePickItem> items;
        for (const auto& option : m_solidFeature->options())
            items.push_back({option.reference, option.geometry, option.selectionShape});
        m_viewport->setReferenceCandidates(items, {});
    }
    syncSolidFeaturePreview();
}
void MainWindow::syncSolidFeaturePreview()
{
    if (!m_solidFeature)
        return;
    m_viewport->setSelectedContourFaces(m_solidFeature->selectedFaces());
    m_viewport->setFeatureReferenceShapes(m_solidFeature->selectedReferences());
    if (const auto& candidate = m_solidFeature->candidate())
        m_viewport->previewShape(candidate->previewShape, candidate->changes);
    else
        m_viewport->clearPreview();
}
void MainWindow::finishSolidFeature(bool accepted)
{
    if (!m_solidFeature)
        return;
    if (accepted)
    {
        if (!m_solidFeature->readyToConfirm())
            return;
        const auto result = *m_solidFeature->candidate();
        m_viewport->clearPreview();
        // History must capture the committed body's display, not the earlier
        // support body temporarily shown while editing a feature.
        if (!m_viewport->displayedShape().IsEqual(m_session.document().shape()) &&
            !m_viewport->setShape(m_session.document().shape()))
            return;
        if (!applyModel(result.document, result.featureId))
            return;
    }
    auto* panel = m_solidFeature;
    panel->stopEdgePreview();
    const bool mirror = panel->pickingMirrorPlane();
    const bool resume = m_solidFromSketch && !accepted;
    m_solidFeature = nullptr;
    m_solidFromSketch = false;
    QObject::disconnect(panel, nullptr, this, nullptr);
    m_managerStack->setCurrentIndex(0);
    m_managerStack->removeWidget(panel);
    panel->hide();
    panel->setParent(nullptr);
    panel->deleteLater();
    m_viewport->clearPreview();
    m_viewport->endContourSelection();
    m_viewport->setReferenceCandidates({}, {});
    m_viewport->setFeatureReferenceShapes({});
    if (!m_viewport->displayedShape().IsEqual(m_session.document().shape()))
        m_viewport->setShape(m_session.document().shape());
    if (resume)
    {
        m_mode = WorkspaceMode::EditingSketch;
        m_viewport->beginSketch(*m_session.editingSketch());
        m_tabs->setCurrentIndex(1);
    }
    else
    {
        if (accepted)
        {
            m_session.editingSketch().reset();
            m_session.editingSketchId().clear();
        }
        m_mode = WorkspaceMode::Part;
        m_viewport->endSketch();
    }
    m_historyDock->setWindowTitle("FeatureManager");
    updateDocumentUi();
    if (mirror)
        m_viewport->showReferencePlanes(m_solidPreviousPlanes);
    m_viewport->setFocus();
}
bool MainWindow::solidFeatureEvent(QObject* watched, QEvent* event)
{
    if (!m_solidFeature || event->type() != QEvent::KeyPress)
        return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != this)
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape)
    {
        finishSolidFeature(false);
        key->accept();
        return true;
    }
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        (widget == m_viewport || m_solidFeature->parameterContains(widget)))
    {
        finishSolidFeature(true);
        key->accept();
        return true;
    }
    if (widget == m_viewport && m_solidFeature->parameter() && !key->text().isEmpty() &&
        !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        (key->text().at(0).isDigit() || key->text() == "." || key->text() == ","))
    {
        m_solidFeature->focusParameter();
        QApplication::sendEvent(m_solidFeature->parameter(), key);
        return true;
    }
    return false;
}
} // namespace thecad::ui
