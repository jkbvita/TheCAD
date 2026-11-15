#include "ui/widgets/FeatureCommandBar.h"
#include "ui/widgets/CommandBar.h"
#include <QAction>
#include <QFrame>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace thecad::ui
{
namespace
{
constexpr int commandHeight = 78;
constexpr int flyoutHeight = 14;
class FeatureButton final : public QToolButton
{
public:
    explicit FeatureButton(QWidget* parent, bool compact = false)
        : QToolButton(parent), m_compact(compact)
    {
        setProperty("featureCommand", true);
        setAutoRaise(true);
        setFixedSize(compact ? 62 : 78, compact ? commandHeight : 76);
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        setIconSize(compact ? QSize(20, 20) : QSize(28, 28));
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (m_compact && menu() && event->key() == Qt::Key_Down)
        {
            showMenu();
            event->accept();
            return;
        }
        QToolButton::keyPressEvent(event);
    }
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = {};
        // Compact columns have a separate full-width menu button below them.
        // Keep native click/hold/keyboard handling without a side gutter.
        if (m_compact)
        {
            option.features = QStyleOptionToolButton::None;
            option.subControls = QStyle::SC_ToolButton;
        }
        QPainter painter(this);
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
        const int split = !m_compact && popupMode() == QToolButton::MenuButtonPopup
                              ? style()->pixelMetric(QStyle::PM_MenuButtonIndicator, &option, this)
                              : 0;
        const int center = (width() - split) / 2, extent = m_compact ? 20 : 28;
        icon().paint(&painter, QRect(center - extent / 2, m_compact ? 4 : 6, extent, extent),
                     Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
        painter.setFont(font());
        painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                       QPalette::ButtonText));
        const auto label = defaultAction() ? defaultAction()->iconText() : text();
        painter.drawText(QRect(2, m_compact ? 27 : 38, width() - split - 4, 34),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, label);
    }

private:
    bool m_compact;
};
class FlyoutButton final : public QToolButton
{
public:
    explicit FlyoutButton(QWidget* parent) : QToolButton(parent)
    {
        setAutoRaise(true);
        setFixedHeight(flyoutHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setPopupMode(QToolButton::InstantPopup);
        setProperty("featureCommand", true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.features = QStyleOptionToolButton::None;
        option.subControls = QStyle::SC_ToolButton;
        QPainter painter(this);
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
        option.rect = QRect(width() / 2 - 3, height() / 2 - 2, 6, 4);
        style()->drawPrimitive(QStyle::PE_IndicatorArrowDown, &option, &painter, this);
    }
};
class Separator final : public QWidget
{
public:
    Separator()
    {
        setObjectName("featureCommandSeparator");
        setFixedSize(1, 76);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        auto color = palette().color(QPalette::WindowText);
        color.setAlpha(45);
        painter.setPen(color);
        painter.drawLine(0, 1, 0, height() - 2);
    }
};
struct Column
{
    FeatureButton* button;
    FlyoutButton* arrow = nullptr;
};
QGridLayout* group(QHBoxLayout* row, const QString& name)
{
    auto* widget = new QWidget;
    widget->setAccessibleName(name);
    widget->setFixedHeight(commandHeight);
    widget->setProperty("featureCommandGroup", true);
    auto* grid = new QGridLayout(widget);
    grid->setContentsMargins(3, 0, 3, 0);
    grid->setSpacing(0);
    for (int i = 0; i < 3; ++i)
        grid->setRowMinimumHeight(i, 26);
    row->addWidget(widget, 0, Qt::AlignTop);
    return grid;
}
void separator(QHBoxLayout* row)
{
    row->addWidget(new Separator, 0, Qt::AlignTop);
}
void fitLabel(FeatureButton* button)
{
    int required = button->property("columnMinimumWidth").toInt();
    const QFontMetrics metrics(button->font());
    const auto label =
        button->defaultAction() ? button->defaultAction()->iconText() : button->text();
    for (const auto& line : label.split('\n'))
        required = qMax(required, metrics.horizontalAdvance(line) + 8);
    button->parentWidget()->setFixedWidth(required);
    button->setFixedWidth(required);
}
Column large(QGridLayout* grid, QAction* action, const QString& label, int column, int width,
             bool dropdown = false)
{
    auto* container = new QWidget(grid->parentWidget());
    container->setFixedSize(width, commandHeight);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* button = new FeatureButton(container, true);
    button->setProperty("columnMinimumWidth", width);
    button->setFixedSize(width, commandHeight - (dropdown ? flyoutHeight : 0));
    if (action)
    {
        action->setIconText(label);
        button->setDefaultAction(action);
        button->setObjectName(action->objectName() + "Button");
        button->setAccessibleName(action->text());
    }
    else
    {
        button->setText(label);
        button->setAccessibleName(QString(label).replace('\n', ' '));
    }
    layout->addWidget(button);
    // Short command names use narrow columns; a retained flyout choice can
    // expand its column enough to keep its full label readable.
    if (action)
        QObject::connect(action, &QAction::changed, button, [button] { fitLabel(button); });
    Column result{button};
    if (dropdown)
    {
        result.arrow = new FlyoutButton(container);
        layout->addWidget(result.arrow);
    }
    grid->addWidget(container, 0, column, 3, 1);
    fitLabel(button);
    return result;
}
void compact(QGridLayout* grid, QAction* action, int row, int column)
{
    auto* button = new QToolButton;
    button->setDefaultAction(action);
    button->setAutoRaise(true);
    button->setObjectName(action->objectName() + "Button");
    button->setAccessibleName(action->text());
    button->setProperty("featureCommand", true);
    button->setFixedHeight(26);
    button->setIconSize({20, 20});
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    grid->addWidget(button, row, column, Qt::AlignLeft);
}
void flyout(Column column, const QList<QAction*>& actions, bool retain = true)
{
    auto* button = column.button;
    auto* arrow = column.arrow;
    // A disabled last-used command must not disable the other menu choices.
    auto* menu = new QMenu(button->parentWidget());
    menu->setObjectName(button->objectName() + "Menu");
    menu->addActions(actions);
    button->setMenu(menu);
    button->setPopupMode(retain ? QToolButton::DelayedPopup : QToolButton::InstantPopup);
    arrow->setObjectName(button->objectName() + "Flyout");
    arrow->setMenu(menu);
    arrow->setAccessibleName(button->accessibleName() + " options");
    arrow->setToolTip(arrow->accessibleName());
    const auto enabled = [actions, arrow]
    {
        bool any = false;
        for (auto* action : actions)
            any |= action->isEnabled();
        arrow->setEnabled(any);
    };
    for (auto* action : actions)
    {
        QObject::connect(action, &QAction::changed, arrow, enabled);
        QObject::connect(action, &QAction::changed, button, [button] { fitLabel(button); });
    }
    enabled();
    if (retain)
        QObject::connect(menu, &QMenu::triggered, button,
                         [button](QAction* action)
                         {
                             button->setDefaultAction(action);
                             button->setAccessibleName(action->text());
                             fitLabel(button);
                         });
}
QAction* future(QWidget* parent, const QString& text, const char* name)
{
    auto* action = new QAction(commandIcon(text), text, parent);
    action->setObjectName(name);
    action->setEnabled(false);
    action->setProperty("futureFeature", true);
    action->setToolTip(text + " — not implemented yet.");
    return action;
}
} // namespace
QToolButton* commandManagerButton(QAction* action, QWidget* parent)
{
    // Assembly buttons retain their established size and interaction.
    auto* button = new FeatureButton(parent);
    button->setDefaultAction(action);
    button->setAccessibleName(action->text());
    return button;
}
void populateFeatureCommands(QHBoxLayout* layout, const FeatureCommandActions& a)
{
    auto* scroll = new QScrollArea;
    scroll->setObjectName("featureCommandsScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    content->setObjectName("featureCommands");
    content->setFixedHeight(commandHeight);
    auto font = content->font();
    font.setPointSizeF(8.25);
    content->setFont(font);
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* boss = group(row, "Boss/Base");
    large(boss, a.extrude, "Extruded\nBoss/Base", 0, 62);
    large(boss, a.revolve, "Revolved\nBoss/Base", 1, 62);
    compact(boss, a.sweep, 0, 2);
    compact(boss, future(content, "Lofted Boss/Base", "actionLoftedBossBase"), 1, 2);
    compact(boss, future(content, "Boundary Boss/Base", "actionBoundaryBossBase"), 2, 2);
    separator(row);
    auto* cut = group(row, "Cut");
    large(cut, a.cut, "Extruded\nCut", 0, 52);
    const auto hole =
        large(cut, future(content, "Hole Wizard", "actionHoleWizard"), "Hole\nWizard", 1, 50, true);
    hole.arrow->setEnabled(false);
    large(cut, a.revolvedCut, "Revolved\nCut", 2, 56);
    compact(cut, a.sweptCut, 0, 3);
    compact(cut, future(content, "Lofted Cut", "actionLoftedCut"), 1, 3);
    compact(cut, future(content, "Boundary Cut", "actionBoundaryCut"), 2, 3);
    separator(row);
    auto* details = group(row, "Dress-up and Pattern");
    a.chamfer->setIconText("Chamfer");
    flyout(large(details, a.fillet, "Fillet", 0, 44, true), {a.fillet, a.chamfer});
    a.circularPattern->setIconText("Circular\nPattern");
    flyout(large(details, a.linearPattern, "Linear\nPattern", 1, 48, true),
           {a.linearPattern, a.circularPattern});
    compact(details, future(content, "Rib", "actionRib"), 0, 2);
    compact(details, future(content, "Draft", "actionDraft"), 1, 2);
    compact(details, future(content, "Shell", "actionShell"), 2, 2);
    compact(details, future(content, "Wrap", "actionWrap"), 0, 3);
    compact(details, future(content, "Intersect", "actionIntersect"), 1, 3);
    compact(details, a.mirror, 2, 3);
    separator(row);
    auto* references = group(row, "Reference Geometry and Curves");
    const auto reference = large(references, nullptr, "Reference\nGeometry", 0, 68, true);
    auto* button = reference.button;
    button->setObjectName("featureReferenceGeometry");
    button->setToolTip("Reference Geometry: Plane, Axis or Point");
    const auto update = [button] { button->setIcon(commandIcon("Reference Geometry")); };
    update();
    flyout(reference, {a.plane, a.axis, a.point}, false);
    const auto enabled = [button, a, update]
    {
        button->setEnabled(a.plane->isEnabled() || a.axis->isEnabled() || a.point->isEnabled());
        update();
    };
    for (auto* action : {a.plane, a.axis, a.point})
        QObject::connect(action, &QAction::changed, button, enabled);
    enabled();
    const auto curves =
        large(references, future(content, "Curves", "actionCurves"), "Curves", 1, 44, true);
    curves.arrow->setEnabled(false);
    row->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll);
}
} // namespace thecad::ui
