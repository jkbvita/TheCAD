#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/MeasurePanel.h"
#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QStatusBar>
#include <QTreeWidget>
#include <algorithm>

namespace thecad::ui
{
using namespace modeling;
void MainWindow::createMeasurePanel()
{
    m_measurePanel = new MeasurePanel(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_measurePanel, Qt::Vertical);
    m_measurePanel->hide();
    connect(m_measurePanel, &MeasurePanel::closed, this, [this] { setMeasureActive(false); });
    connect(m_measurePanel->toggleViewAction(), &QAction::triggered, this,
            &MainWindow::setMeasureActive);
    connect(m_measurePanel, &MeasurePanel::clearPicks, m_viewport, &CadViewport::clearMeasurePicks);
    connect(m_measurePanel, &MeasurePanel::removePick, m_viewport, &CadViewport::removeMeasurePick);
    connect(m_measurePanel, &MeasurePanel::pointModeChanged, m_viewport,
            &CadViewport::setMeasurePointMode);
    connect(m_measurePanel, &MeasurePanel::projectionPick, m_viewport,
            &CadViewport::requestMeasurePlane);
    connect(m_viewport, &CadViewport::measurePlanePicked, m_measurePanel,
            &MeasurePanel::setProjectionPlane);
    connect(m_viewport, &CadViewport::measurePickMessage, this,
            [this](QString msg) { statusBar()->showMessage(msg, 4500); });
    connect(m_viewport, &CadViewport::measureChanged, this, &MainWindow::measureSelectionChanged);
    connect(m_measurePanel, &MeasurePanel::optionsChanged, this,
            [this]
            {
                if (!m_measure->isChecked())
                    return;
                refreshMeasurement();
                m_measurePanel->saveHistory(QFileInfo(windowFilePath()).fileName());
            });
    // Coalesce screen projections during orbit. Formatting and camera movement reuse the BRep
    // reading.
    auto* settle = new QTimer(m_measurePanel);
    settle->setSingleShot(true);
    settle->setInterval(100);
    connect(m_viewport, &CadViewport::measureViewChanged, this,
            [this, settle]
            {
                if (m_measurePanel->projection->currentIndex() == 1)
                    settle->start();
            });
    connect(settle, &QTimer::timeout, this, &MainWindow::refreshMeasurement);
}
void MainWindow::setMeasureActive(bool active)
{
    m_measure->setChecked(active);
    if (!active)
    {
        const bool wasOpen = m_viewport->measuring();
        m_measurePanel->hide();
        m_viewport->setMeasurePicking(false);
        m_measurePanel->resetSession();
        if (wasOpen)
            updateActions();
        return;
    }
    closeSmartDimension();
    hideRelationsContext();
    if (m_session.editingSketch() && m_viewport->sketchTool())
        m_viewport->selectSketchTool();
    std::vector<std::pair<QString, gp_Ax3>> frames{{"Part origin", gp_Ax3()}};
    if (m_session.editingSketch())
        frames.emplace_back("Active sketch", sketchAxes(*m_session.editingSketch()));
    for (const auto& f : document().features())
        if (f.kind == document::FeatureKind::ReferenceGeometry && f.datum.kind == DatumKind::Plane)
            frames.emplace_back(QString::fromStdString(f.name) + " frame", f.datum.frame);
    m_measurePanel->setFrames(frames);
    m_viewport->setMeasurePicking(true);
    m_measurePanel->show();
    m_measurePanel->raise();
    // Give readings room below the tree, including on a laptop at high DPI.
    if (m_historyDock->isVisible() && !m_historyDock->isFloating() &&
        dockWidgetArea(m_historyDock) == Qt::LeftDockWidgetArea)
    {
        const int available = m_viewport->height();
        const int preferred = std::min(480, available * 2 / 3);
        resizeDocks({m_historyDock, m_measurePanel}, {available - preferred, preferred}, Qt::Vertical);
    }
    updateActions();
    measureSelectionChanged();
    m_viewport->setFocus();
}
void MainWindow::updateMeasureAction()
{
    const bool available = !featureDraftActive() && !m_selectingPlane && !m_constraints &&
                           !m_sketchModifier &&
                           (m_session.editingSketch() || !document().features().empty());
    if (!available && m_measure->isChecked())
        setMeasureActive(false);
    m_measure->setEnabled(available);
    m_measurePanel->toggleViewAction()->setEnabled(available);
}
void MainWindow::measureTreeSelection()
{
    if (auto* item = m_tree->currentItem())
    {
        auto id = item->data(0, Qt::UserRole).toString();
        bool solid = false;
        for (const auto& f : document().features())
            if (f.id == id.toStdString())
                solid = document::isSolidFeature(f.kind);
        m_viewport->measureTreeItem(id, solid);
    }
}
void MainWindow::measureSelectionChanged()
{
    if (!m_measure->isChecked())
        return;
    const auto& picks = m_viewport->measurementSelection();
    m_measurePanel->setPicks(picks);
    std::vector<MeasureEntity> entities;
    for (const auto& p : picks)
        entities.push_back(p.entity);
    m_measurePanel->measurement = measureEntities(entities);
    refreshMeasurement();
    m_measurePanel->saveHistory(QFileInfo(windowFilePath()).fileName());
}
void MainWindow::refreshMeasurement()
{
    if (!m_measure || !m_measure->isChecked())
        return;
    auto& panel = *m_measurePanel;
    const auto& picks = m_viewport->measurementSelection();
    const auto& r = panel.measurement;
    panel.setArcAvailability(r && r->centerSpan, r && r->arcConditions[8],
                             r && r->customArcConditions);
    if (!r)
    {
        panel.setReading({}, picks.empty() ? "Select geometry to measure."
                                           : "No measurement is available for this combination.");
        m_viewport->showMeasurement();
        return;
    }
    const auto units = panel.units();
    const auto frame = panel.frame();
    std::vector<MeasureValue> rows;
    std::vector<MeasureGraphic> graphics;
    auto read = [&](QString name, double value, int power = 1)
    {
        auto row = units.format(name, value, power);
        rows.push_back(row);
        return name + "  " + row.display;
    };
    auto add = [&](QString name, std::optional<double> value, int power = 1)
    { return value ? read(name, *value, power) : QString{}; };
    auto coords = [&](QString prefix, const gp_Pnt& point)
    {
        auto xyz = measureCoordinates(point, frame);
        QStringList values;
        for (int i = 1; i <= 3; ++i)
            values << read(prefix + QString("XYZ")[i - 1], xyz.Coord(i));
        return values.join("   ");
    };
    auto spanGraphic =
        [&](QString text, const MeasureSpan& span, MeasureInk ink = MeasureInk::Reading)
    { graphics.push_back({text, gp_Pnt((span.first.XYZ() + span.second.XYZ()) * .5), span, ink}); };
    QString primary;
    if (r->coordinates)
        primary = coords({}, *r->coordinates);
    primary = r->length ? read("Length", *r->length) : primary;
    if (r->area)
        primary = read("Area", *r->area, 2);
    if (r->perimeter)
        primary += "   " + read("Perimeter", *r->perimeter);
    if (r->radius)
    {
        auto radius = read("Radius", *r->radius);
        auto diameter = read("Diameter", 2 * *r->radius);
        primary = r->sweepDegrees && *r->sweepDegrees < 360. - 1e-7 ? radius : diameter;
    }
    add("Circumference", r->circumference);
    add("Sweep", r->sweepDegrees, -1);
    if (r->center)
        coords(r->diameterSpan ? "Center " : "Axis point ", *r->center);
    if (r->direction)
    {
        const auto d = measureComponents({gp::Origin(), gp_Pnt(r->direction->XYZ())}, frame);
        QStringList parts;
        for (int i = 1; i <= 3; ++i)
            parts << units.format({}, d.Coord(i), 0).number;
        const auto vector = parts.join(", ");
        rows.push_back({"Direction", vector, vector});
        if (primary.isEmpty())
            primary = "Direction  " + vector;
    }
    if (r->totalArea)
    {
        auto value = read("Total area", *r->totalArea, 2);
        if (primary.isEmpty())
            primary = value;
    }
    if (r->totalLength)
    {
        auto value = read("Total length", *r->totalLength);
        if (primary.isEmpty())
            primary = value;
    }
    auto span = r->span;
    if (r->distance)
    {
        primary = read("Minimum distance", *r->distance);
        QString center = add("Center distance", r->centerDistance);
        auto maximum = circularSpan(*r, ArcCondition::Far, ArcCondition::Far);
        QString maxText;
        if (maximum)
            maxText = read(r->radialCylinderSpan ? "Maximum radial distance" : "Maximum distance",
                           maximum->first.Distance(maximum->second));
        switch (panel.circle->currentIndex())
        {
        case 1:
            span = r->centerSpan;
            primary = center;
            break;
        case 2:
            span = maximum;
            primary = maxText;
            break;
        case 3:
            span = circularSpan(*r, ArcCondition(panel.firstCondition->currentIndex()),
                                ArcCondition(panel.secondCondition->currentIndex()));
            if (span)
                primary = read("Custom distance", span->first.Distance(span->second));
            break;
        }
    }
    if (span)
        spanGraphic(primary, *span);
    else if (r->diameterSpan)
        spanGraphic(primary, *r->diameterSpan);
    else if (r->anchor && !primary.isEmpty())
        graphics.push_back({primary, *r->anchor});
    if (r->angleDegrees)
    {
        QString angle = read("Angle", *r->angleDegrees, -1);
        if (r->anchor && *r->angleDegrees > 1e-7)
            graphics.push_back({angle, *r->anchor, {}, MeasureInk::Reading, r->angleDirections});
    }
    if (r->normalDistance)
    {
        const auto label = read("Normal distance", *r->normalDistance);
        if (r->normalSpan && (!span || std::abs(span->first.Distance(span->second) -
                                                *r->normalDistance) > Precision::Confusion()))
            spanGraphic(label, *r->normalSpan, MeasureInk::Normal);
    }
    if (span && panel.showXYZ())
    {
        const auto d = measureComponents(*span, frame);
        auto a = span->first;
        const gp_Dir axes[]{frame.XDirection(), frame.YDirection(), frame.Direction()};
        const MeasureInk inks[]{MeasureInk::X, MeasureInk::Y, MeasureInk::Z};
        for (int i = 0; i < 3; ++i)
        {
            auto v = d.Coord(i + 1);
            auto b = a.Translated(gp_Vec(axes[i]) * v);
            auto text = read("Δ" + QString("XYZ").mid(i, 1), v);
            if (std::abs(v) > Precision::Confusion())
                spanGraphic(text, {a, b}, inks[i]);
            a = b;
        }
    }
    const int mode = panel.projection->currentIndex();
    const auto plane = mode == 1   ? std::optional<gp_Pln>(m_viewport->measureScreenPlane())
                       : mode == 2 ? panel.selectedPlane
                                   : std::nullopt;
    QString note;
    if (mode == 2 && !plane)
        note = "Pick a planar face or a reference plane for projection.";
    if (plane && span)
    {
        const auto p = projectMeasurement(*span, *plane);
        spanGraphic(read("Projection", p.inPlane.first.Distance(p.inPlane.second)), p.inPlane,
                    MeasureInk::Projection);
        spanGraphic(read("Normal component", p.normal.first.Distance(p.normal.second)), p.normal,
                    MeasureInk::Normal);
    }
    if (plane && picks.size() == 1 && r->area)
    {
        const auto& shape = picks.front().entity.shape;
        panel.updateProjectedArea(shape, *plane);
        if (panel.computingArea())
            note = "Calculating projected area…";
        else if (panel.projectedArea)
            read("Projected area", *panel.projectedArea, 2);
        else
            note = "The projected outline could not be resolved.";
    }
    panel.setReading(rows, note);
    m_viewport->showMeasurement(panel.callouts() ? std::move(graphics)
                                                 : std::vector<MeasureGraphic>{},
                                panel.resultTextPixels());
}
} // namespace thecad::ui
