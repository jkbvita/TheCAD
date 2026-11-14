#include "ui/widgets/FeatureCommandBar.h"
#include "ui/widgets/CommandBar.h"
#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QToolButton>

namespace thecad::ui {
namespace {
class FeatureButton final:public QToolButton {
public:
    explicit FeatureButton(QWidget* parent):QToolButton(parent) {
        setProperty("featureCommand",true); setAutoRaise(true); setFixedSize(78,76);
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon); setIconSize({28,28});
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionToolButton option; initStyleOption(&option);
        option.text.clear(); option.icon={};
        QPainter painter(this); style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,this);
        const int split=popupMode()==QToolButton::MenuButtonPopup?style()->pixelMetric(QStyle::PM_MenuButtonIndicator,&option,this):0;
        const int center=(width()-split)/2;
        icon().paint(&painter,QRect(center-14,6,28,28),Qt::AlignCenter,isEnabled()?QIcon::Normal:QIcon::Disabled);
        painter.setFont(font()); painter.setPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::ButtonText));
        const auto label=defaultAction()?defaultAction()->iconText():text();
        painter.drawText(QRect(2,38,width()-split-4,34),Qt::AlignHCenter|Qt::AlignTop|Qt::TextWordWrap,label);
    }
};
QGridLayout* group(QHBoxLayout* row,const QString& name) {
    auto* widget=new QWidget; widget->setAccessibleName(name); widget->setFixedHeight(76);
    widget->setProperty("featureCommandGroup",true);
    auto* grid=new QGridLayout(widget); grid->setContentsMargins(5,0,5,0); grid->setSpacing(1);
    for(int i=0;i<3;++i)grid->setRowMinimumHeight(i,24);
    row->addWidget(widget,0,Qt::AlignTop); return grid;
}
void separator(QHBoxLayout* row) {
    auto* line=new QFrame; line->setObjectName("featureCommandSeparator");
    line->setFrameShape(QFrame::VLine); line->setFixedHeight(72);
    row->addWidget(line,0,Qt::AlignVCenter);
}
FeatureButton* large(QGridLayout* grid,QAction* action,const QString& label,int column=0) {
    action->setIconText(label);
    auto* button=new FeatureButton(grid->parentWidget()); button->setDefaultAction(action);
    button->setObjectName(action->objectName()+"Button"); button->setAccessibleName(action->text());
    grid->addWidget(button,0,column,3,1); return button;
}
void compact(QGridLayout* grid,QAction* action,int row,int column) {
    auto* button=new QToolButton; button->setDefaultAction(action); button->setAutoRaise(true);
    button->setProperty("featureCommand",true); button->setFixedHeight(24); button->setIconSize({20,20});
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    grid->addWidget(button,row,column);
}
void flyout(FeatureButton* button,const QList<QAction*>& actions) {
    auto* menu=new QMenu(button); menu->setObjectName(button->objectName()+"Menu"); menu->addActions(actions);
    button->setMenu(menu); button->setPopupMode(QToolButton::MenuButtonPopup);
    QObject::connect(menu,&QMenu::triggered,button,[button](QAction* action) {
        button->setDefaultAction(action); button->setAccessibleName(action->text());
    });
}
}
void populateFeatureCommands(QHBoxLayout* layout,const FeatureCommandActions& a)
{
    auto* scroll=new QScrollArea; scroll->setObjectName("featureCommandsScroll"); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true); scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content=new QWidget; content->setObjectName("featureCommands"); content->setFixedHeight(76);
    auto* row=new QHBoxLayout(content); row->setContentsMargins(0,0,0,0); row->setSpacing(0);
    auto* boss=group(row,"Boss/Base");
    large(boss,a.extrude,"Extruded\nBoss/Base"); large(boss,a.revolve,"Revolved\nBoss/Base",1);
    compact(boss,a.sweep,0,2); separator(row);
    auto* cut=group(row,"Cut"); large(cut,a.cut,"Extruded\nCut");
    compact(cut,a.revolvedCut,0,1); compact(cut,a.sweptCut,1,1); separator(row);
    auto* edges=group(row,"Edge treatments"); a.chamfer->setIconText("Chamfer");
    flyout(large(edges,a.fillet,"Fillet"),{a.fillet,a.chamfer}); separator(row);
    auto* pattern=group(row,"Pattern / Mirror"); a.circularPattern->setIconText("Circular\nPattern");
    flyout(large(pattern,a.linearPattern,"Linear\nPattern"),{a.linearPattern,a.circularPattern});
    compact(pattern,a.mirror,0,1); separator(row);
    auto* references=group(row,"Reference Geometry"); auto* button=new FeatureButton(references->parentWidget());
    button->setObjectName("featureReferenceGeometry"); button->setText("Reference\nGeometry");
    button->setAccessibleName("Reference Geometry"); button->setToolTip("Reference Geometry: Plane, Axis or Point");
    const auto update=[button] {button->setIcon(commandIcon("Reference Plane"));}; update();
    auto* menu=new QMenu(button); menu->setObjectName("featureReferenceMenu"); menu->addActions({a.plane,a.axis,a.point});
    button->setMenu(menu); button->setPopupMode(QToolButton::InstantPopup);
    const auto enabled=[button,a,update] {button->setEnabled(a.plane->isEnabled()||a.axis->isEnabled()||a.point->isEnabled());update();};
    for(auto* action:{a.plane,a.axis,a.point})QObject::connect(action,&QAction::changed,button,enabled);
    enabled(); references->addWidget(button,0,0,3,1);
    row->addStretch(); scroll->setWidget(content); layout->addWidget(scroll);
}
}
