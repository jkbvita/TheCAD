#include "ui/features/SolidFeaturePanel.h"
#include "ui/PropertyManagerPage.h"
#include "modeling/Selection.h"
#include "modeling/PatternFeatures.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
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
    switch (kind)
    {
    case K::Revolve: return "Revolved Boss/Base";
    case K::RevolvedCut: return "Revolved Cut";
    case K::Fillet: return "Fillet";
    case K::Chamfer: return "Chamfer";
    case K::LinearPattern: return "Linear Pattern";
    case K::CircularPattern: return "Circular Pattern";
    case K::Mirror: return "Mirror";
    case K::Sweep: return "Swept Boss/Base";
    case K::SweptCut: return "Swept Cut";
    default: return "Solid Feature";
    }
}
QString description(document::FeatureKind kind)
{
    using K = document::FeatureKind;
    if (kind == K::LinearPattern)
        return "Select features or all bodies, then choose a straight direction. "
               "The instance count includes the original.";
    if (kind == K::CircularPattern)
        return "Select features or all bodies, then pick an axis, circular edge, or cylindrical face. "
               "The instance count includes the original.";
    if (kind == K::Mirror)
        return "Select features or all bodies and a mirror plane or planar face.";
    if (document::isSweepFeature(kind))
        return "Select a closed profile region and an open path sketch. "
               "One path endpoint must lie on the profile plane.";
    if (document::isRevolveFeature(kind))
        return "Select a closed sketch region and a line or reference axis in its plane. "
               "Set the angle and inspect the preview.";
    return "Select solid edges in the viewport or list. Edit the size and inspect the preview.";
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
    m_page = new PropertyManagerPage(title(m_feature.kind), description(m_feature.kind), this);
    layout->addWidget(m_page);
    m_page->buttons->setObjectName("solidFeatureButtons");
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    if (document::isPatternFeature(m_feature.kind))
    {
        buildPatternForm(form);
        m_initializing = false;
        refreshPreview();
        return;
    }
    if (document::isSweepFeature(m_feature.kind))
    {
        buildSweepForm(form);
        m_initializing = false;
        refreshPreview();
        return;
    }
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
        m_pickReference = new QPushButton("Select axis in viewport / tree");
        m_pickReference->setObjectName("revolvePickAxis");
        form->addRow(m_pickReference);
        connect(m_pickReference, &QPushButton::clicked, this, [this] { setContourMode(false); });
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

void SolidFeaturePanel::buildPatternForm(QFormLayout* form)
{
    const bool mirror = pickingMirrorPlane();
    const bool circular = m_feature.kind == document::FeatureKind::CircularPattern;
    m_patternReference = new QComboBox;
    m_patternReference->setObjectName("patternReference");
    m_patternReference->setPlaceholderText(mirror ? "Select a plane or face"
                                                 : circular ? "Select an axis" : "Select a direction");
    m_patternReference->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_patternReference->setMinimumContentsLength(18);
    int selectedReference = -1;
    for (const auto& option : m_support.referenceOptions(circular))
    {
        using S = modeling::ReferenceSource;
        const auto source = option.reference.source;
        const bool supported = source == S::Datum ||
            (mirror ? source == S::BasePlane || source == S::BodyFace
                    : source == S::BaseAxis || source == S::BodyEdge || source == S::SketchLine ||
                          (circular && (source == S::BodyFace || source == S::BodyCircleAxis)));
        if (!supported || option.geometry.kind != (mirror ? modeling::DatumKind::Plane
                                                           : modeling::DatumKind::Axis))
            continue;
        if ((source == S::BodyEdge || source == S::BodyFace || source == S::BodyCircleAxis) &&
            option.reference.topologyName.empty())
            continue;
        if (m_feature.pattern.reference && same(*m_feature.pattern.reference, option.reference))
            selectedReference = int(m_options.size());
        m_options.push_back(option);
        m_patternReference->addItem(QString::fromStdString(option.label));
    }
    m_patternReference->setCurrentIndex(selectedReference);
    form->addRow(mirror ? "Mirror Face/Plane" : circular ? "Pattern axis" : "Direction 1",
                 m_patternReference);
    if (!mirror)
    {
        m_parameter = new QDoubleSpinBox;
        m_parameter->setObjectName(circular ? "patternAngle" : "patternSpacing");
        m_parameter->setDecimals(6);
        m_parameter->setRange(0.000001, circular ? 360 : 1e9);
        m_parameter->setSuffix(circular ? QString::fromUtf8("°") : QString(" mm"));
        m_parameter->setValue(circular ? m_feature.pattern.angle : m_feature.pattern.spacing);
        m_parameter->setKeyboardTracking(false);
        if (circular)
        {
            m_equalSpacing = new QCheckBox("Equal spacing");
            m_equalSpacing->setObjectName("patternEqualSpacing");
            m_equalSpacing->setChecked(m_feature.pattern.equalSpacing);
            form->addRow(m_equalSpacing);
            auto* angleLabel = new QLabel(m_equalSpacing->isChecked() ? "Total angle" : "Angle between instances");
            form->addRow(angleLabel, m_parameter);
            connect(m_equalSpacing, &QCheckBox::toggled, this, [this, angleLabel](bool equal) {
                angleLabel->setText(equal ? "Total angle" : "Angle between instances");
                refreshPreview();
            });
        }
        else
            form->addRow("Spacing", m_parameter);
        m_instances = new QSpinBox;
        m_instances->setObjectName("patternInstances");
        m_instances->setRange(2, modeling::maximumPatternInstances);
        m_instances->setValue(m_feature.pattern.instances);
        m_instances->setKeyboardTracking(false);
        form->addRow("Instances", m_instances);
        m_reverse = new QCheckBox("Reverse direction");
        m_reverse->setObjectName("patternReverse");
        m_reverse->setChecked(m_feature.pattern.reverse);
        form->addRow(m_reverse);
        connect(m_parameter, &QDoubleSpinBox::valueChanged, this, [this] { refreshPreview(); });
        connect(m_instances, &QSpinBox::valueChanged, this, [this] { refreshPreview(); });
        connect(m_parameter->findChild<QLineEdit*>(), &QLineEdit::textChanged, this,
                [this] { refreshPreview(); });
        connect(m_instances->findChild<QLineEdit*>(), &QLineEdit::textChanged, this,
                [this] { refreshPreview(); });
        connect(m_reverse, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    }
    m_patternScope = new QComboBox;
    m_patternScope->setObjectName("patternScope");
    m_patternScope->addItems({"Selected features", "All solid bodies"});
    m_patternScope->setCurrentIndex(m_feature.pattern.wholePart ? 1 : 0);
    form->addRow("Scope", m_patternScope);
    m_page->settings->addLayout(form);
    auto* seedLabel = new QLabel(mirror ? "Features to Mirror" : "Features to Pattern");
    m_page->settings->addWidget(seedLabel);
    m_patternSeeds = new QListWidget;
    m_patternSeeds->setObjectName("patternSeeds");
    m_patternSeeds->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto& seed : m_support.features())
    {
        if (!document::isSolidFeature(seed.kind))
            continue;
        auto* item = new QListWidgetItem(QString::fromStdString(seed.name), m_patternSeeds);
        item->setData(Qt::UserRole, QString::fromStdString(seed.id));
        item->setSelected(std::find(m_feature.pattern.seeds.begin(), m_feature.pattern.seeds.end(),
                                   seed.id) != m_feature.pattern.seeds.end());
    }
    m_page->settings->addWidget(m_patternSeeds);
    m_page->settings->addStretch();
    const auto updateScope = [this, seedLabel] {
        const bool show = m_patternScope->currentIndex() == 0;
        seedLabel->setVisible(show);
        m_patternSeeds->setVisible(show);
        refreshPreview();
    };
    connect(m_patternScope, &QComboBox::currentIndexChanged, this, updateScope);
    connect(m_patternReference, &QComboBox::currentIndexChanged, this, [this] { refreshPreview(); });
    connect(m_patternSeeds, &QListWidget::itemSelectionChanged, this, [this] { refreshPreview(); });
    updateScope();
}

document::Feature SolidFeaturePanel::readPattern() const
{
    auto feature = m_feature;
    auto& pattern = feature.pattern;
    if (m_patternReference->currentIndex() < 0)
        throw std::runtime_error(pickingMirrorPlane() ? "Select a mirror plane or planar face."
                                 : m_equalSpacing ? "Select a pattern axis."
                                                  : "Select a straight pattern direction.");
    pattern.reference = m_options[std::size_t(m_patternReference->currentIndex())].reference;
    pattern.wholePart = m_patternScope->currentIndex() == 1;
    pattern.seeds.clear();
    if (!pattern.wholePart)
        for (auto* item : m_patternSeeds->selectedItems())
            pattern.seeds.push_back(item->data(Qt::UserRole).toString().toStdString());
    if (m_parameter)
    {
        bool parameterOk = false, countOk = false;
        const double value = m_parameter->locale().toDouble(m_parameter->cleanText(), &parameterOk);
        pattern.instances = m_instances->locale().toInt(m_instances->cleanText(), &countOk);
        if (!parameterOk || !countOk || !m_parameter->hasAcceptableInput() ||
            !m_instances->hasAcceptableInput())
            throw std::runtime_error("Enter valid spacing and instance count.");
        if (m_equalSpacing)
        {
            pattern.angle = value;
            pattern.equalSpacing = m_equalSpacing->isChecked();
        }
        else
            pattern.spacing = value;
        pattern.reverse = m_reverse->isChecked();
    }
    return feature;
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
        {
            auto* item = new QListWidgetItem(QString("Region %1").arg(i + 1), m_regions);
            if (m_pathSketch &&
                (m_contours.size() == 1 ||
                 (m_initializing &&
                  std::is_permutation(m_feature.contour.begin(), m_feature.contour.end(),
                                      m_contours[std::size_t(i)].entityIds.begin(),
                                      m_contours[std::size_t(i)].entityIds.end()))))
                item->setSelected(true);
        }
    }
    if (!m_pathSketch)
        rebuildOptions();
    else
        m_pickRegions->setEnabled(!m_contours.empty());
    setContourMode(!m_pathSketch || (sketch() && m_regions->selectedItems().isEmpty()));
    refreshPreview();
}
void SolidFeaturePanel::setContourMode(bool mode)
{
    m_contourMode = mode;
    if (m_pickReference)
    {
        m_pickReference->setCheckable(true);
        m_pickReference->setChecked(!mode);
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
    if (m_patternReference && m_patternReference->currentIndex() >= 0)
        result.push_back(m_options[std::size_t(m_patternReference->currentIndex())].selectionShape);
    if (m_pathSketch && m_pathSketch->currentIndex() >= 0)
    {
        const auto id = m_pathSketch->currentData().toString().toStdString();
        for (const auto& option : m_options)
            if (option.reference.featureId == id)
                result.push_back(option.selectionShape);
    }
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
        auto feature = m_feature;
        modeling::RegionReferences regions;
        double value = 0;
        if (m_parameter && !m_patternReference)
        {
            value = m_parameter->locale().toDouble(m_parameter->cleanText(), &numeric);
            if (!numeric || !m_parameter->hasAcceptableInput())
                throw std::runtime_error("Enter a valid positive parameter.");
        }
        if (m_patternReference)
            feature = readPattern();
        else if (m_pathSketch)
        {
            const auto selected = m_regions->selectedItems();
            if (!sketch() || selected.size() != 1)
                throw std::runtime_error("Select one closed profile region.");
            if (m_pathSketch->currentIndex() < 0)
                throw std::runtime_error("Select a path sketch with a single open chain of lines.");
            feature.sketchId = m_sketches->currentData().toString().toStdString();
            feature.pathSketchId = m_pathSketch->currentData().toString().toStdString();
            feature.contour = m_contours[std::size_t(m_regions->row(selected.front()))].entityIds;
        }
        else if (m_axis)
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
    if (m_patternReference)
        m_patternReference->setCurrentIndex(index);
    else if (m_pathSketch)
        m_pathSketch->setCurrentIndex(m_pathSketch->findData(
            QString::fromStdString(m_options[std::size_t(index)].reference.featureId)));
    else if (m_axis)
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
        if (m_pathSketch)
            m_regions->setCurrentItem(item, item->isSelected() ? QItemSelectionModel::Deselect
                                                             : QItemSelectionModel::ClearAndSelect);
        else
            item->setSelected(!item->isSelected());
        focusParameter();
    }
}
void SolidFeaturePanel::pickTree(const QString& id)
{
    if (m_patternReference)
    {
        using S = modeling::ReferenceSource;
        for (int i = 0; i < int(m_options.size()); ++i)
        {
            const auto& ref = m_options[std::size_t(i)].reference;
            const auto origin = ref.index == 0 ? "origin:XY" : ref.index == 1 ? "origin:XZ" : "origin:YZ";
            if ((ref.source == S::Datum && id.toStdString() == ref.featureId) ||
                (ref.source == S::BasePlane && id == origin))
            {
                pickReference(i);
                return;
            }
        }
        for (int i = 0; i < m_patternSeeds->count(); ++i)
        {
            auto* item = m_patternSeeds->item(i);
            if (item->data(Qt::UserRole).toString() == id)
            {
                m_patternScope->setCurrentIndex(0);
                item->setSelected(!item->isSelected());
                return;
            }
        }
        return;
    }
    if (m_pathSketch)
    {
        const int path = m_pathSketch->findData(id);
        if (path >= 0)
            m_pathSketch->setCurrentIndex(path);
        else if (const int profile = m_sketches->findData(id); profile >= 0)
            m_sketches->setCurrentIndex(profile);
        return;
    }
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
    if (m_pathSketch)
        return;
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
            if (m_patternReference)
            {
                using K = modeling::SelectionKind;
                using S = modeling::ReferenceSource;
                const bool plane = (selected.kind == K::OriginPlane && ref.source == S::BasePlane &&
                                    selected.index == ref.index) ||
                                   (selected.kind == K::ReferencePlane && ref.source == S::Datum &&
                                    selected.featureId == ref.featureId) ||
                                   (selected.kind == K::Face && ref.source == S::BodyFace &&
                                    selected.featureId == ref.featureId && selected.index == ref.index);
                const bool circle = selected.kind == K::Edge && ref.source == S::BodyCircleAxis &&
                                    selected.featureId == ref.featureId && selected.index == ref.index;
                if (edge || datum || plane || circle)
                    m_patternReference->setCurrentIndex(i);
            }
            else if (edge || datum)
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
    if (!m_parameter)
        return;
    m_parameter->setFocus();
    m_parameter->selectAll();
}
bool SolidFeaturePanel::parameterContains(QWidget* widget) const
{
    return (m_parameter && (widget == m_parameter || m_parameter->isAncestorOf(widget))) ||
           (m_instances && (widget == m_instances || m_instances->isAncestorOf(widget)));
}
} // namespace thecad::ui
