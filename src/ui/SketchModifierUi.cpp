#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/PropertyManagerPage.h"
#include "modeling/SketchContourEdits.h"
#include "modeling/SketchOffset.h"
#include <QCheckBox>
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
    }
    return {};
}
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
    closeSmartDimension();
    hideRelationsContext();
    m_viewport->selectSketchTool();
    m_sketchModifier = std::make_unique<SketchModifierDraft>();
    auto& draft = *m_sketchModifier;
    draft.kind = kind;
    draft.seeds = m_viewport->selectedSketchEntities();
    const bool convert = kind == Modifier::Convert;
    const bool lineEdit = modeling::isSketchLineEdit(kind);
    if (lineEdit)
        m_viewport->clearSelection();
    draft.page = new PropertyManagerPage(modifierTitle(kind),
        convert ? "Pick a planar face or edges in the viewport. Ctrl-click adds to the selection."
                : lineEdit ? "Hover over a straight line to preview. Each click applies one edit. Esc exits."
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
        draft.axis = new QComboBox;
        draft.axis->setObjectName("mirrorSketchAxis");
        draft.axis->setPlaceholderText("Select a line");
        const auto& entities = m_session.editingSketch()->entities;
        for (std::size_t i = 0; i < entities.size(); ++i)
            if (entities[i].kind == modeling::EntityKind::Line)
                draft.axis->addItem(QString("Line %1%2").arg(i + 1).arg(
                    entities[i].construction ? " (construction)" : ""),
                    QString::fromStdString(entities[i].id));
        draft.axis->setCurrentIndex(-1);
        form->addRow("Mirror about", draft.axis);
        draft.pickAxis = new QPushButton("Pick mirror line in viewport");
        draft.pickAxis->setObjectName("pickMirrorSketchAxis");
        draft.pickAxis->setCheckable(true);
        form->addRow(draft.pickAxis);
        connect(draft.pickAxis, &QPushButton::toggled, this, [this](bool checked) {
            if (checked) m_viewport->setFocus();
            refreshSketchModifier();
        });
        connect(draft.axis, &QComboBox::currentIndexChanged, this, [this] { refreshSketchModifier(); });
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
    connect(draft.page->buttons, &QDialogButtonBox::accepted, this, [this] { finishSketchModifier(true); });
    connect(draft.page->buttons, &QDialogButtonBox::rejected, this, [this] { finishSketchModifier(false); });
    m_viewport->setSketchModifierPicking(true, convert, lineEdit);
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
    if (modeling::isSketchLineEdit(draft.kind))
        return;
    const auto& sketch = *m_session.editingSketch();
    draft.candidate.reset();
    try
    {
        if (draft.kind == Modifier::Convert)
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
            if (viewportSelection)
            {
                if (draft.pickAxis && draft.pickAxis->isChecked())
                {
                    const int index = draft.axis->findData(QString::fromStdString(m_viewport->selectedSketchEntity()));
                    if (index >= 0)
                    {
                        const QSignalBlocker blockAxis(draft.axis), blockButton(draft.pickAxis);
                        draft.axis->setCurrentIndex(index);
                        draft.pickAxis->setChecked(false);
                    }
                }
                else
                    draft.seeds = m_viewport->selectedSketchEntities();
            }
            draft.selection->setText(QString("%1 seed entities selected").arg(draft.seeds.size()));
            modeling::SketchPattern parameters;
            parameters.kind = draft.kind;
            if (draft.axis)
            {
                if (draft.pickAxis->isChecked())
                    throw std::runtime_error("Click a sketch line to set the mirror axis.");
                if (draft.axis->currentIndex() < 0)
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
