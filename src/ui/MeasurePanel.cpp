#include "ui/MeasurePanel.h"
#include "ui/MeasureIcons.h"
#include <QApplication>
#include <QCloseEvent>
#include <QPromise>
#include <QThreadPool>
#include <BRepBuilderAPI_Copy.hxx>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QStandardItemModel>
#include <cmath>
#include <numeric>

namespace thecad::ui
{
namespace
{
const QStringList lengths{"mm", "cm", "m", "in", "ft", "µm"};
double scale(QString unit)
{
    return unit == "in"   ? 25.4
           : unit == "ft" ? 304.8
           : unit == "m"  ? 1000.
           : unit == "cm" ? 10.
           : unit == "µm" ? .001
                          : 1.;
}
QStringList& history()
{
    static QStringList readings;
    return readings;
}
QToolButton* tool(QString caption, QString help, bool toggle = false)
{
    auto* b = new QToolButton;
    b->setText(caption);
    b->setAccessibleName(help);
    b->setToolTip(help);
    b->setAutoRaise(true);
    b->setCheckable(toggle);
    return b;
}
QFormLayout* settings(QToolButton* button)
{
    auto* menu = new QMenu(button);
    auto* host = new QWidget;
    auto* form = new QFormLayout(host);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(host);
    menu->addAction(action);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    return form;
}
} // namespace
MeasureValue MeasureUnits::format(QString name, double value, int power) const
{
    const auto number = [&](double v, int digits)
    {
        if (!scientific && std::abs(v) < .5 * std::pow(10., -digits))
            v = 0.;
        return QLocale().toString(v, scientific ? 'e' : 'f', digits);
    };
    if (power < 0)
    {
        auto n = number(angle == "rad" ? value * std::acos(-1.) / 180. : value, angleDecimals);
        return {name, n + (angle == "rad" ? " rad" : "°"), n};
    }
    if (power == 0)
    {
        auto n = number(value, decimals);
        return {name, n, n};
    }
    auto one = [&](QString u)
    {
        auto n = number(value / std::pow(scale(u), power), decimals);
        if (power == 1 && u == "in" && fractionDenominator > 0 && !scientific)
        {
            const auto ticks = std::abs(value) / 25.4 * fractionDenominator;
            if (ticks < std::numeric_limits<qint64>::max() / 2.)
            {
                const auto count =
                    qint64(nearestFraction ? std::round(ticks) : std::ceil(ticks - 1e-12));
                const auto whole = count / fractionDenominator;
                const int rem = int(count % fractionDenominator),
                          divisor = std::gcd(rem, fractionDenominator);
                n = rem == 0 ? QString::number(whole)
                             : (whole ? QString::number(whole) + " " : QString{}) +
                                   QString("%1/%2")
                                       .arg(rem / divisor)
                                       .arg(fractionDenominator / divisor);
                if (value < 0 && count)
                    n.prepend('-');
            }
        }
        return MeasureValue{name, n + " " + u + (power == 2 ? "²" : ""), n};
    };
    auto result = one(length);
    if (!secondary.isEmpty() && secondary != length)
        result.display += "  [" + one(secondary).display + "]";
    return result;
}
MeasurePanel::MeasurePanel(QWidget* parent) : QDockWidget("Measure", parent)
{
    setObjectName("measurePanel");
    setAllowedAreas(Qt::LeftDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 4, 6, 5);
    layout->setSpacing(4);
    m_controlsToggle = tool("Measurement controls", "Show or hide measurement controls", true);
    m_controlsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_controlsToggle->setArrowType(Qt::RightArrow);
    layout->addWidget(m_controlsToggle, 0, Qt::AlignLeft);
    m_commands = new QWidget;
    auto* top = new QGridLayout(m_commands);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(2);
    top->setColumnStretch(0, 1);
    top->setColumnStretch(1, 1);
    layout->addWidget(m_commands);
    connect(m_controlsToggle, &QToolButton::toggled, this, [this](bool open) {
        m_controlsToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_commands->setVisible(open || m_controlsToggle->isHidden());
    });
    auto iconButton = [&](int row, int column, MeasureIcon kind, QString caption, QString help,
                          bool toggle = false)
    {
        auto* b = tool(caption, help, toggle);
        b->setIcon(measureIcon(kind));
        b->setIconSize({20, 20});
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        top->addWidget(b, row, column);
        return b;
    };
    auto* arcTool = iconButton(0, 1, MeasureIcon::Circular, "Arc distance",
                               "Choose how to measure between circular edges");
    auto* arcs = settings(arcTool);
    circle = new QComboBox;
    circle->setObjectName("measureCircleMode");
    circle->addItems(
        {"Minimum distance", "Center to center", "Maximum distance", "Custom distance"});
    arcs->addRow("Distance", circle);
    auto condition = [&](QString name)
    {
        auto* c = new QComboBox;
        c->addItems({"Center", "Minimum", "Maximum"});
        arcs->addRow(name, c);
        return c;
    };
    firstCondition = condition("First arc");
    secondCondition = condition("Second arc");
    auto* unitsTool =
        iconButton(2, 0, MeasureIcon::Units, "Units", "Set measurement units and precision");
    auto* form = settings(unitsTool);
    m_unit = new QComboBox;
    m_unit->setObjectName("measureUnits");
    m_unit->addItem("Document (mm)");
    m_unit->addItems(lengths);
    form->addRow("Length", m_unit);
    auto digits = [&](QString label)
    {
        auto* n = new QSpinBox;
        n->setRange(0, 10);
        n->setValue(3);
        form->addRow(label, n);
        return n;
    };
    m_digits = digits("Decimal places");
    m_digits->setObjectName("measurePrecision");
    m_dual = new QComboBox;
    m_dual->addItem("Off");
    m_dual->addItems(lengths);
    form->addRow("Dual units", m_dual);
    m_angle = new QComboBox;
    m_angle->addItems({"Degrees", "Radians"});
    form->addRow("Angle", m_angle);
    m_angleDigits = digits("Angle decimals");
    m_scientific = new QCheckBox("Scientific notation");
    form->addRow(m_scientific);
    m_fractions = new QComboBox;
    m_fractions->addItem("Decimal", 0);
    for (int d : {2, 4, 8, 16, 32, 64, 128, 256})
        m_fractions->addItem(QString("1/%1").arg(d), d);
    form->addRow("Inch fractions", m_fractions);
    m_roundFraction = new QCheckBox("Round to nearest fraction");
    m_roundFraction->setChecked(true);
    form->addRow(m_roundFraction);
    auto syncUnits = [this]
    {
        bool custom = m_unit->currentIndex() > 0;
        for (QWidget* w : std::initializer_list<QWidget*>{m_digits, m_dual, m_angle, m_angleDigits,
                                                          m_scientific})
            w->setEnabled(custom);
        bool inches = custom && !m_scientific->isChecked() &&
                      (m_unit->currentText() == "in" || m_dual->currentText() == "in");
        m_fractions->setEnabled(inches);
        m_roundFraction->setEnabled(inches && m_fractions->currentData().toInt() > 0);
    };
    for (auto* c : {m_unit, m_dual, m_fractions})
        connect(c, &QComboBox::currentIndexChanged, this, syncUnits);
    connect(m_scientific, &QCheckBox::toggled, this, syncUnits);
    syncUnits();
    m_xyz = new QCheckBox("Show XYZ");
    m_xyz->setObjectName("measureXYZ");
    m_xyz->setToolTip("Show XYZ measurements");
    m_xyz->setChecked(true);
    top->addWidget(m_xyz, 3, 1, Qt::AlignCenter);
    m_point = iconButton(0, 0, MeasureIcon::Points, "Point to point",
                         "Measure between two picked locations", true);
    m_point->setObjectName("measurePointMode");
    auto* rel = settings(iconButton(1, 0, MeasureIcon::Frame, "Coordinates",
                                    "Choose the coordinate frame for XYZ measurements"));
    m_frame = new QComboBox;
    m_frame->addItem("Part origin");
    rel->addRow(m_frame);
    auto* proj = settings(iconButton(1, 1, MeasureIcon::Project, "Project onto",
                                     "Project onto the screen, a planar face or a plane"));
    projection = new QComboBox;
    projection->setObjectName("measureProjection");
    projection->addItems({"None", "Screen", "Selected face/plane"});
    proj->addRow(projection);
    m_pickPlane = new QPushButton("Pick face or plane");
    proj->addRow(m_pickPlane);
    auto* showHistory =
        iconButton(3, 0, MeasureIcon::History, "History", "View or copy earlier measurements");
    auto* options = settings(iconButton(2, 1, MeasureIcon::Display, "Display",
                                        "Set callout visibility, copying and panel options"));
    m_visible = new QCheckBox("Show callouts");
    m_visible->setChecked(true);
    options->addRow(m_visible);
    m_copyMode = new QComboBox;
    m_copyMode->addItems({"Off", "Number only", "Number and units"});
    m_copyMode->setCurrentIndex(1);
    options->addRow("Quick Copy", m_copyMode);
    auto* sensor = new QPushButton("Create sensor");
    sensor->setEnabled(false);
    sensor->setToolTip("Requires persistent document sensors.");
    options->addRow(sensor);
    m_pin = new QCheckBox("Keep open on Esc");
    options->addRow(m_pin);
    m_expand = new QCheckBox("Show selection and results");
    options->addRow(m_expand);
    m_expand->setChecked(true);
    m_details = new QWidget;
    auto* body = new QVBoxLayout(m_details);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(3);
    layout->addWidget(m_details, 1);
    m_selection = new QListWidget;
    m_selection->setObjectName("measureSelection");
    m_selection->setAccessibleName("Measured entities");
    m_selection->setStyleSheet("QListWidget#measureSelection {border: 1px solid "
                               "palette(highlight); background: palette(alternate-base);}");
    m_selection->setMinimumHeight(40);
    m_selection->setMaximumHeight(64);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Selection"));
    row->addStretch();
    auto* remove = tool("Remove", "Remove the highlighted list entry");
    auto* clear = tool("Clear", "Clear all measured entities");
    row->addWidget(remove);
    row->addWidget(clear);
    auto* results = new QHBoxLayout;
    body->addLayout(results);
    results->addWidget(new QLabel("Results"));
    results->addStretch();
    auto* copy = tool("Copy", "Copy the result table");
    auto* smaller = tool("A−", "Decrease result and callout text size");
    auto* larger = tool("A+", "Increase result and callout text size");
    for (auto* button : {copy, smaller, larger})
        results->addWidget(button);
    m_values = new QTableWidget(0, 2);
    m_values->setObjectName("measureReadout");
    m_values->setAccessibleName("Measurement results");
    auto resultFont = m_values->font();
    resultFont.setPixelSize(defaultMeasureTextPixels);
    m_values->setFont(resultFont);
    m_values->setMinimumHeight(76);
    m_values->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_values->setSelectionMode(QAbstractItemView::SingleSelection);
    m_values->setShowGrid(false);
    m_values->verticalHeader()->hide();
    m_values->horizontalHeader()->hide();
    m_values->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_values->horizontalHeader()->setStretchLastSection(true);
    m_values->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_values->setMouseTracking(true);
    body->addWidget(m_values, 1);
    m_note = new QLabel;
    m_note->setWordWrap(true);
    body->addWidget(m_note);
    body->addLayout(row);
    body->addWidget(m_selection);
    auto removeCurrent = [this] { emit removePick(m_selection->currentRow()); };
    connect(remove, &QToolButton::clicked, this, removeCurrent);
    auto* del = new QShortcut(QKeySequence::Delete, m_selection);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, removeCurrent);
    connect(clear, &QToolButton::clicked, this, &MeasurePanel::clearPicks);
    connect(copy, &QToolButton::clicked, this,
            [this] { QApplication::clipboard()->setText(readingText()); });
    connect(m_values, &QTableWidget::cellEntered, this,
            [this](int r, int c)
            {
                if (m_copyMode->currentIndex() && c == 1)
                    m_values->setCurrentCell(r, c);
            });
    connect(m_values, &QTableWidget::cellClicked, this,
            [this](int r, int c)
            {
                if (!m_copyMode->currentIndex() || c != 1)
                    return;
                auto* v = m_values->item(r, c);
                QApplication::clipboard()->setText(
                    m_copyMode->currentIndex() == 1 ? v->data(Qt::UserRole).toString() : v->text());
            });
    auto fontSize = [this](int step)
    {
        auto f = m_values->font();
        f.setPixelSize(std::clamp(f.pixelSize() + step, 12, 28));
        m_values->setFont(f);
        m_values->resizeRowsToContents();
        emit optionsChanged();
    };
    connect(larger, &QToolButton::clicked, this, [=] { fontSize(1); });
    connect(smaller, &QToolButton::clicked, this, [=] { fontSize(-1); });
    connect(m_expand, &QCheckBox::toggled, m_details, &QWidget::setVisible);
    connect(m_point, &QToolButton::toggled, this, &MeasurePanel::pointModeChanged);
    connect(m_pickPlane, &QPushButton::clicked, this,
            [this]
            {
                emit projectionPick(true);
                if (auto* pop = QApplication::activePopupWidget())
                    pop->close();
            });
    connect(projection, &QComboBox::currentIndexChanged, this,
            [this](int n)
            {
                m_pickPlane->setEnabled(n == 2);
                emit projectionPick(n == 2 && !selectedPlane);
                emit optionsChanged();
            });
    m_pickPlane->setEnabled(false);
    for (auto* c :
         {circle, firstCondition, secondCondition, m_unit, m_dual, m_angle, m_frame, m_fractions})
        connect(c, &QComboBox::currentIndexChanged, this, &MeasurePanel::optionsChanged);
    for (auto* b : {m_xyz, m_visible, m_scientific, m_roundFraction})
        connect(b, &QCheckBox::toggled, this, &MeasurePanel::optionsChanged);
    for (auto* n : {m_digits, m_angleDigits})
        connect(n, &QSpinBox::valueChanged, this, &MeasurePanel::optionsChanged);
    connect(&m_areaJob, &QFutureWatcher<std::optional<double>>::finished, this,
            [this]
            {
                projectedArea = m_areaJob.result();
                m_areaPending = false;
                emit optionsChanged();
            });
    connect(showHistory, &QToolButton::clicked, this,
            [this]
            {
                auto* d = new QDialog(this, Qt::Tool);
                d->setAttribute(Qt::WA_DeleteOnClose);
                d->setWindowTitle("Measurement history");
                auto* v = new QVBoxLayout(d);
                auto* list = new QListWidget;
                list->addItems(history());
                v->addWidget(list);
                if (list->count())
                    list->setCurrentRow(list->count() - 1);
                auto* copy = new QPushButton("Copy selected measurement");
                copy->setEnabled(list->count() > 0);
                v->addWidget(copy);
                connect(copy, &QPushButton::clicked, d,
                        [list]
                        {
                            if (auto* item = list->currentItem())
                                QApplication::clipboard()->setText(item->text());
                        });
                d->resize(430, 360);
                d->show();
            });
    setWidget(scroll);
}
QSize MeasurePanel::sizeHint() const
{
    return QDockWidget::sizeHint().expandedTo(QSize(280, 480));
}
void MeasurePanel::resizeEvent(QResizeEvent* event)
{
    QDockWidget::resizeEvent(event);
    // Keep the readings in view when the dock shares a short sidebar with the tree.
    const bool compact = height() < 350;
    m_controlsToggle->setVisible(compact);
    m_commands->setVisible(!compact || m_controlsToggle->isChecked());
}
void MeasurePanel::setPicks(const std::vector<MeasurePick>& picks)
{
    QStringList names;
    for (const auto& p : picks)
        names << p.label;
    QStringList old;
    for (int i = 0; i < m_selection->count(); ++i)
        old << m_selection->item(i)->text();
    if (names == old)
        return;
    m_selection->clear();
    m_selection->addItems(names);
    m_selection->setCurrentRow(m_selection->count() - 1);
}
void MeasurePanel::setReading(const std::vector<MeasureValue>& rows, const QString& note)
{
    m_values->setRowCount(0);
    for (const auto& row : rows)
    {
        int n = m_values->rowCount();
        m_values->insertRow(n);
        m_values->setItem(n, 0, new QTableWidgetItem(row.name));
        auto* v = new QTableWidgetItem(row.display);
        v->setData(Qt::UserRole, row.number);
        v->setToolTip("Click to copy");
        m_values->setItem(n, 1, v);
    }
    m_note->setText(note);
    m_note->setVisible(!note.isEmpty());
}
QString MeasurePanel::readingText() const
{
    QStringList lines;
    for (int i = 0; i < m_values->rowCount(); ++i)
        lines << m_values->item(i, 0)->text() + "  " + m_values->item(i, 1)->text();
    return lines.join('\n');
}
void MeasurePanel::saveHistory(const QString& document)
{
    if (!m_values->rowCount())
        return;
    QStringList labels;
    for (int i = 0; i < m_selection->count(); ++i)
        labels << m_selection->item(i)->text();
    const auto entry = document + " · " + labels.join(", ") + "\n" + readingText();
    if (history().isEmpty() || history().back() != entry)
        history().append(entry);
}
MeasureUnits MeasurePanel::units() const
{
    if (!m_unit->currentIndex())
        return {};
    return {m_unit->currentText(),
            m_dual->currentIndex() ? m_dual->currentText() : QString{},
            m_angle->currentIndex() ? "rad" : "°",
            m_digits->value(),
            m_angleDigits->value(),
            m_scientific->isChecked(),
            m_fractions->currentData().toInt(),
            m_roundFraction->isChecked()};
}
bool MeasurePanel::pinned() const
{
    return m_pin->isChecked();
}
bool MeasurePanel::showXYZ() const
{
    return m_xyz->isChecked();
}
bool MeasurePanel::callouts() const
{
    return m_visible->isChecked();
}
void MeasurePanel::updateProjectedArea(const TopoDS_Shape& shape, const gp_Pln& plane)
{
    if (m_areaPending || (shape.IsSame(m_areaShape) && m_areaDirection &&
                          m_areaDirection->IsParallel(plane.Axis().Direction(), 1e-12)))
        return;
    m_areaShape = shape;
    m_areaDirection = plane.Axis().Direction();
    projectedArea.reset();
    m_areaPending = true;
    auto result = std::make_shared<QPromise<std::optional<double>>>();
    result->start();
    m_areaJob.setFuture(result->future());
    QThreadPool::globalInstance()->start(
        [shape, plane, result]
        {
            std::optional<double> area;
            try
            {
                auto copy = BRepBuilderAPI_Copy(shape, true, false).Shape();
                area = modeling::projectedMeasureArea(copy, plane);
            }
            catch (const Standard_Failure&)
            {
            }
            result->addResult(area);
            result->finish();
        });
}
void MeasurePanel::resetSession()
{
    selectedPlane.reset();
    m_pickPlane->setText("Pick face or plane");
    projection->setCurrentIndex(0);
    m_point->setChecked(false);
    if (!m_areaPending)
    {
        m_areaShape.Nullify();
        m_areaDirection.reset();
    }
    measurement.reset();
}
void MeasurePanel::closeEvent(QCloseEvent* event)
{
    QDockWidget::closeEvent(event);
    if (event->isAccepted())
        emit closed();
}
int MeasurePanel::resultTextPixels() const
{
    return m_values->font().pixelSize();
}
void MeasurePanel::setFrames(const std::vector<std::pair<QString, gp_Ax3>>& frames)
{
    QSignalBlocker block(m_frame);
    m_frames.clear();
    m_frame->clear();
    for (const auto& [name, f] : frames)
    {
        m_frame->addItem(name);
        m_frames.push_back(f);
    }
    m_frame->setEnabled(m_frames.size() > 1);
}
gp_Ax3 MeasurePanel::frame() const
{
    return m_frame->currentIndex() >= 0 && m_frame->currentIndex() < int(m_frames.size())
               ? m_frames[m_frame->currentIndex()]
               : gp_Ax3();
}
void MeasurePanel::setProjectionPlane(gp_Pln plane, const QString& label)
{
    selectedPlane = plane;
    m_pickPlane->setText(label + " · Replace");
    emit optionsChanged();
}
void MeasurePanel::setArcAvailability(bool center, bool extremes, bool custom)
{
    QSignalBlocker block(circle);
    auto* model = qobject_cast<QStandardItemModel*>(circle->model());
    model->item(1)->setEnabled(center);
    model->item(2)->setEnabled(extremes);
    model->item(3)->setEnabled(custom);
    circle->setEnabled(center);
    if (!model->item(circle->currentIndex())->isEnabled())
        circle->setCurrentIndex(0);
    firstCondition->setEnabled(circle->currentIndex() == 3);
    secondCondition->setEnabled(circle->currentIndex() == 3);
}
} // namespace thecad::ui
