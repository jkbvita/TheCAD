#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/SketchTools.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <algorithm>
namespace thecad::ui {
void MainWindow::toggleConstruction(){
    if(!m_editSketch)return;const auto id=m_viewport->selectedSketchEntity();
    const auto found=std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id && e.kind==modeling::EntityKind::Line;});
    if(found==m_editSketch->entities.end())return;auto e=*found;e.construction=!e.construction;applySketchEntityEdit(e);updateActions();
}
void MainWindow::editPolygon(){
    if(!m_editSketch)return;const auto* group=modeling::polygonForEntity(*m_editSketch,m_viewport->selectedSketchEntity());if(!group)return;
    const auto id=group->id;const auto p=modeling::polygonParameters(*m_editSketch,*group);
    QDialog dialog(this);dialog.setObjectName("polygonEditor");dialog.setWindowTitle("Edit Regular Polygon");auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    auto* sides=new QSpinBox;sides->setObjectName("polygonEditSides");sides->setRange(3,999);sides->setValue(p.sides);form->addRow("Sides",sides);
    const auto input=[&](const char* name,const char* title,double value,double low,double high){auto* v=new QDoubleSpinBox;v->setObjectName(name);v->setDecimals(6);v->setRange(low,high);v->setValue(value);form->addRow(title,v);return v;};
    auto* x=input("polygonCenterX","Center U (mm)",p.center.x,-1e9,1e9);auto* y=input("polygonCenterY","Center V (mm)",p.center.y,-1e9,1e9);
    auto* radius=input("polygonRadius","Circumradius (mm)",p.radius,0.000001,1e9);auto* angle=input("polygonAngle","Orientation (degrees)",p.angle*180/3.14159265358979323846,-360,360);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    for(auto* v:{x,y,radius,angle}){if(!v->hasAcceptableInput()){reportError("Enter valid polygon parameters.");return;}v->interpretText();}
    try{
        const auto candidate=modeling::editRegularPolygon(*m_editSketch,id,{{x->value(),y->value()},radius->value(),angle->value()*3.14159265358979323846/180,sides->value()});
        acceptConstrainedSketch(candidate);m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateEntityList();updateActions();
    }catch(const std::exception& e){reportError(QString::fromUtf8(e.what()));}
}
}
