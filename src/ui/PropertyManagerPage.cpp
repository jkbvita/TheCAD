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
    setStyleSheet("QWidget#operationPanel {background:#f6f7f9;} "
        "QLabel#operationTitle {font-size:16px;font-weight:600;color:#203d53;} "
        "QLabel#operationDescription {color:#586b79;} "
        "QGroupBox {background:white;border:1px solid #d5dfe7;border-radius:4px;margin-top:14px;padding:12px 8px 8px;} "
        "QGroupBox::title {subcontrol-origin:margin;left:9px;padding:0 4px;color:#294b64;} "
        "QDoubleSpinBox,QComboBox {min-height:27px;background:white;border:1px solid #b8c7d2;border-radius:3px;padding:0 4px;} "
        "QDoubleSpinBox:focus,QComboBox:focus {border:2px solid #3d8bbb;} "
        "QListWidget {border:1px solid #b8c7d2;border-radius:3px;} "
        "QListWidget::item {padding:5px;} QListWidget::item:selected {background:#c7e5fa;color:#17415d;} "
        "QLabel#operationStatus {background:#eaf1f6;border:1px solid #cfdee9;border-radius:4px;padding:9px;color:#294b64;} "
        "QLabel#operationStatus[error=true] {background:#fff0e8;border-color:#e5b599;color:#8c3821;} "
        "QPushButton {min-height:25px;padding:2px 9px;} ");
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
