#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/Measurement.h"

#include <QAction>
#include <QLabel>
#include <QLocale>
#include <QStringList>

namespace thecad::ui
{
void MainWindow::setMeasureActive(bool active)
{
    m_measure->setChecked(active);
    if (active == !m_measureReadout->isHidden())
        return;
    m_viewport->setMeasurePicking(active);
    m_measureReadout->setVisible(active);
    if (active)
    {
        refreshMeasurement();
        m_viewport->setFocus();
    }
    else
        m_viewport->showMeasurement();
}

void MainWindow::updateMeasureAction()
{
    const bool available = !featureDraftActive() && !m_selectingPlane && !m_session.editingSketch() &&
                           !m_session.document().shape().IsNull();
    if (!available && m_measure->isChecked())
        setMeasureActive(false);
    m_measure->setEnabled(available);
}

void MainWindow::refreshMeasurement()
{
    if (!m_measure->isChecked())
        return;
    const auto shapes = m_viewport->measurementShapes();
    if (shapes.empty() || shapes.size() > 2)
    {
        m_measureReadout->setText("Measure\nSelect one or two vertices, edges, or faces in the viewport.");
        m_viewport->showMeasurement();
        return;
    }
    const auto result = modeling::measureGeometry(shapes.front(), shapes.size() == 2 ? shapes.back()
                                                                                    : TopoDS_Shape{});
    if (!result)
    {
        m_measureReadout->setText("Measure\nCould not measure this selection. Try a different edge, face, or vertex.");
        m_viewport->showMeasurement();
        return;
    }
    const auto number = [](double value) { return QLocale().toString(value, 'f', 3); };
    const auto mm = [&](double value) { return number(value) + " mm"; };
    QStringList readings;
    QString callout;
    if (result->length)
    {
        callout = mm(*result->length);
        readings << "Length  " + callout;
    }
    if (result->radius)
    {
        callout = QString::fromUtf8("Ø ") + mm(2 * *result->radius);
        readings << "Radius  " + mm(*result->radius) << "Diameter  " + mm(2 * *result->radius);
    }
    if (result->coordinates)
    {
        const auto& p = *result->coordinates;
        readings << "X  " + mm(p.X()) << "Y  " + mm(p.Y()) << "Z  " + mm(p.Z());
        callout = "X " + number(p.X()) + "  Y " + number(p.Y()) + "  Z " + mm(p.Z());
    }
    if (result->distance)
    {
        callout = mm(*result->distance);
        readings << "Minimum distance  " + callout;
    }
    if (result->centerDistance)
        readings << "Center to center  " + mm(*result->centerDistance);
    if (result->angleDegrees)
    {
        const auto angle = number(*result->angleDegrees) + QString::fromUtf8("°");
        readings << "Angle  " + angle;
        callout += QString::fromUtf8("  ·  ") + angle;
    }
    if (readings.isEmpty())
        readings << "Select another face, edge, or vertex to measure the distance.";
    else if (shapes.size() == 1)
        readings << "Select another entity to compare.";
    m_measureReadout->setText("Measure\n" + readings.join('\n'));
    m_viewport->showMeasurement(callout, &*result);
}
}
