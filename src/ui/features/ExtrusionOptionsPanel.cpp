#include "ui/features/ExtrusionOptionsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thecad::ui {
using namespace modeling;
namespace {
struct Choice { const char* label; ExtrudeEnd end; };
constexpr Choice ends[] = {
    {"Blind", ExtrudeEnd::Blind}, {"Through All", ExtrudeEnd::ThroughAll},
    {"Through All Both", ExtrudeEnd::ThroughAllBoth}, {"Up To Next", ExtrudeEnd::UpToNext},
    {"Up To Vertex", ExtrudeEnd::Vertex}, {"Up To Surface", ExtrudeEnd::Surface},
    {"Offset From Surface", ExtrudeEnd::SurfaceOffset}, {"Up To Body", ExtrudeEnd::Body},
    {"Mid Plane", ExtrudeEnd::MidPlane}
};

bool sameReference(const GeometryReference& a, const GeometryReference& b)
{
    if (a.source != b.source || a.featureId != b.featureId || a.entityId != b.entityId || a.role != b.role)
        return false;
    if (!a.topologyName.empty() && !b.topologyName.empty()) return a.topologyName == b.topologyName;
    return a.index == b.index && a.signature == b.signature;
}

QDoubleSpinBox* dimension(QFormLayout* form, const QString& label, double value, const char* name,
                          double minimum = 0.000001, double maximum = 1e9, const QString& unit = " mm")
{
    auto* field = new QDoubleSpinBox;
    field->setObjectName(name);
    field->setDecimals(6);
    field->setRange(minimum, maximum);
    field->setSuffix(unit);
    field->setValue(value);
    field->setKeyboardTracking(true);
    form->addRow(label, field);
    return field;
}

double read(const QDoubleSpinBox* field)
{
    bool ok = false;
    const auto value = field->locale().toDouble(field->cleanText(), &ok);
    if (!ok || !field->hasAcceptableInput()) throw std::runtime_error("Complete the numeric input before confirming.");
    return value;
}

QCheckBox* flag(QFormLayout* form, const QString& label, bool checked)
{
    auto* result = new QCheckBox(label);
    result->setChecked(checked);
    form->addRow(result);
    return result;
}

ExtrudeEnd condition(const QComboBox* field)
{
    return ExtrudeEnd(field->currentData().toInt());
}
}

QFormLayout* ExtrusionOptionsPanel::section(QVBoxLayout* layout, const QString& title, QCheckBox** enabled)
{
    auto* head = new QWidget;
    auto* row = new QHBoxLayout(head);
    row->setContentsMargins(0, 0, 0, 0);
    auto* fold = new QToolButton;
    fold->setObjectName("extrusionSection");
    fold->setText(title);
    fold->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fold->setArrowType(Qt::DownArrow);
    fold->setCheckable(true);
    fold->setChecked(true);
    fold->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    if (enabled) {
        *enabled = new QCheckBox;
        (*enabled)->setAccessibleName(title);
        row->addWidget(*enabled);
    }
    row->addWidget(fold);
    layout->addWidget(head);
    auto* body = new QWidget;
    auto* form = new QFormLayout(body);
    form->setContentsMargins(5, 1, 5, 4);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->addWidget(body);
    QObject::connect(fold, &QToolButton::toggled, body, [fold, body](bool expanded) {
        fold->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(expanded);
    });
    if (enabled) {
        body->setEnabled((*enabled)->isChecked());
        QObject::connect(*enabled, &QCheckBox::toggled, body, [body, fold](bool on) {
            body->setEnabled(on);
            if (on) fold->setChecked(true);
        });
        fold->setChecked(false);
    }
    return form;
}

