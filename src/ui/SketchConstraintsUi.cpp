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
const char* dimensionTitle(modeling::DimensionKind kind){switch(kind){case modeling::DimensionKind::LineLength:return "Line length";case modeling::DimensionKind::HorizontalDistance:return "Horizontal distance";case modeling::DimensionKind::VerticalDistance:return "Vertical distance";case modeling::DimensionKind::CircleRadius:return "Circle radius";default:return "Circle diameter";}}
}
void MainWindow::acceptConstrainedSketch(const modeling::Sketch& sketch) {
    if(!m_editSketch)return;
    auto candidate=m_document;auto feature=*std::find_if(candidate.features().begin(),candidate.features().end(),[this](const auto& f){return f.id==m_editSketchId;});
    feature.sketch=sketch;candidate.replaceFeature(feature); // Solve and regenerate before accepting any edit.
    const auto& solved=*std::find_if(candidate.features().begin(),candidate.features().end(),[this](const auto& f){return f.id==m_editSketchId;});
    m_editSketch=solved.sketch;m_viewport->setShape(candidate.shape());
}
void MainWindow::startConstraints(const std::string& dimensionId) {
    if(!m_editSketch || m_operation || m_constraints)return;
    m_constraints=std::make_unique<ConstraintDraft>();auto& d=*m_constraints;
    d.page=new QWidget;d.page->setObjectName("constraintPage");auto* layout=new QVBoxLayout(d.page);
    auto* title=new QLabel("Smart Dimension / Relations");title->setStyleSheet("font-weight: bold");layout->addWidget(title);
    auto* help=new QLabel("Select a line or circle, or two points for a horizontal/vertical distance. Click empty space to place the dimension, enter mm and confirm. Escape cancels.");help->setWordWrap(true);layout->addWidget(help);
    d.selection=new QLabel;d.selection->setWordWrap(true);d.selection->setObjectName("constraintSelection");layout->addWidget(d.selection);
    auto* form=new QFormLayout;layout->addLayout(form);d.kind=new QComboBox;d.kind->setObjectName("dimensionKind");
    for(int i=0;i<5;++i)d.kind->addItem(dimensionTitle(modeling::DimensionKind(i)),i);form->addRow("Dimension",d.kind);
    d.value=new QDoubleSpinBox;d.value->setObjectName("dimensionValue");d.value->setDecimals(6);d.value->setRange(0,1e9);d.value->setSuffix(" mm");d.value->setValue(10);form->addRow("Value",d.value);
    d.reference=new QCheckBox("Reference (measurement only)");d.reference->setObjectName("dimensionReference");layout->addWidget(d.reference);
    auto* apply=new QPushButton("Confirm dimension");apply->setObjectName("confirmDimension");layout->addWidget(apply);connect(apply,&QPushButton::clicked,this,[this]{applyDimension();});
    d.relation=new QComboBox;d.relation->setObjectName("relationKind");d.relation->addItems({"Coincident points","Horizontal points","Vertical points","Fixed point","Parallel lines","Perpendicular lines","Tangent line/circle or circles"});form->addRow("Relation",d.relation);
    auto* relation=new QPushButton("Add geometric relation");relation->setObjectName("confirmRelation");layout->addWidget(relation);connect(relation,&QPushButton::clicked,this,[this]{applyRelation();});
    d.constraints=new QListWidget;d.constraints->setObjectName("sketchConstraints");layout->addWidget(d.constraints);
    auto* remove=new QPushButton("Delete selected constraint");remove->setObjectName("deleteConstraint");layout->addWidget(remove);
    connect(remove,&QPushButton::clicked,this,[this]{if(!m_constraints || !m_constraints->constraints->currentItem())return;const auto key=m_constraints->constraints->currentItem()->data(Qt::UserRole).toString();try{auto candidate=key.startsWith("d:")?modeling::removeSketchDimension(*m_editSketch,key.mid(2).toStdString()):modeling::removeSketchRelation(*m_editSketch,key.mid(2).toStdString());acceptConstrainedSketch(candidate);m_viewport->setSketch(*m_editSketch);setWindowModified(true);closeConstraints();startConstraints();}catch(const std::exception& e){constraintError(e.what());}});
    connect(d.constraints,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){const auto key=item->data(Qt::UserRole).toString();if(key.startsWith("d:")){closeConstraints();startConstraints(key.mid(2).toStdString());}});
    d.error=new QLabel;d.error->setWordWrap(true);d.error->setObjectName("constraintError");d.error->setStyleSheet("color: #b00020");layout->addWidget(d.error);
    auto* done=new QPushButton("Close / Cancel pending edit");done->setObjectName("cancelDimension");layout->addWidget(done);connect(done,&QPushButton::clicked,this,[this]{closeConstraints();});
    connect(d.kind,&QComboBox::currentIndexChanged,this,[this]{updateConstraintDraft();});
    connect(d.reference,&QCheckBox::toggled,this,[this](bool reference){if(m_constraints)m_constraints->value->setEnabled(!reference);});
    m_managerStack->addWidget(d.page);m_managerStack->setCurrentWidget(d.page);m_viewport->setConstraintPicking(true);
    if(!dimensionId.empty()) {
        const auto found=std::find_if(m_editSketch->dimensions.begin(),m_editSketch->dimensions.end(),[&](const auto& dim){return dim.id==dimensionId;});
        if(found==m_editSketch->dimensions.end()){closeConstraints();return;}
        d.editingId=found->id;d.kind->setCurrentIndex(int(found->kind));d.points={found->first,found->second};d.entities={found->first.entityId};d.annotation=found->annotation;d.value->setValue(found->value);d.reference->setChecked(!found->driving);
    } else {
        if(m_entities->currentRow()>=0)constraintEntitySelected(QString::fromStdString(m_editSketch->entities.at(std::size_t(m_entities->currentRow())).id));
        if(m_constraintPoint)constraintPointSelected(*m_constraintPoint);
    }
    updateConstraintDraft();updateActions();
}
void MainWindow::closeConstraints() {
    if(!m_constraints)return;auto* page=m_constraints->page;m_constraints.reset();m_managerStack->setCurrentIndex(0);m_managerStack->removeWidget(page);page->deleteLater();m_viewport->setConstraintPicking(false);m_viewport->setConstraintErrors({});updateActions();
}
void MainWindow::constraintEntitySelected(const QString& id) {
    if(!m_constraints || id.isEmpty() || !m_constraints->editingId.empty())return;auto& d=*m_constraints;
    const auto found=std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id.toStdString();});if(found==m_editSketch->entities.end())return;
    if(d.entities.empty() || d.entities.back()!=found->id){d.entities.push_back(found->id);if(d.entities.size()>2)d.entities.erase(d.entities.begin());}
    if(d.kind->currentIndex()==0 || d.kind->currentIndex()>=3) {
        if(found->kind==modeling::EntityKind::Circle){d.kind->setCurrentIndex(4);d.value->setValue(found->radius*2);}
        else if(found->kind==modeling::EntityKind::Line){d.kind->setCurrentIndex(0);d.value->setValue(std::hypot(found->second.x-found->first.x,found->second.y-found->first.y));}
    }
    updateConstraintDraft();
}
void MainWindow::constraintPointSelected(const modeling::SketchPointRef& point) {
    if(!m_constraints || !m_constraints->editingId.empty())return;auto& points=m_constraints->points;
    if(points.empty() || !same(points.back(),point)){points.push_back(point);if(points.size()>2)points.erase(points.begin());}updateConstraintDraft();
}
void MainWindow::updateConstraintDraft() {
    if(!m_constraints)return;auto& d=*m_constraints;QStringList refs;for(const auto& id:d.entities)refs<<QString::fromStdString(id);
    d.selection->setText(QString("Entities: %1\nPoints: %2 / 2\nAnnotation: %3%4").arg(refs.join(", ")).arg(d.points.size()).arg(d.annotation?"placed":"click empty viewport").arg(d.editingId.empty()?"":"\nEditing "+QString::fromStdString(d.editingId)));
    const auto current=d.constraints->currentRow();d.constraints->clear();
    const auto add=[&](QString text,QString key){auto* item=new QListWidgetItem(text,d.constraints);item->setData(Qt::UserRole,key);};
    for(const auto& dim:m_editSketch->dimensions)add(QString("%1: %2 = %3 mm%4").arg(QString::fromStdString(dim.id),dimensionTitle(dim.kind)).arg(dim.value).arg(dim.driving?"":" (reference)"),"d:"+QString::fromStdString(dim.id));
    const QStringList names{"Coincident","Horizontal","Vertical","Fixed","Parallel","Perpendicular","Tangent"};for(const auto& rel:m_editSketch->relations)add(QString::fromStdString(rel.id)+": "+names.at(int(rel.kind)),"r:"+QString::fromStdString(rel.id));
    d.constraints->setCurrentRow(current);statusBar()->showMessage(QString::fromStdString(modeling::solvePlanarSketch(*m_editSketch).diagnostics.message));
}
void MainWindow::constraintError(const QString& error) {
    if(m_constraints){m_constraints->error->setText(error);m_constraints->value->setStyleSheet("color: #b00020");}
    std::vector<std::string> ids;
    if(m_editSketch){for(const auto& d:m_editSketch->dimensions)if(error.contains(QString::fromStdString(d.id)))ids.push_back(d.id);for(const auto& r:m_editSketch->relations)if(error.contains(QString::fromStdString(r.id)))ids.push_back(r.id);}
    m_viewport->setConstraintErrors(ids);
    if(m_constraints)for(int i=0;i<m_constraints->constraints->count();++i){auto* item=m_constraints->constraints->item(i);const auto id=item->data(Qt::UserRole).toString().mid(2);if(error.contains(id))item->setForeground(Qt::red);}
    statusBar()->showMessage(error);
}
void MainWindow::applyDimension() {
    if(!m_constraints || !m_editSketch)return;auto& draft=*m_constraints;
    try {
        if(!draft.reference->isChecked() && !draft.value->hasAcceptableInput())throw std::runtime_error("Enter a valid dimension in millimeters.");
        modeling::SketchDimension dim;dim.id=draft.editingId.empty()?constraintId():draft.editingId;dim.kind=modeling::DimensionKind(draft.kind->currentIndex());dim.value=draft.value->value();dim.driving=!draft.reference->isChecked();
        if(!draft.annotation)throw std::runtime_error("Click empty viewport space to place the dimension annotation.");dim.annotation=*draft.annotation;
        if(dim.kind==modeling::DimensionKind::HorizontalDistance || dim.kind==modeling::DimensionKind::VerticalDistance) {
            if(draft.points.size()!=2)throw std::runtime_error("Select two distinct sketch points (the origin can be one).");dim.first=draft.points[0];dim.second=draft.points[1];
            const auto a=modeling::sketchPointValue(*m_editSketch,dim.first),b=modeling::sketchPointValue(*m_editSketch,dim.second);const double delta=dim.kind==modeling::DimensionKind::HorizontalDistance?b.x-a.x:b.y-a.y;dim.sense=delta<0?-1:1;
            if(!draft.editingId.empty()){const auto old=std::find_if(m_editSketch->dimensions.begin(),m_editSketch->dimensions.end(),[&](const auto& d){return d.id==draft.editingId;});dim.sense=old->sense;}
        } else {
            if(draft.entities.empty())throw std::runtime_error("Select a line or circle.");const auto id=draft.entities.back();const auto& e=*std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id;});
            if(dim.kind==modeling::DimensionKind::LineLength){if(e.kind!=modeling::EntityKind::Line)throw std::runtime_error("A length dimension requires a line.");dim.first={id,modeling::PointRole::First};dim.second={id,modeling::PointRole::Second};}
            else {if(e.kind!=modeling::EntityKind::Circle)throw std::runtime_error("Radius/diameter requires a circle.");dim.first={id,modeling::PointRole::Center};}
        }
        if(!dim.driving)dim.value=modeling::dimensionMeasurement(*m_editSketch,dim);
        acceptConstrainedSketch(modeling::setSketchDimension(*m_editSketch,dim));m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateEntityList();closeConstraints();
    } catch(const std::exception& e){constraintError(e.what());}
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
        acceptConstrainedSketch(modeling::setSketchRelation(*m_editSketch,relation));m_viewport->setSketch(*m_editSketch);setWindowModified(true);d.error->clear();d.value->setStyleSheet({});m_viewport->setConstraintErrors({});updateConstraintDraft();updateEntityList();
    } catch(const std::exception& e){constraintError(e.what());}
}
}
