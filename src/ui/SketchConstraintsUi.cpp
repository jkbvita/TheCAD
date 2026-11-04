#include <QInputDialog>
#include "modeling/RelationCommands.h"
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/ConstraintSolver.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace thecad::ui {
namespace {
std::string constraintId(){return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();}
bool same(const modeling::SketchPointRef& a,const modeling::SketchPointRef& b){return a.entityId==b.entityId && a.role==b.role;}
const char* dimensionTitle(modeling::DimensionKind kind){switch(kind){case modeling::DimensionKind::LineLength:return "Line length";case modeling::DimensionKind::HorizontalDistance:return "Horizontal distance";case modeling::DimensionKind::VerticalDistance:return "Vertical distance";case modeling::DimensionKind::CircleRadius:return "Circle radius";case modeling::DimensionKind::CircleDiameter:return "Circle diameter";case modeling::DimensionKind::PointDistance:return "Point distance";case modeling::DimensionKind::LineDistance:return "Line spacing";default:return "Angle";}}
}
void MainWindow::acceptConstrainedSketch(const modeling::Sketch& sketch) {
    if(!m_editSketch)return;
    auto previous=editState();auto candidate=m_document;auto feature=*std::find_if(candidate.features().begin(),candidate.features().end(),[this](const auto& f){return f.id==m_editSketchId;});
    feature.sketch=sketch;candidate.replaceFeature(feature); // Solve and regenerate before accepting any edit.
    const auto& solved=*std::find_if(candidate.features().begin(),candidate.features().end(),[this](const auto& f){return f.id==m_editSketchId;});
    if(!m_viewport->setShape(candidate.shape()))throw std::runtime_error("The edited geometry could not be displayed.");
    m_editSketch=solved.sketch;rememberSketchChange(std::move(previous));
}
void MainWindow::startConstraints() {
    closeSmartDimension();
    if(!m_editSketch || m_operation || m_constraints)return;
    m_constraints=std::make_unique<ConstraintDraft>();auto& d=*m_constraints;
    d.page=new QWidget;d.page->setObjectName("constraintPage");auto* layout=new QVBoxLayout(d.page);
    auto* title=new QLabel("Sketch Relations");title->setStyleSheet("font-weight: bold");layout->addWidget(title);
    auto* help=new QLabel("Select points or lines to add a geometric relation. Double-click a dimension in the list to edit it in the viewport.");help->setWordWrap(true);layout->addWidget(help);
    d.selection=new QLabel;d.selection->setWordWrap(true);d.selection->setObjectName("constraintSelection");layout->addWidget(d.selection);
    auto* form=new QFormLayout;layout->addLayout(form);
    d.relation=new QComboBox;d.relation->setObjectName("relationKind");d.relation->addItems({"Coincident points","Horizontal points","Vertical points","Fixed point","Parallel lines","Perpendicular lines","Tangent line/circle or circles"});form->addRow("Relation",d.relation);
    auto* relation=new QPushButton("Add geometric relation");relation->setObjectName("confirmRelation");layout->addWidget(relation);connect(relation,&QPushButton::clicked,this,[this]{applyRelation();});
    d.constraints=new QListWidget;d.constraints->setObjectName("sketchConstraints");layout->addWidget(d.constraints);
    auto* remove=new QPushButton("Delete selected constraint");remove->setObjectName("deleteConstraint");layout->addWidget(remove);
    connect(remove,&QPushButton::clicked,this,[this]{if(!m_constraints || !m_constraints->constraints->currentItem())return;const auto key=m_constraints->constraints->currentItem()->data(Qt::UserRole).toString();try{auto candidate=key.startsWith("d:")?modeling::removeSketchDimension(*m_editSketch,key.mid(2).toStdString()):modeling::removeSketchRelation(*m_editSketch,key.mid(2).toStdString());acceptConstrainedSketch(candidate);m_viewport->setSketch(*m_editSketch);setWindowModified(true);closeConstraints();startConstraints();}catch(const std::exception& e){constraintError(e.what());}});
    connect(d.constraints,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){const auto key=item->data(Qt::UserRole).toString();if(key.startsWith("r:"))m_viewport->selectSketchRelation(key.mid(2).toStdString());});
    connect(d.constraints,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){const auto key=item->data(Qt::UserRole).toString();if(key.startsWith("d:")){closeConstraints();editSmartDimension(key.mid(2).toStdString());}});
    d.error=new QLabel;d.error->setWordWrap(true);d.error->setObjectName("constraintError");d.error->setStyleSheet("color: #b00020");layout->addWidget(d.error);
    auto* change=new QPushButton("Change selected relation...");change->setObjectName("changeRelation");layout->addWidget(change);
    connect(change,&QPushButton::clicked,this,[this]{
        if(!m_constraints || !m_editSketch || !m_constraints->constraints->currentItem())return;
        const auto key=m_constraints->constraints->currentItem()->data(Qt::UserRole).toString();if(!key.startsWith("r:"))return;
        const auto id=key.mid(2).toStdString();const auto found=std::find_if(m_editSketch->relations.begin(),m_editSketch->relations.end(),[&](const auto& r){return r.id==id;});if(found==m_editSketch->relations.end())return;
        auto relation=*found;if(relation.kind==modeling::RelationKind::FixedPoint || relation.kind==modeling::RelationKind::FixedEntity){constraintError("Delete Fix to release the geometry before choosing another relation.");return;}std::vector<std::string> selected{relation.first.entityId};if(!relation.second.entityId.empty() && relation.second.entityId!=relation.first.entityId)selected.push_back(relation.second.entityId);
        const bool multipleLines=selected.size()>1 && std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==selected[0];})->kind==modeling::EntityKind::Line;
        auto choices=modeling::availableRelations(*m_editSketch,selected);choices.erase(std::remove_if(choices.begin(),choices.end(),[&](auto kind){
            return kind==modeling::RelationKind::FixedEntity || (multipleLines && (kind==modeling::RelationKind::Horizontal || kind==modeling::RelationKind::Vertical)) || kind==modeling::RelationKind::PointOnLine || kind==modeling::RelationKind::PointOnCircle;
        }),choices.end());
        if(choices.empty()){constraintError("Delete this relation to release its geometry, or use an applicable relation on the current selection.");return;}
        QStringList names;for(auto kind:choices)names<<modeling::relationName(kind);bool accepted=false;
        const auto text=QInputDialog::getItem(this,"Change Sketch Relation","Relation",names,std::clamp(int(std::find(choices.begin(),choices.end(),relation.kind)-choices.begin()),0,int(choices.size())-1),false,&accepted);
        if(!accepted)return;relation.kind=choices[std::size_t(names.indexOf(text))];
        if(selected.size()==1){relation.first={selected[0],modeling::PointRole::First};relation.second={selected[0],modeling::PointRole::Second};}
        try{acceptConstrainedSketch(modeling::setSketchRelation(*m_editSketch,relation));m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateConstraintDraft();updateEntityList();}
        catch(const std::exception& e){constraintError(QString::fromUtf8(e.what()));}
    });
    auto* done=new QPushButton("Close / Cancel pending edit");done->setObjectName("cancelDimension");layout->addWidget(done);connect(done,&QPushButton::clicked,this,[this]{closeConstraints();});
    m_managerStack->addWidget(d.page);m_managerStack->setCurrentWidget(d.page);m_viewport->setConstraintPicking(true);
    if(m_entities->currentRow()>=0)constraintEntitySelected(QString::fromStdString(m_editSketch->entities.at(std::size_t(m_entities->currentRow())).id));
    if(m_constraintPoint)constraintPointSelected(*m_constraintPoint);
    updateConstraintDraft();updateActions();
}
void MainWindow::closeConstraints() {
    if(!m_constraints)return;auto* page=m_constraints->page;m_constraints.reset();m_managerStack->setCurrentIndex(0);m_managerStack->removeWidget(page);page->deleteLater();m_viewport->setConstraintPicking(false);m_viewport->setConstraintErrors({});updateActions();
}
void MainWindow::constraintEntitySelected(const QString& id) {
    if(!m_constraints || id.isEmpty() || !m_viewport->selectedSketchRelation().empty())return;auto& d=*m_constraints;
    const auto found=std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id.toStdString();});if(found==m_editSketch->entities.end())return;
    if(d.entities.empty() || d.entities.back()!=found->id){d.entities.push_back(found->id);if(d.entities.size()>2)d.entities.erase(d.entities.begin());}
    updateConstraintDraft();
}
void MainWindow::constraintPointSelected(const modeling::SketchPointRef& point) {
    if(!m_constraints)return;auto& points=m_constraints->points;
    if(points.empty() || !same(points.back(),point)){points.push_back(point);if(points.size()>2)points.erase(points.begin());}updateConstraintDraft();
}
void MainWindow::updateConstraintDraft() {
    if(!m_constraints)return;auto& d=*m_constraints;QStringList refs;for(const auto& id:d.entities)refs<<QString::fromStdString(id);
    d.selection->setText(QString("Entities: %1\nPoints: %2 / 2").arg(refs.join(", ")).arg(d.points.size()));
    const auto current=d.constraints->currentRow();const auto currentKey=d.constraints->currentItem()?d.constraints->currentItem()->data(Qt::UserRole).toString():QString{};d.constraints->clear();
    const auto add=[&](QString text,QString key){auto* item=new QListWidgetItem(text,d.constraints);item->setData(Qt::UserRole,key);};
    const auto ids=m_viewport->selectedSketchEntities();
    const auto associated=[&](const auto& r){return ids.empty() || std::find(ids.begin(),ids.end(),r.first.entityId)!=ids.end() || std::find(ids.begin(),ids.end(),r.second.entityId)!=ids.end();};
    for(const auto& dim:m_editSketch->dimensions)if(associated(dim))add(QString("%1: %2 = %3%4").arg(QString::fromStdString(dim.id),dimensionTitle(dim.kind)).arg(dim.value).arg(dim.driving?"":" (reference)"),"d:"+QString::fromStdString(dim.id));
    for(const auto& rel:m_editSketch->relations)if(associated(rel))add(QString::fromStdString(rel.id)+": "+QString::fromUtf8(modeling::relationName(rel.kind)),"r:"+QString::fromStdString(rel.id));
    d.constraints->setCurrentRow(current);
    const auto selectedKey=m_viewport->selectedSketchRelation().empty()?currentKey:QString("r:")+QString::fromStdString(m_viewport->selectedSketchRelation());
    for(int i=0;i<d.constraints->count();++i)if(d.constraints->item(i)->data(Qt::UserRole).toString()==selectedKey)d.constraints->setCurrentRow(i);
    statusBar()->showMessage(QString::fromStdString(modeling::solvePlanarSketch(*m_editSketch).diagnostics.message));
}
void MainWindow::constraintError(const QString& error) {
    if(m_constraints){m_constraints->error->setText(error);}
    std::vector<std::string> ids;
    if(m_editSketch){for(const auto& d:m_editSketch->dimensions)if(error.contains(QString::fromStdString(d.id)))ids.push_back(d.id);for(const auto& r:m_editSketch->relations)if(error.contains(QString::fromStdString(r.id)))ids.push_back(r.id);}
    m_viewport->setConstraintErrors(ids);
    if(m_constraints)for(int i=0;i<m_constraints->constraints->count();++i){auto* item=m_constraints->constraints->item(i);const auto id=item->data(Qt::UserRole).toString().mid(2);if(error.contains(id))item->setForeground(Qt::red);}
    statusBar()->showMessage(error);
}
void MainWindow::applyRelation() {
    if(!m_constraints || !m_editSketch)return;auto& d=*m_constraints;
    try {
        modeling::SketchRelation relation;relation.id=constraintId();relation.kind=modeling::RelationKind(d.relation->currentIndex());
        if(relation.kind==modeling::RelationKind::FixedPoint) {
            if(d.points.empty())throw std::runtime_error("Select the point to fix.");relation.first=d.points.back();relation.fixedPosition=modeling::sketchPointValue(*m_editSketch,relation.first);
        } else if(int(relation.kind)<=2) {
            if(d.points.size()==2){relation.first=d.points[0];relation.second=d.points[1];}
            else if(relation.kind!=modeling::RelationKind::Coincident && !d.entities.empty()){relation.first={d.entities.back(),modeling::PointRole::First};relation.second={d.entities.back(),modeling::PointRole::Second};}
            else throw std::runtime_error("Select two distinct points.");
        } else {
            if(d.entities.size()!=2)throw std::runtime_error("Select two distinct entities.");
            const auto ref=[&](const std::string& id){const auto& e=*std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id;});return modeling::SketchPointRef{id,e.kind==modeling::EntityKind::Circle?modeling::PointRole::Center:modeling::PointRole::First};};
            relation.first=ref(d.entities[0]);relation.second=ref(d.entities[1]);
        }
        acceptConstrainedSketch(modeling::setSketchRelation(*m_editSketch,relation));m_viewport->setSketch(*m_editSketch);setWindowModified(true);d.error->clear();m_viewport->setConstraintErrors({});updateConstraintDraft();updateEntityList();
    } catch(const std::exception& e){constraintError(e.what());}
}
}
