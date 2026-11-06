#include "ui/features/SolidFeaturePanel.h"
#include "ui/PropertyManagerPage.h"
#include "modeling/Selection.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
namespace thecad::ui
{
namespace
{
bool same(const modeling::GeometryReference& a, const modeling::GeometryReference& b)
{
    return a.source == b.source && a.featureId == b.featureId && a.entityId == b.entityId &&
           (a.topologyName.empty() ? a.index == b.index : a.topologyName == b.topologyName);
}
QString title(document::FeatureKind kind)
{
    using K = document::FeatureKind;
    return kind == K::Revolve       ? "Revolved Boss/Base"
           : kind == K::RevolvedCut ? "Revolved Cut"
           : kind == K::Fillet      ? "Fillet"
                                    : "Chamfer";
}
} // namespace
SolidFeaturePanel::SolidFeaturePanel(document::PartDocument source, document::Feature feature,
                                     bool editing, QWidget* parent)
    : QWidget(parent), m_source(std::move(source)),
      m_support(editing ? m_source.beforeFeature(feature.id) : m_source),
      m_feature(std::move(feature)), m_editing(editing)
{
    setObjectName("solidFeaturePanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const bool revolve = document::isRevolveFeature(m_feature.kind);
    m_page = new PropertyManagerPage(
        title(m_feature.kind),
        revolve ? "Select a closed sketch region and a line or reference axis in its plane. Set "
                  "the angle and inspect the preview."
                : "Select solid edges in the viewport or list. Edit the size and inspect the "
                  "preview before confirming.",
        this);
    layout->addWidget(m_page);
    m_page->buttons->setObjectName("solidFeatureButtons");
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    if (revolve)
    {
        m_sketches = new QComboBox;
        m_sketches->setObjectName("revolveSketch");
        m_sketches->setPlaceholderText("Select a sketch");
        for (const auto& f : m_support.features())
            if (f.kind == document::FeatureKind::Sketch)
                m_sketches->addItem(QString::fromStdString(f.name), QString::fromStdString(f.id));
        m_sketches->setCurrentIndex(
            m_sketches->findData(QString::fromStdString(m_feature.sketchId)));
        form->addRow("Profile sketch", m_sketches);
        m_axis = new QComboBox;
        m_axis->setObjectName("revolveAxis");
        m_axis->setPlaceholderText("Select an axis");
        form->addRow("Axis of revolution", m_axis);
        m_pickAxis = new QPushButton("Select axis in viewport / tree");
        m_pickAxis->setObjectName("revolvePickAxis");
        form->addRow(m_pickAxis);
        connect(m_pickAxis, &QPushButton::clicked, this, [this] { setContourMode(false); });
        m_direction = new QComboBox;
        m_direction->setObjectName("revolveDirection");
        m_direction->addItems({"Forward", "Reverse"});
        m_direction->setCurrentIndex(m_feature.direction == modeling::Direction::Positive ? 0 : 1);
        form->addRow("Direction", m_direction);
    }
    m_parameter = new QDoubleSpinBox;
    m_parameter->setDecimals(6);
    m_parameter->setRange(0.000001, revolve ? 360 : 1e9);
    m_parameter->setObjectName(revolve ? "revolveAngle" : "edgeTreatmentSize");
    m_parameter->setSuffix(revolve ? " deg" : " mm");
    m_parameter->setValue(revolve ? m_feature.angle : m_feature.size);
    form->addRow(revolve                                           ? "Angle"
                 : m_feature.kind == document::FeatureKind::Fillet ? "Radius"
                                                                   : "Equal distance",
                 m_parameter);
    m_page->settings->addLayout(form);
    if (revolve)
    {
        m_pickRegions = new QPushButton("Select contours in viewport");
        m_pickRegions->setObjectName("revolvePickContours");
        m_page->settings->addWidget(m_pickRegions);
        connect(m_pickRegions, &QPushButton::clicked, this, [this] { setContourMode(true); });
        m_page->settings->addWidget(new QLabel("Selected Contours"));
        m_regions = new QListWidget;
        m_regions->setObjectName("revolveContours");
        m_regions->setSelectionMode(QAbstractItemView::MultiSelection);
        m_page->settings->addWidget(m_regions);
        connect(m_sketches, &QComboBox::currentIndexChanged, this, [this] { sketchChanged(); });
        connect(m_axis, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
        connect(m_direction, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
        connect(m_regions, &QListWidget::itemSelectionChanged, this, [this] { refreshPreview(); });
        sketchChanged();
    }
    else
    {
        m_edges = new QListWidget;
        m_edges->setObjectName("treatmentEdges");
        m_edges->setSelectionMode(QAbstractItemView::MultiSelection);
        m_page->settings->addWidget(new QLabel("Edges — click to add/remove"));
        m_page->settings->addWidget(m_edges);
        rebuildOptions();
        connect(m_edges, &QListWidget::itemSelectionChanged, this, [this] { refreshPreview(); });
    }
    connect(m_parameter, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
    connect(m_parameter->findChild<QLineEdit*>(), &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    m_page->settings->addStretch();
    m_initializing = false;
    if (revolve)
    {
        const auto refs = m_feature.contours.empty() ? modeling::RegionReferences{m_feature.contour}
                                                     : m_feature.contours;
        for (int i = 0; i < int(m_contours.size()); ++i)
            for (auto ids : refs)
            {
                auto actual = m_contours[std::size_t(i)].entityIds;
                std::sort(ids.begin(), ids.end());
                std::sort(actual.begin(), actual.end());
                if (ids == actual)
                    m_regions->item(i)->setSelected(true);
            }
        setContourMode(true);
    }
    refreshPreview();
}
const modeling::Sketch* SolidFeaturePanel::sketch() const
{
    if (!m_sketches)
        return nullptr;
    const auto id = m_sketches->currentData().toString().toStdString();
    for (const auto& f : m_support.features())
        if (f.id == id && f.kind == document::FeatureKind::Sketch)
            return &f.sketch;
    return nullptr;
}
void SolidFeaturePanel::rebuildOptions()
{
    const auto previous =
        m_axis && m_axis->currentIndex() >= 0 && m_axis->currentIndex() < int(m_options.size())
            ? std::optional<modeling::GeometryReference>{m_options[std::size_t(
                                                                       m_axis->currentIndex())]
                                                             .reference}
            : m_feature.axis;
    m_options.clear();
    if (m_axis)
    {
        const QSignalBlocker blocker(m_axis);
        m_axis->clear();
        for (const auto& option : m_support.referenceOptions())
        {
            using S = modeling::ReferenceSource;
            if (option.geometry.kind != modeling::DatumKind::Axis ||
                option.reference.source == S::BodyFace)
                continue;
            m_options.push_back(option);
            m_axis->addItem(QString::fromStdString(option.label));
        }
        m_axis->setCurrentIndex(-1);
        if (previous)
            for (int i = 0; i < int(m_options.size()); ++i)
                if (same(*previous, m_options[std::size_t(i)].reference))
                    m_axis->setCurrentIndex(i);
    }
    else
    {
        m_options = m_support.edgeOptions();
        const QSignalBlocker blocker(m_edges);
        m_edges->clear();
        for (const auto& option : m_options)
        {
            auto* item = new QListWidgetItem(QString::fromStdString(option.label), m_edges);
            for (const auto& edge : m_feature.edges)
                if (same(edge, option.reference))
                    item->setSelected(true);
        }
    }
}
void SolidFeaturePanel::sketchChanged()
{
    m_contours.clear();
    {
        const QSignalBlocker blocker(m_regions);
        m_regions->clear();
        try
        {
            if (const auto* value = sketch())
                m_contours = modeling::closedContours(*value);
        }
        catch (const std::exception& e)
        {
            m_page->setStatus(QString::fromUtf8(e.what()), true);
        }
        for (int i = 0; i < int(m_contours.size()); ++i)
            new QListWidgetItem(QString("Region %1").arg(i + 1), m_regions);
    }
    rebuildOptions();
    setContourMode(true);
    refreshPreview();
}
void SolidFeaturePanel::setContourMode(bool mode)
{
    m_contourMode = mode;
    if (m_pickAxis)
    {
        m_pickAxis->setCheckable(true);
        m_pickAxis->setChecked(!mode);
        m_pickRegions->setCheckable(true);
        m_pickRegions->setChecked(mode);
    }
    if (!m_initializing)
        emit pickingChanged();
}
std::vector<TopoDS_Face> SolidFeaturePanel::selectedFaces() const
{
    std::vector<TopoDS_Face> result;
    if (m_regions)
        for (auto* item : m_regions->selectedItems())
            result.push_back(m_contours[std::size_t(m_regions->row(item))].face);
    return result;
}
std::vector<TopoDS_Shape> SolidFeaturePanel::selectedReferences() const
{
    std::vector<TopoDS_Shape> result;
    if (m_axis && m_axis->currentIndex() >= 0)
        result.push_back(m_options[std::size_t(m_axis->currentIndex())].selectionShape);
    if (m_edges)
        for (auto* item : m_edges->selectedItems())
            result.push_back(m_options[std::size_t(m_edges->row(item))].selectionShape);
    return result;
}
void SolidFeaturePanel::refreshPreview()
{
    if (m_initializing)
        return;
    m_candidate.reset();
    m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    try
    {
        bool numeric = false;
        const double value = m_parameter->locale().toDouble(m_parameter->cleanText(), &numeric);
        if (!numeric || !m_parameter->hasAcceptableInput())
            throw std::runtime_error("Enter a valid positive parameter.");
        auto feature = m_feature;
        modeling::RegionReferences regions;
        if (m_axis)
        {
            if (!sketch())
                throw std::runtime_error("Select a profile sketch.");
            if (m_axis->currentIndex() < 0)
                throw std::runtime_error("Select an axis of revolution.");
            feature.axis = m_options[std::size_t(m_axis->currentIndex())].reference;
            feature.sketchId = m_sketches->currentData().toString().toStdString();
            feature.angle = value;
            feature.direction = m_direction->currentIndex() == 0 ? modeling::Direction::Positive
                                                                 : modeling::Direction::Negative;
            for (auto* item : m_regions->selectedItems())
                regions.push_back(m_contours[std::size_t(m_regions->row(item))].entityIds);
            if (regions.empty())
                throw std::runtime_error("Select at least one closed contour.");
            feature.contour.clear();
            feature.contours = regions;
        }
        else
        {
            feature.size = value;
            feature.edges.clear();
            for (auto* item : m_edges->selectedItems())
                feature.edges.push_back(m_options[std::size_t(m_edges->row(item))].reference);
            if (feature.edges.empty())
                throw std::runtime_error("Select one or more solid edges.");
        }
        m_candidate = application::FeatureOperation::preview(m_source, feature, regions, m_editing);
        m_page->setStatus("Preview ready. Enter confirms; Esc cancels.");
        m_page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
    catch (const std::exception& e)
    {
        m_page->setStatus(QString::fromUtf8(e.what()), true);
    }
    emit previewChanged();
}
void SolidFeaturePanel::pickReference(int index)
{
    if (index < 0 || index >= int(m_options.size()))
        return;
    if (m_axis)
    {
        m_axis->setCurrentIndex(index);
        setContourMode(true);
    }
    else
    {
        auto* item = m_edges->item(index);
        item->setSelected(!item->isSelected());
    }
    focusParameter();
}
void SolidFeaturePanel::pickRegion(modeling::Point2 point)
{
    if (!m_regions || !sketch())
        return;
    int hit = -1;
    for (int i = 0; i < int(m_contours.size()); ++i)
        if (modeling::contourContains(m_contours[std::size_t(i)], *sketch(), point))
        {
            if (hit >= 0)
            {
                m_page->setStatus(
                    "Several regions overlap here. Select the intended region from the list.",
                    true);
                return;
            }
            hit = i;
        }
    if (hit >= 0)
    {
        auto* item = m_regions->item(hit);
        item->setSelected(!item->isSelected());
        focusParameter();
    }
}
void SolidFeaturePanel::pickTree(const QString& id)
{
    if (!m_axis)
        return;
    const int sketchIndex = m_sketches->findData(id);
    if (sketchIndex >= 0)
    {
        m_sketches->setCurrentIndex(sketchIndex);
        return;
    }
    for (int i = 0; i < int(m_options.size()); ++i)
        if (m_options[std::size_t(i)].reference.source == modeling::ReferenceSource::Datum &&
            m_options[std::size_t(i)].reference.featureId == id.toStdString())
        {
            pickReference(i);
            return;
        }
}
void SolidFeaturePanel::preselect(const std::vector<modeling::SelectionRef>& selection)
{
    for (const auto& selected : selection)
        for (int i = 0; i < int(m_options.size()); ++i)
        {
            const auto& ref = m_options[std::size_t(i)].reference;
            const bool edge = selected.kind == modeling::SelectionKind::Edge &&
                              ref.source == modeling::ReferenceSource::BodyEdge &&
                              selected.featureId == ref.featureId && selected.index == ref.index;
            const bool datum = selected.kind == modeling::SelectionKind::ReferenceAxis &&
                               ref.source == modeling::ReferenceSource::Datum &&
                               selected.featureId == ref.featureId;
            if (edge || datum)
            {
                if (m_axis)
                    m_axis->setCurrentIndex(i);
                else
                    m_edges->item(i)->setSelected(true);
            }
        }
}
void SolidFeaturePanel::focusParameter()
{
    m_parameter->setFocus();
    m_parameter->selectAll();
}
bool SolidFeaturePanel::parameterContains(QWidget* widget) const
{
    return widget == m_parameter || m_parameter->isAncestorOf(widget);
}
} // namespace thecad::ui