ExtrusionOptionsPanel::ExtrusionOptionsPanel(QVBoxLayout* layout, QWidget* context,
        const document::Feature& feature, std::vector<document::ReferenceOption> options, Changed onChange, Pick onPick)
    : m_context(context), m_changed(std::move(onChange)), m_pick(std::move(onPick)),
      m_cut(feature.kind == document::FeatureKind::Cut), m_options(std::move(options))
{
    ExtrudeDefinition initial;
    if (feature.extrusion) initial = *feature.extrusion;
    else {
        initial.first.depth = feature.length;
        initial.first.end = feature.throughAll ? ExtrudeEnd::ThroughAll : ExtrudeEnd::Blind;
    }
    m_fromForm = section(layout, "From");
    m_from = new QComboBox;
    m_from->setObjectName("operationStartingReference");
    m_from->addItems({"Sketch Plane", "Surface/Face/Plane", "Vertex", "Offset"});
    m_from->setCurrentIndex(int(initial.start));
    m_fromForm->addRow(m_from);
    m_start = referenceField(m_fromForm, "Start reference", "extrudeStartReference");
    m_startOffset = dimension(m_fromForm, "Offset", std::abs(initial.startOffset), "extrudeStartOffset", 0);
    m_reverseStart = flag(m_fromForm, "Reverse start offset", initial.startOffset < 0);

    auto* first = section(layout, "Direction 1");
    m_reverse = new QComboBox;
    m_reverse->setObjectName("featureDirection");
    m_reverse->addItems({"Along sketch normal", "Opposite sketch normal"});
    m_reverse->setCurrentIndex(feature.direction == Direction::Positive ? 0 : 1);
    auto* reverse = new QPushButton("Reverse direction");
    reverse->setObjectName("featureReverseDirection");
    first->addRow(reverse, m_reverse);
    QObject::connect(reverse, &QPushButton::clicked, m_context, [this] { m_reverse->setCurrentIndex(1 - m_reverse->currentIndex()); });
    m_direction = referenceField(first, "Direction of extrusion", "extrudeDirectionReference");
    fill(m_direction, choices(DatumKind::Axis));
    m_direction.choices->setItemText(0, "Normal to sketch");
    restore(m_direction, initial.direction);
    addLeg(m_first, first, initial.first, false);
    if (m_cut) m_flip = flag(first, "Flip side to cut", initial.flipCut);
    else {
        m_merge = flag(first, "Merge result", initial.merge);
        m_merge->setVisible(std::any_of(m_options.begin(), m_options.end(), [](const auto& option) {
            return option.reference.source == ReferenceSource::BodyFace;
        }));
    }

    auto* second = section(layout, "Direction 2", &m_secondOn);
    m_secondOn->setObjectName("extrudeDirection2");
    m_secondOn->setChecked(initial.second.has_value());
    addLeg(m_second, second, initial.second.value_or(ExtrudeLeg{}), true);

    m_thinForm = section(layout, "Thin Feature", &m_thinOn);
    m_thinOn->setObjectName("extrudeThinFeature");
    m_thinOn->setChecked(initial.thin.has_value());
    const auto thin = initial.thin.value_or(ExtrudeThin{});
    m_thinMode = new QComboBox;
    m_thinMode->setObjectName("extrudeThinMode");
    m_thinMode->addItems({"One-Direction", "Mid Plane", "Two-Direction"});
    m_thinMode->setCurrentIndex(int(thin.mode));
    m_thinForm->addRow("Type", m_thinMode);
    m_wall = dimension(m_thinForm, "Thickness", thin.thickness, "extrudeThinThickness");
    m_otherWall = dimension(m_thinForm, "Thickness 2", thin.secondThickness, "extrudeThinThickness2");
    m_reverseWall = flag(m_thinForm, "Reverse thickness direction", thin.reverse);
    m_autoFillet = flag(m_thinForm, "Auto-fillet corners", thin.cornerRadius > 0);
    m_filletRadius = dimension(m_thinForm, "Fillet radius", thin.cornerRadius > 0 ? thin.cornerRadius : 1, "extrudeThinFilletRadius");
    if (!m_cut) {
        m_cap = flag(m_thinForm, "Cap ends", thin.endCap > 0);
        m_capThickness = dimension(m_thinForm, "Cap thickness", thin.endCap > 0 ? thin.endCap : 1, "extrudeCapThickness");
    }

    m_contourForm = section(layout, "Selected Contours");
    const auto bodyFaces = choices(DatumKind::Plane, false, true);
    if (!bodyFaces.empty()) {
        auto* scope = section(layout, "Feature Scope", &m_scopeOn);
        m_scopeOn->setObjectName("extrudeFeatureScope");
        auto* hint = new QLabel("Choose one face per body to limit which bodies are affected. All intersecting bodies are used by default.");
        hint->setWordWrap(true);
        scope->addRow(hint);
        m_scope = new QListWidget;
        m_scope->setObjectName("extrudeScopeBodies");
        m_scope->setSelectionMode(QAbstractItemView::MultiSelection);
        m_scope->setMaximumHeight(100);
        for (const int index : bodyFaces) {
            const auto& option = m_options[std::size_t(index)];
            auto* item = new QListWidgetItem(QString::fromStdString(option.label), m_scope);
            item->setData(Qt::UserRole, index);
            item->setSelected(std::any_of(initial.scope.begin(), initial.scope.end(),
                [&](const auto& ref) { return sameReference(ref, option.reference); }));
        }
        scope->addRow(m_scope);
        auto* pick = new QPushButton("Pick body face");
        scope->addRow(pick);
        m_scopeOn->setChecked(!initial.scope.empty());
        QObject::connect(pick, &QPushButton::clicked, m_context, [this] {
            m_picking = nullptr;
            m_pickingScope = true;
            m_pick(choices(DatumKind::Plane, false, true));
        });
        QObject::connect(m_scope, &QListWidget::itemSelectionChanged, m_context, [this] { changed(); });
    }

    refresh();
    restore(m_start, initial.startReference);
    restore(m_first.target, initial.first.target);
    if (initial.second) restore(m_second.target, initial.second->target);
    const auto watchCombo = [this](QComboBox* field) {
        QObject::connect(field, &QComboBox::currentIndexChanged, m_context, [this] { changed(); });
    };
    const auto watchCheck = [this](QCheckBox* field) {
        if (field) QObject::connect(field, &QCheckBox::toggled, m_context, [this] { changed(); });
    };
    const auto watchNumber = [this](QDoubleSpinBox* field) {
        if (!field) return;
        QObject::connect(field, &QDoubleSpinBox::valueChanged, m_context, [this] { changed(); });
        QObject::connect(field->findChild<QLineEdit*>(), &QLineEdit::textChanged, m_context,
            [this, field, invalid = false]() mutable {
                const bool wasInvalid = invalid;
                invalid = !field->hasAcceptableInput();
                if (invalid || wasInvalid) changed();
            });
    };
    for (auto* field : {m_from, m_start.choices, m_direction.choices, m_reverse, m_thinMode}) watchCombo(field);
    for (auto* field : {m_reverseStart, m_secondOn, m_thinOn, m_reverseWall, m_autoFillet, m_cap, m_merge, m_flip, m_scopeOn}) watchCheck(field);
    for (auto* field : {m_startOffset, m_wall, m_otherWall, m_filletRadius, m_capThickness}) watchNumber(field);
    for (const auto* leg : {&m_first, &m_second}) {
        for (auto* field : {leg->end, leg->target.choices}) watchCombo(field);
        for (auto* field : {leg->reverseOffset, leg->translate, leg->draft, leg->outward}) watchCheck(field);
        for (auto* field : {leg->depth, leg->offset, leg->angle}) watchNumber(field);
    }
}

