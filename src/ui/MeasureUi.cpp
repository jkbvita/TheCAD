#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/Measurement.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

namespace thecad::ui
{
QWidget* MainWindow::createMeasurePanel(QWidget* parent)
{
    m_measurePanel = new QWidget(parent);
    m_measurePanel->setObjectName("measurePanel");
    auto* layout = new QVBoxLayout(m_measurePanel);
    layout->setContentsMargins(6, 4, 6, 6);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel("Measure");
    title->setObjectName("operationTitle");
    header->addWidget(title, 1);
    auto* clear = new QPushButton("Clear");
    header->addWidget(clear);
    auto* copy = new QPushButton("Copy");
    header->addWidget(copy);
    layout->addLayout(header);

    auto* form = new QFormLayout;
    m_measureUnits = new QComboBox;
    m_measureUnits->setObjectName("measureUnits");
    m_measureUnits->addItem("mm", 1.0);
    m_measureUnits->addItem("cm", 10.0);
    m_measureUnits->addItem("m", 1000.0);
    m_measureUnits->addItem("in", 25.4);
    form->addRow("Units", m_measureUnits);
    m_measurePrecision = new QSpinBox;
    m_measurePrecision->setRange(0, 6);
    m_measurePrecision->setValue(3);
    m_measurePrecision->setObjectName("measurePrecision");
    form->addRow("Decimals", m_measurePrecision);
    m_measureCircleMode = new QComboBox;
    m_measureCircleMode->setObjectName("measureCircleMode");
    m_measureCircleMode->addItems({"Minimum distance", "Center to center"});
    m_measureCircleMode->setEnabled(false);
    form->addRow("Circular edges", m_measureCircleMode);
    m_measureXYZ = new QCheckBox("Show XYZ differences");
    m_measureXYZ->setObjectName("measureXYZ");
    form->addRow(m_measureXYZ);
    layout->addLayout(form);

    m_measureSelection = new QLabel;
    m_measureSelection->setObjectName("measureSelection");
    m_measureSelection->setWordWrap(true);
    layout->addWidget(m_measureSelection);
    m_measureReadout = new QLabel;
    m_measureReadout->setObjectName("measureReadout");
    m_measureReadout->setTextFormat(Qt::PlainText);
    m_measureReadout->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_measureReadout->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_measureReadout->setWordWrap(true);
    layout->addWidget(m_measureReadout);

    connect(clear, &QPushButton::clicked, m_viewport, &CadViewport::clearSelection);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_measureSelection->text() + '\n' + m_measureReadout->text());
    });
    connect(m_measureUnits, &QComboBox::currentIndexChanged, this, &MainWindow::refreshMeasurement);
    connect(m_measurePrecision, &QSpinBox::valueChanged, this, &MainWindow::refreshMeasurement);
    connect(m_measureCircleMode, &QComboBox::currentIndexChanged, this, &MainWindow::refreshMeasurement);
    connect(m_measureXYZ, &QCheckBox::toggled, this, &MainWindow::refreshMeasurement);
    m_measureReadout->hide();
    m_measurePanel->hide();
    return m_measurePanel;
}

void MainWindow::setMeasureActive(bool active)
{
    m_measure->setChecked(active);
    if (active == !m_measurePanel->isHidden())
        return;
    m_viewport->setMeasurePicking(active);
    m_measureReadout->setVisible(active);
    m_measurePanel->setVisible(active);
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
    const auto selection = m_viewport->measurementSelection();
    m_measureCircleMode->setEnabled(false);
    QStringList labels;
    for (const auto& [ref, shape] : selection)
    {
        const auto* label = shape.ShapeType() == TopAbs_VERTEX ? "Vertex" :
                            shape.ShapeType() == TopAbs_EDGE ? "Edge" : "Face";
        labels << QString("%1 %2").arg(label).arg(ref.index);
    }
    m_measureSelection->setText(labels.isEmpty() ? "No entities selected" : labels.join("  ·  "));
    if (selection.empty() || selection.size() > 2)
    {
        m_measureReadout->setText("Select one or two vertices, edges, or faces in the viewport.");
        m_viewport->showMeasurement();
        return;
    }
    auto result = modeling::measureGeometry(selection.front().second, selection.size() == 2 ?
                                            selection.back().second : TopoDS_Shape{});
    if (!result)
    {
        m_measureReadout->setText("Could not measure this selection.");
        m_viewport->showMeasurement();
        return;
    }
    const auto number = [&](double value) {
        return QLocale().toString(value, 'f', m_measurePrecision->value());
    };
    const double scale = m_measureUnits->currentData().toDouble();
    const auto unit = m_measureUnits->currentText();
    const auto length = [&](double value) { return number(value / scale) + " " + unit; };
    QStringList readings;
    QString callout;
    if (result->length)
    {
        callout = length(*result->length);
        readings << "Length  " + callout;
    }
    if (result->area)
    {
        callout = number(*result->area / (scale * scale)) + " " + unit + "²";
        readings << "Area  " + callout;
    }
    if (result->radius)
    {
        callout = "Ø " + length(2 * *result->radius);
        readings << "Radius  " + length(*result->radius) << "Diameter  " + length(2 * *result->radius);
    }
    if (result->coordinates)
    {
        const auto& point = *result->coordinates;
        readings << "X  " + length(point.X()) << "Y  " + length(point.Y()) << "Z  " + length(point.Z());
        callout = "X " + length(point.X()) + "  Y " + length(point.Y()) + "  Z " + length(point.Z());
    }
    if (result->distance)
    {
        callout = length(*result->distance);
        readings << "Minimum distance  " + callout;
    }
    if (result->centerDistance)
    {
        m_measureCircleMode->setEnabled(true);
        readings << "Center to center  " + length(*result->centerDistance);
        if (m_measureCircleMode->currentIndex() == 1)
        {
            result->span = result->centerSpan;
            const auto& [a, b] = *result->span;
            result->anchor = gp_Pnt((a.XYZ() + b.XYZ()) * 0.5);
            callout = length(*result->centerDistance);
        }
    }
    if (result->angleDegrees)
    {
        const auto angle = number(*result->angleDegrees) + "°";
        readings << "Angle  " + angle;
        callout += "  ·  " + angle;
    }
    if (m_measureXYZ->isChecked() && result->span)
    {
        const auto& [a, b] = *result->span;
        const auto x = "ΔX  " + length(b.X() - a.X());
        const auto y = "ΔY  " + length(b.Y() - a.Y());
        const auto z = "ΔZ  " + length(b.Z() - a.Z());
        readings << x << y << z;
        callout += '\n' + x + "  " + y + "  " + z;
    }
    if (readings.isEmpty())
        readings << "Select another face, edge, or vertex to measure the distance.";
    else if (selection.size() == 1)
        readings << "Select another entity to compare.";
    m_measureReadout->setText(readings.join('\n'));
    m_viewport->showMeasurement(callout, &*result);
}
}
