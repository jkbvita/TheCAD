#include "ui/Theme.h"
#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QStyleFactory>
namespace thecad::ui {
ThemeManager& ThemeManager::instance(){static auto* manager=new ThemeManager;return *manager;}
ThemeManager::ThemeManager():QObject(qApp){
    const QString saved=qEnvironmentVariable("THECAD_TEST_THEME",QSettings("TheCAD","TheCAD").value("appearance/theme","dark").toString());
    m_mode=saved=="light"?ThemeMode::Light:saved=="system"?ThemeMode::System:ThemeMode::Dark;
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    connect(QApplication::styleHints(),&QStyleHints::colorSchemeChanged,this,[this]{if(m_mode==ThemeMode::System)apply();});apply();
}
bool ThemeManager::isDark() const{return m_mode==ThemeMode::Dark||(m_mode==ThemeMode::System&&QApplication::styleHints()->colorScheme()==Qt::ColorScheme::Dark);}
void ThemeManager::setMode(ThemeMode mode,bool persist){m_mode=mode;if(persist)QSettings("TheCAD","TheCAD").setValue("appearance/theme",mode==ThemeMode::Dark?"dark":mode==ThemeMode::Light?"light":"system");apply();}
void ThemeManager::apply(){
    const bool dark=isDark();const QString bg=dark?"#252B33":"#f5f6f7",panel=dark?"#303843":"#edf0f3",input=dark?"#20262E":"#ffffff",text=dark?"#E3E8ED":"#20282f",muted=dark?"#AAB5C0":"#586b79",border=dark?"#4a5665":"#cbd3db",selection=dark?"#315c82":"#c6e3f3",hover=dark?"#3b4e63":"#d9ebf6";
    QPalette palette;palette.setColor(QPalette::Window,QColor(bg));palette.setColor(QPalette::WindowText,QColor(text));palette.setColor(QPalette::Base,QColor(input));palette.setColor(QPalette::AlternateBase,QColor(panel));palette.setColor(QPalette::Text,QColor(text));palette.setColor(QPalette::Button,QColor(panel));palette.setColor(QPalette::ButtonText,QColor(text));palette.setColor(QPalette::Highlight,QColor(selection));palette.setColor(QPalette::HighlightedText,QColor(text));palette.setColor(QPalette::ToolTipBase,QColor(panel));palette.setColor(QPalette::ToolTipText,QColor(text));palette.setColor(QPalette::PlaceholderText,QColor(muted));palette.setColor(QPalette::Link,dark?QColor("#8bbfea"):QColor("#276a91"));
    for(auto role:{QPalette::Text,QPalette::WindowText,QPalette::ButtonText})palette.setColor(QPalette::Disabled,role,QColor(dark?"#7f8b98":"#989fa5"));QApplication::setPalette(palette);
    QString css=QString(
        "QMainWindow,QDialog,QMessageBox {background:%1;} QWidget {color:%4;} "
        "QToolBar,QMenuBar,QMenu,QStatusBar,QDockWidget::title {background:%2;} QToolBar {border:0;spacing:3px;} "
        "QMenu {border:1px solid %6;} QMenu::item:selected,QMenuBar::item:selected {background:%8;} QMenu::item:disabled {color:%5;} "
        "QTabWidget::pane {border:1px solid %6;} QTabBar::tab {padding:4px 15px;background:%2;border:1px solid %6;} QTabBar::tab:selected {background:%3;} "
        "QToolButton {border:1px solid transparent;padding:3px;} QToolButton:hover,QPushButton:hover {background:%8;border-radius:3px;} QToolButton:checked {background:%7;border:1px solid %6;border-radius:3px;} QToolButton#extrusionSection {font-weight:600;} QToolButton#extrusionSection:checked:!hover {background:transparent;border-color:transparent;} "
        "QTreeWidget,QListWidget,QPlainTextEdit,QTextEdit {background:%3;border:1px solid %6;selection-background-color:%7;} QTreeWidget::item:selected,QListWidget::item:selected {background:%7;color:%4;} "
        "QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox {background:%3;border:1px solid %6;border-radius:3px;min-height:25px;padding:0 4px;selection-background-color:%7;} QComboBox QAbstractItemView {background:%3;selection-background-color:%7;} "
        "QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus {border:1px solid #559bd3;} "
        "QPushButton {background:%2;border:1px solid %6;border-radius:3px;min-height:25px;padding:2px 9px;} QPushButton:pressed {background:%7;} "
        "QGroupBox {background:%2;border:1px solid %6;border-radius:4px;margin-top:14px;padding:12px 8px 8px;} QGroupBox::title {subcontrol-origin:margin;left:9px;padding:0 4px;} "
        "QDockWidget::title {padding:5px;} QScrollArea {border:0;} QToolTip {background:%2;color:%4;border:1px solid %6;padding:5px;} "
        "QLabel#operationTitle {font-size:16px;font-weight:600;} QLabel#operationDescription,QLabel#secondaryCaption {color:%5;} QLabel#secondaryCaption {font-size:10px;} "
        "QLabel#operationStatus {background:%2;border:1px solid %6;border-radius:4px;padding:9px;} QFrame#sketchRelationsContext {background:%2;border:1px solid %6;border-radius:3px;} "
    ).arg(bg,panel,input,text,muted,border,selection,hover);
    css += QString(
        "QWidget#operationPanel[compactFeature=true] QLabel#operationTitle {font-size:13px;font-weight:600;padding:2px 0 3px 0;} "
        "QWidget#operationPanel[compactFeature=true] QComboBox,QWidget#operationPanel[compactFeature=true] QDoubleSpinBox {min-height:22px;border-radius:0;} "
        "QWidget#operationPanel[compactFeature=true] QPushButton[propertyAction=true] {min-height:0;min-width:0;padding:0;border:0;background:transparent;} "
        "QWidget#operationPanel[compactFeature=true] QPushButton[propertyAction=true]:hover {background:%1;} "
        "QWidget#operationPanel[compactFeature=true] QPushButton[propertyAction=true]:checked {background:%2;border:1px solid %3;} "
        "QComboBox[referenceSelection=true],QListWidget[referenceSelection=true] {border-radius:0;border-left:5px solid #559bd3;min-height:24px;padding-left:3px;} "
        "QComboBox[referenceSelection=true]::drop-down {width:0;border:0;} QComboBox[referenceSelection=true]::down-arrow {image:none;width:0;} "
        "QComboBox[referenceSelection=true][picking=true] {background:%2;border-color:#559bd3;} "
        "QWidget#operationPanel[compactFeature=true] QLabel#operationStatus {background:transparent;border:0;border-radius:0;padding:4px 1px;color:%4;} "
    ).arg(hover,selection,border,muted);
    css+=dark?"QLabel#operationStatus[error=true],QWidget#operationPanel[compactFeature=true] QLabel#operationStatus[error=true] {background:#492f30;color:#ffb6a5;border:1px solid #a16b60;}":"QLabel#operationStatus[error=true],QWidget#operationPanel[compactFeature=true] QLabel#operationStatus[error=true] {background:#fff0e8;color:#8c3821;border:1px solid #e5b599;}";
    css+="QWidget:disabled {color:"+QString(dark?"#7f8b98":"#989fa5")+";}";qApp->setStyleSheet(css);emit changed();
}
QColor themeColor(ThemeColor role){const bool dark=ThemeManager::instance().isDark();switch(role){
    case ThemeColor::Defined:return QColor(dark?"#E3E8ED":"#000000");
    case ThemeColor::Underdefined:return QColor(dark?"#73A5FF":"#193899");
    case ThemeColor::Construction:return QColor(dark?"#98ACD0":"#4d669e");
    case ThemeColor::Reference:return QColor(dark?"#9CB8E2":"#172e6b");
    case ThemeColor::Error:return QColor(dark?"#FF9C90":"#b00020");
    case ThemeColor::Accent:return QColor(dark?"#8BBFEA":"#276a91");
    }return {};}
Quantity_Color viewportColor(ThemeColor role){if(!ThemeManager::instance().isDark()){if(role==ThemeColor::Underdefined)return {0.10,0.22,0.60,Quantity_TOC_RGB};if(role==ThemeColor::Construction)return {0.30,0.40,0.62,Quantity_TOC_RGB};}const auto c=themeColor(role);return {c.redF(),c.greenF(),c.blueF(),Quantity_TOC_RGB};}
}