void ExtrusionOptionsPanel::addLeg(Leg& leg, QFormLayout* form, const ExtrudeLeg& value, bool second)
{
    leg.form = form;
    leg.end = new QComboBox;
    leg.end->setObjectName(second ? "extrudeEndCondition2" : "operationEndCondition");
    for (const auto& option : ends) {
        if (second && (option.end == ExtrudeEnd::ThroughAllBoth || option.end == ExtrudeEnd::MidPlane)) continue;
        if (!m_cut && (option.end == ExtrudeEnd::ThroughAll || option.end == ExtrudeEnd::ThroughAllBoth)) continue;
        if (option.end == ExtrudeEnd::UpToNext && !m_cut &&
            std::none_of(m_options.begin(), m_options.end(), [](const auto& item) {
                return item.reference.source == ReferenceSource::BodyFace;
            })) continue;
        leg.end->addItem(option.label, int(option.end));
    }
    leg.end->setCurrentIndex(leg.end->findData(int(value.end)));
    form->addRow("End condition", leg.end);
    leg.depth = dimension(form, "Depth", value.depth, second ? "extrudeDepth2" : "featureLength");
    leg.depth->setAccessibleName("Feature depth in millimeters");
    leg.target = referenceField(form, "Limit reference", second ? "extrudeEndReference2" : "extrudeEndReference");
    leg.offset = dimension(form, "Offset distance", value.offset, second ? "extrudeEndOffset2" : "extrudeEndOffset", 0);
    leg.reverseOffset = flag(form, "Reverse offset", value.reverseOffset);
    leg.translate = flag(form, "Translate surface", value.translateSurface);
    leg.draft = flag(form, "Draft", value.draftAngle > 0);
    leg.angle = dimension(form, "Draft angle", value.draftAngle > 0 ? value.draftAngle : 1,
                          second ? "extrudeDraftAngle2" : "extrudeDraftAngle", 0.000001, 89.999999, "°");
    leg.outward = flag(form, "Draft outward", value.draftOutward);
}

