#include "ui/PropertyManagerPage.h"
#include "ui/widgets/PropertyManagerControls.h"
#include "ui/widgets/CommandBar.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
namespace thecad::ui {
PropertyManagerPage::PropertyManagerPage(const QString& title,const QString& description,QWidget* parent):QWidget(parent){
    setObjectName("operationPanel");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(12,12,12,12);layout->setSpacing(9);
    m_heading=new QLabel(title);m_heading->setObjectName("operationTitle");layout->addWidget(m_heading);
    m_description=new QLabel(description);m_description->setObjectName("operationDescription");m_description->setWordWrap(true);layout->addWidget(m_description);
    buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->setObjectName("operationButtons");
    buttons->button(QDialogButtonBox::Ok)->setText("Confirm");buttons->button(QDialogButtonBox::Ok)->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    buttons->button(QDialogButtonBox::Cancel)->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));layout->addWidget(buttons);
    auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);layout->addWidget(scroll,1);
    auto* content=new QWidget;scroll->setWidget(content);settings=new QVBoxLayout(content);settings->setContentsMargins(0,0,0,0);settings->setSpacing(9);
    status=new QLabel;status->setObjectName("operationStatus");status->setWordWrap(true);layout->addWidget(status);
    m_shortcuts=new QLabel("Enter: confirm   ·   Esc: cancel");m_shortcuts->setObjectName("operationDescription");layout->addWidget(m_shortcuts);
}
void PropertyManagerPage::useCompactFeatureHeader(QPushButton* preview)
{
    setProperty("compactFeature", true);
    layout()->setContentsMargins(7, 7, 7, 7); layout()->setSpacing(4);
    settings->setSpacing(2);
    m_description->hide(); m_shortcuts->hide();
    status->hide();
    m_heading->setProperty("compactFeature", true);
    auto* titleRow = new QWidget(this);
    auto* titleLayout = new QHBoxLayout(titleRow); titleLayout->setContentsMargins(0,0,0,0); titleLayout->setSpacing(5);
    auto* featureIcon = new QLabel;
    featureIcon->setPixmap(commandIcon(m_heading->text().contains("Cut") ? "Extruded Cut" : "Extrude").pixmap(20,20));
    featureIcon->setFixedWidth(22);
    layout()->removeWidget(m_heading); titleLayout->addWidget(featureIcon); titleLayout->addWidget(m_heading,1);
    static_cast<QVBoxLayout*>(layout())->insertWidget(0,titleRow);
    buttons->setProperty("compactFeature", true);
    const auto setup = [](QPushButton* button, const QString& name, PropertySymbol symbol, const QColor& color) {
        button->setAccessibleName(name); button->setToolTip(name); button->setText({});
        button->setIcon(propertyIcon(symbol, color)); button->setIconSize({18,18});
        button->setFlat(true); button->setFixedSize(25,25); button->setAutoDefault(false);
        button->setProperty("propertyAction", true);
    };
    auto* confirm=buttons->button(QDialogButtonBox::Ok);
    auto* cancel=buttons->button(QDialogButtonBox::Cancel);
    setup(confirm,"Confirm (Enter)",PropertySymbol::Confirm,QColor("#44aa62"));
    setup(cancel,"Cancel (Esc)",PropertySymbol::Cancel,QColor("#df6258"));
    setup(preview,"Show preview",PropertySymbol::Preview,palette().color(QPalette::WindowText));
    if (auto* row=qobject_cast<QBoxLayout*>(buttons->layout())) {
        while (auto* item=row->takeAt(0)) delete item;
        row->addWidget(confirm); row->addWidget(cancel); row->addWidget(preview); row->addStretch();
        row->setContentsMargins(0,0,0,2); row->setSpacing(2);
    }
}
void PropertyManagerPage::setStatus(const QString& message,bool error){status->setText(message);status->setVisible(!message.isEmpty());status->setProperty("error",error);status->style()->unpolish(status);status->style()->polish(status);}
}
