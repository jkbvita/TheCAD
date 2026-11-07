#include "ui/features/SolidFeaturePanel.h"
#include "ui/PropertyManagerPage.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopoDS_Edge.hxx>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace thecad::ui
{
void SolidFeaturePanel::buildSweepForm(QFormLayout* form)
{
    m_sketches = new QComboBox;
    m_sketches->setObjectName("sweepProfile");
    m_sketches->setPlaceholderText("Select a profile sketch");
    m_pathSketch = new QComboBox;
    m_pathSketch->setObjectName("sweepPath");
    m_pathSketch->setPlaceholderText("Select a path sketch");
    for (const auto& feature : m_support.features())
    {
        if (feature.kind != document::FeatureKind::Sketch)
            continue;
        const auto id = QString::fromStdString(feature.id);
        const auto label = QString::fromStdString(feature.name);
        try
        {
            (void)modeling::openSketchPath(feature.sketch);
        }
        catch (const std::exception&)
        {
            m_sketches->addItem(label, id);
            continue;
        }
        m_pathSketch->addItem(label, id);
        for (const auto& entity : feature.sketch.entities)
        {
            if (entity.construction || entity.kind != modeling::EntityKind::Line)
                continue;
            const auto first = modeling::sketchPoint(feature.sketch, entity.first);
            const auto last = modeling::sketchPoint(feature.sketch, entity.second);
            modeling::GeometryReference ref;
            ref.source = modeling::ReferenceSource::SketchLine;
            ref.featureId = feature.id;
            ref.entityId = entity.id;
            m_options.push_back({feature.name + " / " + entity.id, ref,
                                 {modeling::DatumKind::Axis, gp_Ax3(first, gp_Dir(gp_Vec(first, last)))},
                                 BRepBuilderAPI_MakeEdge(first, last).Edge()});
        }
    }
    auto pathId = QString::fromStdString(m_feature.pathSketchId);
    const auto selectedId = QString::fromStdString(m_feature.sketchId);
    if (!m_editing && m_pathSketch->findData(selectedId) >= 0)
        pathId = selectedId;
    m_sketches->setCurrentIndex(m_sketches->findData(selectedId));
    m_pathSketch->setCurrentIndex(m_pathSketch->findData(pathId));
    form->addRow("Profile sketch", m_sketches);
    form->addRow("Path sketch", m_pathSketch);
    m_pickReference = new QPushButton("Select path in viewport");
    m_pickReference->setObjectName("sweepPickPath");
    m_pickReference->setEnabled(m_pathSketch->count() > 0);
    form->addRow(m_pickReference);
    m_pickRegions = new QPushButton("Select profile region in viewport");
    m_pickRegions->setObjectName("sweepPickRegion");
    form->addRow(m_pickRegions);
    m_page->settings->addLayout(form);
    m_page->settings->addWidget(new QLabel("Profile Region"));
    m_regions = new QListWidget;
    m_regions->setObjectName("sweepRegions");
    m_regions->setSelectionMode(QAbstractItemView::SingleSelection);
    m_page->settings->addWidget(m_regions);
    m_page->settings->addStretch();
    connect(m_sketches, &QComboBox::currentIndexChanged, this, [this] { sketchChanged(); });
    connect(m_pathSketch, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
    connect(m_regions, &QListWidget::itemSelectionChanged, this, [this] { refreshPreview(); });
    connect(m_pickReference, &QPushButton::clicked, this, [this] { setContourMode(false); });
    connect(m_pickRegions, &QPushButton::clicked, this, [this] { setContourMode(sketch() != nullptr); });
    sketchChanged();
}
} // namespace thecad::ui