ExtrusionOptionsPanel::ReferenceField ExtrusionOptionsPanel::referenceField(QFormLayout* form,
        const QString& label, const char* name)
{
    ReferenceField field{new QWidget, new QComboBox};
    field.choices->setObjectName(name);
    field.choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    field.choices->setMinimumContentsLength(8);
    auto* layout = new QHBoxLayout(field.row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(field.choices, 1);
    auto* button = new QPushButton("Pick");
    button->setAccessibleName("Pick " + label.toLower());
    layout->addWidget(button);
    form->addRow(label, field.row);
    QObject::connect(button, &QPushButton::clicked, m_context, [this, combo = field.choices] {
        for (auto* field : {&m_start, &m_direction, &m_first.target, &m_second.target})
            if (field->choices == combo) { startPicking(*field); break; }
    });
    return field;
}

void ExtrusionOptionsPanel::startPicking(ReferenceField& field)
{
    const bool cancel = m_picking == &field;
    m_picking = cancel ? nullptr : &field;
    m_pickingScope = false;
    std::vector<int> indexes;
    if (cancel) { m_pick({}); return; }
    for (int i = 1; i < field.choices->count(); ++i) indexes.push_back(field.choices->itemData(i).toInt());
    m_pick(indexes);
}

void ExtrusionOptionsPanel::picked(int index)
{
    auto* field = m_picking;
    const bool scope = m_pickingScope;
    m_picking = nullptr;
    m_pickingScope = false;
    m_pick({});
    if (field) field->choices->setCurrentIndex(field->choices->findData(index));
    if (scope) {
        for (int row = 0; row < m_scope->count(); ++row)
            if (auto* item = m_scope->item(row); item->data(Qt::UserRole).toInt() == index) {
                item->setSelected(!item->isSelected());
                break;
            }
    }
    changed();
}

std::vector<int> ExtrusionOptionsPanel::choices(DatumKind kind, bool faces, bool bodies) const
{
    std::vector<int> result;
    for (int i = 0; i < int(m_options.size()); ++i) {
        const auto& option = m_options[std::size_t(i)];
        const auto source = option.reference.source;
        if (source == ReferenceSource::BodyFace) {
            if ((bodies || faces) && !option.reference.topologyName.empty()) result.push_back(i);
        } else if (!bodies && option.geometry.kind == kind) result.push_back(i);
    }
    return result;
}

void ExtrusionOptionsPanel::fill(ReferenceField& field, const std::vector<int>& indexes)
{
    const int before = field.choices->currentData().isValid() ? field.choices->currentData().toInt() : -1;
    const QSignalBlocker blocked(field.choices);
    if (field.choices->count() == int(indexes.size()) + 1) {
        bool same = true;
        for (int i = 0; i < int(indexes.size()); ++i)
            same &= field.choices->itemData(i + 1).toInt() == indexes[std::size_t(i)];
        if (same) return;
    }
    field.choices->clear();
    field.choices->addItem("Select reference");
    for (const int index : indexes)
        field.choices->addItem(QString::fromStdString(m_options[std::size_t(index)].label), index);
    field.choices->setCurrentIndex(std::max(0, field.choices->findData(before)));
}

void ExtrusionOptionsPanel::restore(const ReferenceField& field, const std::optional<GeometryReference>& saved)
{
    if (!saved) return;
    for (int i = 1; i < field.choices->count(); ++i)
        if (sameReference(*saved, m_options[std::size_t(field.choices->itemData(i).toInt())].reference)) {
            field.choices->setCurrentIndex(i);
            return;
        }
}

void ExtrusionOptionsPanel::refreshLeg(Leg& leg)
{
    const auto end = condition(leg.end);
    const bool offset = end == ExtrudeEnd::SurfaceOffset;
    const bool face = end == ExtrudeEnd::Surface || offset;
    leg.form->setRowVisible(leg.depth, end == ExtrudeEnd::Blind || end == ExtrudeEnd::MidPlane);
    leg.depth->setEnabled(end == ExtrudeEnd::Blind || end == ExtrudeEnd::MidPlane);
    leg.form->setRowVisible(leg.target.row, end == ExtrudeEnd::Vertex || face || end == ExtrudeEnd::Body);
    leg.form->setRowVisible(leg.offset, offset);
    leg.form->setRowVisible(leg.reverseOffset, offset);
    leg.form->setRowVisible(leg.translate, offset);
    leg.form->setRowVisible(leg.angle, leg.draft->isChecked());
    leg.form->setRowVisible(leg.outward, leg.draft->isChecked());
    if (end == ExtrudeEnd::Vertex) fill(leg.target, choices(DatumKind::Point));
    else if (face || end == ExtrudeEnd::Body) fill(leg.target, choices(DatumKind::Plane, face, end == ExtrudeEnd::Body));
}

void ExtrusionOptionsPanel::refresh()
{
    const auto from = ExtrudeStart(m_from->currentIndex());
    m_fromForm->setRowVisible(m_startOffset, from == ExtrudeStart::Offset);
    m_fromForm->setRowVisible(m_reverseStart, from == ExtrudeStart::Offset);
    if (m_scopeOn) m_scopeOn->setEnabled(!m_merge || m_merge->isChecked());
    m_fromForm->setRowVisible(m_start.row, from == ExtrudeStart::Vertex || from == ExtrudeStart::Surface);
    if (from == ExtrudeStart::Vertex) fill(m_start, choices(DatumKind::Point));
    else if (from == ExtrudeStart::Surface) fill(m_start, choices(DatumKind::Plane, true));
    refreshLeg(m_first);
    refreshLeg(m_second);
    const auto end = condition(m_first.end);
    const bool symmetric = end == ExtrudeEnd::MidPlane || end == ExtrudeEnd::ThroughAllBoth;
    if (symmetric && m_secondOn->isChecked()) m_secondOn->setChecked(false);
    m_secondOn->setEnabled(!symmetric);
    const auto thin = ThinMode(m_thinMode->currentIndex());
    m_thinForm->setRowVisible(m_otherWall, thin == ThinMode::TwoDirections);
    m_thinForm->setRowVisible(m_reverseWall, thin != ThinMode::MidPlane);
    m_thinForm->setRowVisible(m_autoFillet, m_canFillet);
    m_thinForm->setRowVisible(m_filletRadius, m_canFillet && m_autoFillet->isChecked());
    if (m_cap) {
        const bool capsAvailable = !m_openProfile && (!m_merge || m_merge->isHidden()) && !m_secondOn->isChecked() &&
                                   end == ExtrudeEnd::Blind && !m_first.draft->isChecked();
        m_thinForm->setRowVisible(m_cap, capsAvailable);
        m_thinForm->setRowVisible(m_capThickness, capsAvailable && m_cap->isChecked());
    }
}

void ExtrusionOptionsPanel::changed()
{
    if (m_refreshing) return;
    m_refreshing = true;
    refresh();
    m_refreshing = false;
    m_changed();
}

void ExtrusionOptionsPanel::setOpenProfile(bool available, bool onlyOpen, bool canFillet)
{
    m_openProfile = available;
    m_canFillet = canFillet;
    if (onlyOpen && !thinEnabled()) {
        m_refreshing = true;
        m_thinOn->setChecked(true);
        m_refreshing = false;
    }
    m_thinOn->setEnabled(!onlyOpen);
    refresh();
}

bool ExtrusionOptionsPanel::thinEnabled() const { return m_thinOn->isChecked(); }
Direction ExtrusionOptionsPanel::direction() const { return m_reverse->currentIndex() == 0 ? Direction::Positive : Direction::Negative; }

std::optional<GeometryReference> ExtrusionOptionsPanel::reference(const ReferenceField& field) const
{
    const auto index = field.choices->currentData();
    if (!index.isValid()) return {};
    return m_options[std::size_t(index.toInt())].reference;
}

ExtrudeLeg ExtrusionOptionsPanel::readLeg(const Leg& fields) const
{
    ExtrudeLeg result;
    result.end = condition(fields.end);
    if (result.end == ExtrudeEnd::Blind || result.end == ExtrudeEnd::MidPlane) {
        if (!fields.depth->hasAcceptableInput()) throw std::runtime_error("Enter a valid positive depth in millimeters.");
        result.depth = read(fields.depth);
    }
    if (result.end == ExtrudeEnd::Vertex || result.end == ExtrudeEnd::Surface ||
        result.end == ExtrudeEnd::SurfaceOffset || result.end == ExtrudeEnd::Body) {
        result.target = reference(fields.target);
        if (!result.target) throw std::runtime_error("Select a limiting reference for each active direction.");
    }
    if (result.end == ExtrudeEnd::SurfaceOffset) {
        result.offset = read(fields.offset);
        result.reverseOffset = fields.reverseOffset->isChecked();
        result.translateSurface = fields.translate->isChecked();
    }
    if (fields.draft->isChecked()) {
        result.draftAngle = read(fields.angle);
        result.draftOutward = fields.outward->isChecked();
    }
    return result;
}

ExtrudeDefinition ExtrusionOptionsPanel::definition() const
{
    ExtrudeDefinition result;
    result.start = ExtrudeStart(m_from->currentIndex());
    if (result.start == ExtrudeStart::Offset)
        result.startOffset = read(m_startOffset) * (m_reverseStart->isChecked() ? -1 : 1);
    if (result.start == ExtrudeStart::Vertex || result.start == ExtrudeStart::Surface) {
        result.startReference = reference(m_start);
        if (!result.startReference) throw std::runtime_error("Select a start reference.");
    }
    result.direction = reference(m_direction);
    result.first = readLeg(m_first);
    if (m_secondOn->isChecked()) result.second = readLeg(m_second);
    if (thinEnabled()) {
        ExtrudeThin thin;
        thin.mode = ThinMode(m_thinMode->currentIndex());
        thin.thickness = read(m_wall);
        if (thin.mode == ThinMode::TwoDirections) thin.secondThickness = read(m_otherWall);
        thin.reverse = m_reverseWall->isChecked();
        if (m_canFillet && m_autoFillet->isChecked()) thin.cornerRadius = read(m_filletRadius);
        if (m_cap && !m_cap->isHidden() && m_cap->isChecked()) thin.endCap = read(m_capThickness);
        result.thin = thin;
    }
    result.merge = !m_merge || m_merge->isChecked();
    result.flipCut = m_flip && m_flip->isChecked();
    if (result.merge && m_scopeOn && m_scopeOn->isChecked()) {
        for (const auto* item : m_scope->selectedItems())
            result.scope.push_back(m_options[std::size_t(item->data(Qt::UserRole).toInt())].reference);
        if (result.scope.empty()) throw std::runtime_error("Pick at least one body for the feature scope.");
    }
    return result;
}

std::vector<TopoDS_Shape> ExtrusionOptionsPanel::selectedReferences() const
{
    std::vector<TopoDS_Shape> shapes;
    const auto add = [&](const ReferenceField& field) {
        const auto index = field.choices->currentData();
        if (!field.row->isHidden() && index.isValid()) shapes.push_back(m_options[std::size_t(index.toInt())].selectionShape);
    };
    add(m_start); add(m_direction); add(m_first.target);
    if (m_secondOn->isChecked()) add(m_second.target);
    return shapes;
}
}
