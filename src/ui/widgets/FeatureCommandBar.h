#pragma once
class QAction;
class QHBoxLayout;
class QWidget;
class QToolButton;
namespace thecad::ui {
struct FeatureCommandActions {
    QAction *extrude,*revolve,*sweep,*cut,*revolvedCut,*sweptCut;
    QAction *fillet,*chamfer,*linearPattern,*circularPattern,*mirror;
    QAction *plane,*axis,*point;
};
void populateFeatureCommands(QHBoxLayout* layout,const FeatureCommandActions& actions);
QToolButton* commandManagerButton(QAction* action,QWidget* parent=nullptr);
}
