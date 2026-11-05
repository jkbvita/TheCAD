#include <QPainter>
#include <QUuid>
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "ui/RelationIcons.h"
#include "modeling/RelationCommands.h"
#include "modeling/SketchTools.h"
#include "modeling/ConstraintSolver.h"
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QToolButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStatusBar>
#include <QToolTip>
#include <algorithm>
namespace thecad::ui {
void MainWindow::hideRelationsContext(){if(m_relationsToolbar)m_relationsToolbar->hide();}
void MainWindow::showRelationsContext(QPoint position){
    hideRelationsContext();if(!m_editSketch || m_operation || m_constraints || (m_smartDimension && m_smartDimension->editor))return;
    const auto ids=m_viewport->selectedSketchEntities();if(ids.empty())return;
    const auto relations=modeling::availableRelations(*m_editSketch,ids);
    const bool construction=std::all_of(ids.begin(),ids.end(),[&](const auto& id){const auto e=std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& v){return v.id==id;});return e!=m_editSketch->entities.end() && (e->kind==modeling::EntityKind::Line || e->kind==modeling::EntityKind::Circle || e->kind==modeling::EntityKind::Rectangle);});
    if(relations.empty() && !construction)return;
    auto* previous=m_relationsToolbar.data();m_relationsToolbar=nullptr;delete previous;
    // A native child stays above the OCCT native surface without creating an
    // independent tool window or changing activation when it appears.
    m_relationsToolbar=new QFrame(m_viewport);
    m_relationsToolbar->setObjectName("sketchRelationsContext");m_relationsToolbar->setAttribute(Qt::WA_NativeWindow);m_relationsToolbar->setFocusPolicy(Qt::NoFocus);m_relationsToolbar->setMouseTracking(true);
    m_relationsToolbar->setStyleSheet("QFrame#sketchRelationsContext {background:#f6f7f8;border:1px solid #9ca9b2;border-radius:3px;} QToolButton {border:0;padding:4px;background:transparent;} QToolButton:hover {background:#d5eaf9;}");
    auto* layout=new QGridLayout(m_relationsToolbar);layout->setContentsMargins(3,3,3,3);layout->setSpacing(1);
    const auto bounds=m_viewport->rect().adjusted(4,4,-4,-4);const int columns=std::max(1,std::min(10,(bounds.width()-8)/30));int index=0;
    const auto button=[&](const QString& name,const QString& object,const QIcon& icon,auto callback){auto* b=new QToolButton(m_relationsToolbar);b->setObjectName(object);b->setToolTip(name);b->setAccessibleName(name);b->setIcon(icon);b->setIconSize({20,20});b->setFixedSize(29,29);b->setFocusPolicy(Qt::NoFocus);b->setMouseTracking(true);layout->addWidget(b,index/columns,index%columns);++index;connect(b,&QToolButton::clicked,this,callback);};
    for(const auto kind:relations)button(modeling::relationName(kind),QString("contextRelation%1").arg(int(kind)),relationIcon(kind),[this,kind,ids]{hideRelationsContext();applyContextRelation(kind,ids);});
    if(construction){const bool allConstruction=std::all_of(ids.begin(),ids.end(),[&](const auto& id){return std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id;})->construction;});
        QPixmap icon(40,40);icon.fill(Qt::transparent);QPainter p(&icon);p.setPen(QPen(QColor("#276a91"),3,allConstruction?Qt::SolidLine:Qt::DashDotLine));p.drawLine(5,33,35,7);p.end();
        button(allConstruction?"Normal Geometry":"For Construction","contextConstruction",QIcon(icon),[this,ids]{hideRelationsContext();applyContextConstruction(ids);});}
    m_relationsToolbar->adjustSize();m_relationsAnchor=m_viewport->mapToGlobal(position);auto location=position+QPoint(14,14);
    location.setX(std::clamp(location.x(),bounds.left(),std::max(bounds.left(),bounds.right()-m_relationsToolbar->width()+1)));location.setY(std::clamp(location.y(),bounds.top(),std::max(bounds.top(),bounds.bottom()-m_relationsToolbar->height()+1)));
    m_relationsToolbar->move(location);m_relationsToolbar->show();m_relationsToolbar->raise();
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event){
    if(referenceGeometryEvent(watched,event))return true;
    if(operationKeyEvent(watched,event))return true;
    const auto* widget=qobject_cast<QWidget*>(watched);
    const bool ourWindow=widget && (widget==this || widget->window()==this);
    if(ourWindow && event->type()==QEvent::KeyRelease){
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Shift && !key->isAutoRepeat() && m_viewport->finishSketchMultiSelection()){key->accept();return true;}
    }
    if(event->type()==QEvent::ApplicationDeactivate || (watched==this && event->type()==QEvent::WindowDeactivate)){
        hideRelationsContext();m_viewport->cancelSketchMultiSelection();
    }
    if(m_relationsToolbar && m_relationsToolbar->isVisible()){
        if(event->type()==QEvent::MouseMove){const auto position=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();const auto globalBounds=QRect(m_relationsToolbar->mapToGlobal(QPoint(0,0)),m_relationsToolbar->size());const auto corridor=globalBounds.united(QRect(m_relationsAnchor-QPoint(12,12),QSize(24,24))).adjusted(-75,-75,75,75);if(!corridor.contains(position))hideRelationsContext();}
        else if(event->type()==QEvent::MouseButtonPress){if(!widget || (widget!=m_relationsToolbar && !m_relationsToolbar->isAncestorOf(widget)))hideRelationsContext();}
        else if(event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){hideRelationsContext();event->accept();return true;}
        else if((watched==this || watched==m_viewport) && (event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::Hide || event->type()==QEvent::Wheel))hideRelationsContext();
    }
    return QMainWindow::eventFilter(watched,event);
}
void MainWindow::applyContextRelation(modeling::RelationKind kind,const std::vector<std::string>& ids){
    if(!m_editSketch || ids!=m_viewport->selectedSketchEntities())return;
    try{const auto candidate=modeling::addSelectionRelations(*m_editSketch,kind,ids,QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());acceptConstrainedSketch(candidate);m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateEntityList();updateActions();statusBar()->showMessage(QString(modeling::relationName(kind))+" applied.",4000);}
    catch(const std::exception& e){constraintError(QString::fromUtf8(e.what()));QToolTip::showText(m_relationsAnchor,QString::fromUtf8(e.what()),m_viewport);}
    m_viewport->setFocus();
}
void MainWindow::deleteRelation(const std::string& id){
    if(!m_editSketch || m_operation || (m_smartDimension && m_smartDimension->editor))return;
    hideRelationsContext();
    try{acceptConstrainedSketch(modeling::removeSketchRelation(*m_editSketch,id));m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateEntityList();updateActions();if(m_constraints)updateConstraintDraft();statusBar()->showMessage("Relation deleted; sketch geometry retained.",3000);}
    catch(const std::exception& e){constraintError(QString::fromUtf8(e.what()));}
}
void MainWindow::applyContextConstruction(const std::vector<std::string>& ids){
    if(!m_editSketch || ids!=m_viewport->selectedSketchEntities())return;
    const auto state=modeling::constructionState(*m_editSketch,ids);
    if(state!=modeling::ConstructionState::Unavailable)setSelectionConstruction(state!=modeling::ConstructionState::Construction);
}
}
