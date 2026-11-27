#include "ui/assembly/AssemblyMateParameters.h"
#include "ui/widgets/PropertyManagerControls.h"
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <cmath>
#include <algorithm>
namespace thecad::ui
{
using namespace modeling::assembly;
AssemblyMateParameters::AssemblyMateParameters(QWidget* parent) : QWidget(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto check = [&](const char* id, const char* label)
    {
        auto* w = new QCheckBox(label);
        m_fields[id] = w;
        w->setObjectName(QString("mateOption_%1").arg(id));
        m_form->addRow(w);
        connect(w, &QCheckBox::toggled, this, &AssemblyMateParameters::changed);
    };
    const auto number = [&](const char* id, const char* label, double value,
                            const char* suffix = "", double minimum = -1e9, double maximum = 1e9)
    {
        auto* w = new PropertyDimension;
        w->setRange(minimum, maximum);
        w->setSuffix(suffix);
        w->setValue(value);
        m_fields[id] = w;
        w->setObjectName(QString("mateOption_%1").arg(id));
        m_form->addRow(label, w);
        connect(w, &QDoubleSpinBox::valueChanged, this,
                [this, id]
                {
                    if (m_setting)
                        return;
                    if ((std::string(id) == "firstRatio" || std::string(id) == "secondRatio") &&
                        m_fields.count("geometryRatio"))
                    {
                        const QSignalBlocker block(m_fields.at("geometryRatio"));
                        static_cast<QCheckBox*>(m_fields.at("geometryRatio"))->setChecked(false);
                    }
                    emit changed();
                });
    };
    const auto choice = [&](const char* id, const char* label, QStringList choices)
    {
        auto* w = new QComboBox;
        w->addItems(choices);
        m_fields[id] = w;
        w->setObjectName(QString("mateOption_%1").arg(id));
        m_form->addRow(label, w);
        connect(w, &QComboBox::currentIndexChanged, this, &AssemblyMateParameters::changed);
    };
    check("lockRotation", "Lock rotation");
    check("alignAxes", "Align coordinate axes");
    check("internal", "Internal tangency");
    choice("firstArc", "First arc condition", {"Center", "Minimum", "Maximum"});
    choice("secondArc", "Second arc condition", {"Center", "Minimum", "Maximum"});
    number("minimum", "Minimum", 0, " mm");
    number("maximum", "Maximum", 100, " mm");
    choice("position", "Constraint", {"Free", "Centered", "Distance", "Percent"});
    number("distance", "Distance along", 0, " mm");
    number("percent", "Percent along", 50, " %", 0, 100);
    check("flipPosition", "Flip dimension");
    number("offset", "Offset", 0, " mm");
    number("rotation", "Profile rotation", 0, "°");
    check("clockwise", "Clockwise");
    choice("followAxis", "Follow path", {"Free", "X axis", "Y axis", "Z axis"});
    choice("upAxis", "Up vector axis", {"Free", "X axis", "Y axis", "Z axis"});
    check("flipTangent", "Flip tangent");
    check("flipUp", "Flip up vector");
    number("firstRatio", "First ratio", 1, "", 0.000001);
    number("secondRatio", "Second ratio", 1, "", 0.000001);
    number("pitch", "Travel per revolution", 1, " mm", 0.000001);
    check("geometryRatio", "Use selected geometry ratio");
    check("reverse", "Reverse motion");
    check("hingeLimits", "Specify angle limits");
    configure(MateKind::Coincident);
}
void AssemblyMateParameters::visible(const char* id, bool value)
{
    m_form->setRowVisible(m_fields.at(id), value);
}
void AssemblyMateParameters::configure(MateKind k)
{
    if (k != MateKind::Gear)
        for (auto id : {"firstRatio", "secondRatio"})
            m_fields.at(id)->setEnabled(true);
    for (const auto& [id, w] : m_fields)
        m_form->setRowVisible(w, false);
    const auto show = [&](std::initializer_list<const char*> names)
    {
        for (auto id : names)
            visible(id, true);
    };
    if (k == MateKind::Concentric || k == MateKind::Slot || k == MateKind::ProfileCenter)
        show({"lockRotation"});
    if (k == MateKind::Coincident)
        show({"alignAxes"});
    if (k == MateKind::Tangent || k == MateKind::Cam)
        show({"internal"});
    if (k == MateKind::Distance || k == MateKind::LimitDistance)
        show({"firstArc", "secondArc"});
    if (k == MateKind::LimitDistance || k == MateKind::LimitAngle || k == MateKind::Hinge)
    {
        show({"minimum", "maximum"});
        const bool angular = k != MateKind::LimitDistance;
        for (auto id : {"minimum", "maximum"})
        {
            auto* n = static_cast<QDoubleSpinBox*>(m_fields.at(id));
            n->setSuffix(angular ? "°" : " mm");
            n->setRange(angular ? 0 : -1e9, angular ? 180 : 1e9);
        }
    }
    if (k == MateKind::Width || k == MateKind::Path || k == MateKind::Slot)
    {
        show({"position", "distance", "percent", "flipPosition"});
    }
    if (k == MateKind::ProfileCenter)
        show({"offset", "rotation", "clockwise"});
    if (k == MateKind::Path)
        show({"followAxis", "upAxis", "flipTangent", "flipUp"});
    if (k == MateKind::LinearCoupler || k == MateKind::Gear)
        show({"firstRatio", "secondRatio", "reverse"});
    if (k == MateKind::Gear)
        show({"geometryRatio"});
    if (k == MateKind::RackPinion || k == MateKind::Screw)
        show({"pitch", "reverse"});
    if (k == MateKind::UniversalJoint || k == MateKind::Belt)
        show({"reverse"});
    if (k == MateKind::Hinge)
        show({"hingeLimits"});
}
MateParameters AssemblyMateParameters::parameters() const
{
    MateParameters p;
    const auto b = [&](const char* id)
    { return static_cast<QCheckBox*>(m_fields.at(id))->isChecked(); };
    const auto n = [&](const char* id)
    { return static_cast<QDoubleSpinBox*>(m_fields.at(id))->value(); };
    const auto i = [&](const char* id)
    { return static_cast<QComboBox*>(m_fields.at(id))->currentIndex(); };
    p.standard = {b("lockRotation"), b("alignAxes"), ArcCondition(i("firstArc")),
                  ArcCondition(i("secondArc")), b("internal")};
    p.limit = {n("minimum"), n("maximum")};
    p.position = {PositionMode(i("position")), n("distance"), n("percent"), b("flipPosition")};
    p.profile = {n("offset"), n("rotation"), b("clockwise")};
    p.path = {i("followAxis") - 1, i("upAxis") - 1, b("flipTangent"), b("flipUp")};
    p.coupler = {n("firstRatio"), n("secondRatio"), n("pitch"), b("reverse")};
    p.coupler.useGeometryRatio = b("geometryRatio");
    p.hinge.limits = b("hingeLimits");
    return p;
}
void AssemblyMateParameters::setParameters(const MateParameters& p)
{
    const QSignalBlocker block(this);
    m_setting = true;
    const auto b = [&](const char* id, bool value)
    { static_cast<QCheckBox*>(m_fields.at(id))->setChecked(value); };
    const auto n = [&](const char* id, double value)
    { static_cast<QDoubleSpinBox*>(m_fields.at(id))->setValue(value); };
    const auto i = [&](const char* id, int value)
    { static_cast<QComboBox*>(m_fields.at(id))->setCurrentIndex(value); };
    b("lockRotation", p.standard.lockRotation);
    b("alignAxes", p.standard.alignAxes);
    b("internal", p.standard.internalTangency);
    i("firstArc", int(p.standard.firstArc));
    i("secondArc", int(p.standard.secondArc));
    n("minimum", p.limit.minimum);
    n("maximum", p.limit.maximum);
    i("position", int(p.position.mode));
    n("distance", p.position.distance);
    n("percent", p.position.percent);
    b("flipPosition", p.position.flip);
    n("offset", p.profile.offset);
    n("rotation", p.profile.rotation);
    b("clockwise", p.profile.clockwise);
    i("followAxis", p.path.followAxis + 1);
    i("upAxis", p.path.upAxis + 1);
    b("flipTangent", p.path.flipTangent);
    b("flipUp", p.path.flipUp);
    n("firstRatio", p.coupler.firstRatio);
    n("secondRatio", p.coupler.secondRatio);
    n("pitch", p.coupler.pitch);
    b("reverse", p.coupler.reverse);
    b("hingeLimits", p.hinge.limits);
    b("geometryRatio", p.coupler.useGeometryRatio);
    m_setting = false;
}
void AssemblyMateParameters::geometryRatio(double first, double second)
{
    const QSignalBlocker block(this);
    m_setting = true;
    auto* geometry = static_cast<QCheckBox*>(m_fields.at("geometryRatio"));
    const bool available = first > 1e-6 && second > 1e-6;
    geometry->setEnabled(available);
    if (!available)
        geometry->setChecked(false);
    for (auto id : {"firstRatio", "secondRatio"})
        static_cast<QDoubleSpinBox*>(m_fields.at(id))->setEnabled(!geometry->isChecked());
    if (geometry->isChecked())
    {
        static_cast<QDoubleSpinBox*>(m_fields.at("firstRatio"))->setValue(2 * first);
        static_cast<QDoubleSpinBox*>(m_fields.at("secondRatio"))->setValue(2 * second);
    }
    m_setting = false;
}
void AssemblyMateParameters::coordinateFrames(bool available)
{
    const QSignalBlocker block(this);
    auto* axes = static_cast<QCheckBox*>(m_fields.at("alignAxes"));
    axes->setEnabled(available);
    if (!available)
        axes->setChecked(false);
}
void AssemblyMateParameters::selectionGeometry(
    MateKind kind, const std::vector<std::pair<std::string, MateGeometry>>& geometry)
{
    const QSignalBlocker block(this);
    bool angular = false, centeredOnly = false, slotPair = false;
    if (kind == MateKind::Width)
    {
        std::vector<MateGeometry> width, tab;
        for (const auto& [role, g] : geometry)
            (role == "width" ? width : tab).push_back(g);
        const auto wedge = [](const auto& set)
        {
            return set.size() == 2 &&
                   !set[0].frame.Direction().IsParallel(set[1].frame.Direction(), 1e-6);
        };
        angular = wedge(width) || wedge(tab);
        if (!width.empty() && !tab.empty())
        {
            centeredOnly = (width.size() == 1 && tab.size() == 1) ||
                           (angular && (width.size() == 1 || tab.size() == 1));
            if (!angular)
            {
                const auto extent = [](const auto& set)
                {
                    return set.size() == 1
                               ? 2 * set[0].radius
                               : std::abs(gp_Vec(set[0].frame.Location(), set[1].frame.Location())
                                              .Dot(gp_Vec(set[0].frame.Direction())));
                };
                centeredOnly |= std::abs(extent(width) - extent(tab)) < 1e-6;
            }
        }
    }
    if (kind == MateKind::Slot)
        slotPair = std::any_of(
            geometry.begin(), geometry.end(), [](const auto& entry)
            { return entry.first == "follower" && entry.second.profile == MateProfileKind::Slot; });
    auto* position = static_cast<QComboBox*>(m_fields.at("position"));
    auto* model = qobject_cast<QStandardItemModel*>(position->model());
    for (int i = 0; i < position->count(); ++i)
        model->item(i)->setEnabled((!centeredOnly || i == int(PositionMode::Centered)) &&
                                   (!slotPair || i <= int(PositionMode::Centered)));
    if (!model->item(position->currentIndex())->isEnabled())
        position->setCurrentIndex(int(PositionMode::Centered));
    static_cast<QDoubleSpinBox*>(m_fields.at("distance"))->setSuffix(angular ? "°" : " mm");
    if (kind == MateKind::ProfileCenter)
    {
        const bool circular =
            std::any_of(geometry.begin(), geometry.end(), [](const auto& entry)
                        { return entry.second.profile == MateProfileKind::Circle; });
        m_fields.at("lockRotation")->setEnabled(circular);
        if (!circular)
            static_cast<QCheckBox*>(m_fields.at("lockRotation"))->setChecked(false);
    }
    else
        m_fields.at("lockRotation")->setEnabled(true);
}
} // namespace thecad::ui
