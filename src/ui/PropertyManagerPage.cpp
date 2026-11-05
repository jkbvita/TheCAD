#include "ui/PropertyManagerPage.h"
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
    auto* heading=new QLabel(title);heading->setObjectName("operationTitle");layout->addWidget(heading);
    auto* hint=new QLabel(description);hint->setObjectName("operationDescription");hint->setWordWrap(true);layout->addWidget(hint);
    buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->setObjectName("operationButtons");
    buttons->button(QDialogButtonBox::Ok)->setText("Confirm");buttons->button(QDialogButtonBox::Ok)->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    buttons->button(QDialogButtonBox::Cancel)->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));layout->addWidget(buttons);
    auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);layout->addWidget(scroll,1);
    auto* content=new QWidget;scroll->setWidget(content);settings=new QVBoxLayout(content);settings->setContentsMargins(0,0,0,0);settings->setSpacing(9);
    status=new QLabel;status->setObjectName("operationStatus");status->setWordWrap(true);layout->addWidget(status);
    auto* shortcuts=new QLabel("Enter: confirm   ·   Esc: cancel");shortcuts->setObjectName("operationDescription");layout->addWidget(shortcuts);
}
void PropertyManagerPage::setStatus(const QString& message,bool error){status->setText(message);status->setProperty("error",error);status->style()->unpolish(status);status->style()->polish(status);}
}
