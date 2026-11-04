#include "modeling/DimensionAnnotation.h"
#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/DimensionInference.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
namespace thecad::ui {
namespace {
QString dimensionName(modeling::DimensionKind kind) {
    switch(kind){case modeling::DimensionKind::HorizontalDistance:return "Horizontal distance";case modeling::DimensionKind::VerticalDistance:return "Vertical distance";
    case modeling::DimensionKind::CircleRadius:return "Radius";case modeling::DimensionKind::CircleDiameter:return "Diameter";
    case modeling::DimensionKind::LineDistance:return "Line spacing";case modeling::DimensionKind::LineAngle:return "Angle";default:return "Length";}
}
}
void MainWindow::startSmartDimension() {
    if(m_smartDimension){closeSmartDimension();return;}
    if(!m_editSketch || m_operation)return;
    closeConstraints();
    const auto point=m_viewport->selectedSketchPoint();const auto entity=m_viewport->selectedSketchEntity();
    m_smartDimension=std::make_unique<SmartDimensionDraft>();
    if(point)m_smartDimension->points.push_back(*point);else if(!entity.empty()){
        const auto selected=std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==entity;});
        if(selected!=m_editSketch->entities.end() && selected->kind==modeling::EntityKind::Point)m_smartDimension->points.push_back({entity,modeling::PointRole::First});else m_smartDimension->entities.push_back(entity);
    }
    m_viewport->setSmartDimensionPicking(true);m_viewport->setDimensionSelection(m_smartDimension->entities,m_smartDimension->points);
    auto* action=findChild<QAction*>("actionSmartDimension");action->setCheckable(true);action->setChecked(true);
    statusBar()->showMessage("Smart Dimension: select one line/circle, two lines or two points; move and click to place. Esc exits.");updateActions();
}
void MainWindow::closeSmartDimension() {
    if(!m_smartDimension)return;
    auto* editor=m_smartDimension->editor;m_smartDimension.reset();
    if(editor){editor->disconnect(this);editor->close();editor->deleteLater();}
    m_viewport->setDimensionPreview({});m_viewport->setDimensionSelection({},{});m_viewport->setSmartDimensionPicking(false);m_viewport->setConstraintErrors({});
    findChild<QAction*>("actionSmartDimension")->setChecked(false);updateActions();
}
void MainWindow::resetSmartSelection() {
    if(!m_smartDimension)return;m_smartDimension->points.clear();m_smartDimension->entities.clear();m_smartDimension->pending.reset();m_smartDimension->radius=false;m_smartDimension->editingExisting=false;
    m_viewport->setDimensionPreview({});m_viewport->setDimensionSelection({},{});m_viewport->setConstraintErrors({});
    statusBar()->showMessage("Smart Dimension: select the next geometry, then click to place. Esc exits.");
}
void MainWindow::smartReferencePicked(const QString& id,int role) {
    if(!m_smartDimension || m_smartDimension->editor)return;auto& draft=*m_smartDimension;
    if(role>=0) {
        draft.entities.clear();const modeling::SketchPointRef point{id.toStdString(),modeling::PointRole(role)};
        const auto same=[&](const auto& p){return p.entityId==point.entityId && p.role==point.role;};
        if(draft.points.size()==2)draft.points.clear();if(std::none_of(draft.points.begin(),draft.points.end(),same))draft.points.push_back(point);
    } else {
        draft.points.clear();const auto entity=id.toStdString();if(draft.entities.size()==2)draft.entities.clear();
        if(std::find(draft.entities.begin(),draft.entities.end(),entity)==draft.entities.end())draft.entities.push_back(entity);
    }
    draft.pending.reset();m_viewport->setDimensionPreview({});m_viewport->setDimensionSelection(draft.entities,draft.points);
    statusBar()->showMessage(QString("Smart Dimension: %1 selected. Move to preview, click empty space to place; select another reference if needed. Circle: R radius / D diameter.").arg(draft.points.empty()?draft.entities.size():draft.points.size()));
}
void MainWindow::updateSmartPreview(modeling::Point2 point) {
    if(!m_smartDimension || m_smartDimension->editor || !m_editSketch)return;
    try {
        auto& draft=*m_smartDimension;draft.pending=modeling::inferDimension(*m_editSketch,draft.points,draft.entities,point,draft.radius);
        m_viewport->setDimensionPreview(draft.pending);statusBar()->showMessage(dimensionName(draft.pending->kind)+": click to place and enter a value. Esc exits.");
    }catch(const std::exception& error){m_smartDimension->pending.reset();m_viewport->setDimensionPreview({});statusBar()->showMessage(QString::fromUtf8(error.what()));}
}
void MainWindow::placeSmartDimension(modeling::Point2 point) {
    if(!m_smartDimension || m_smartDimension->editor)return;updateSmartPreview(point);
    if(m_smartDimension->pending)openDimensionEditor(*m_smartDimension->pending);
}
void MainWindow::editSmartDimension(const std::string& id) {
    if(!m_editSketch)return;const bool resume=bool(m_smartDimension);
    closeConstraints();closeSmartDimension();
    const auto found=std::find_if(m_editSketch->dimensions.begin(),m_editSketch->dimensions.end(),[&](const auto& d){return d.id==id;});if(found==m_editSketch->dimensions.end())return;
    m_smartDimension=std::make_unique<SmartDimensionDraft>();m_smartDimension->resume=resume;m_smartDimension->editingExisting=true;m_smartDimension->pending=*found;
    m_viewport->setSmartDimensionPicking(true);findChild<QAction*>("actionSmartDimension")->setChecked(true);openDimensionEditor(*found);
}
void MainWindow::openDimensionEditor(const modeling::SketchDimension& dimension) {
    if(!m_smartDimension || m_smartDimension->editor)return;
    auto* editor=new QDialog(this,Qt::Tool | Qt::FramelessWindowHint);editor->setObjectName("smartDimensionEditor");editor->setWindowTitle("Modify Dimension");editor->setModal(false);
    m_smartDimension->editor=editor;auto* layout=new QVBoxLayout(editor);layout->setContentsMargins(10,8,10,8);
    auto* title=new QLabel("Modify — "+dimensionName(dimension.kind));layout->addWidget(title);
    auto* value=new QDoubleSpinBox;value->setObjectName("smartDimensionValue");value->setDecimals(6);value->setKeyboardTracking(false);
    const bool angular=dimension.kind==modeling::DimensionKind::LineAngle;value->setRange(angular?0.000001:0.0,angular?179.999999:1e9);value->setSuffix(angular?QString::fromUtf8(" °"):" mm");value->setValue(dimension.value);value->setEnabled(dimension.driving);layout->addWidget(value);
    auto* reference=new QCheckBox("Reference");reference->setObjectName("smartDimensionReference");reference->setChecked(!dimension.driving);layout->addWidget(reference);
    connect(reference,&QCheckBox::toggled,value,[value](bool checked){value->setEnabled(!checked);});
    auto* error=new QLabel;error->setObjectName("smartDimensionError");error->setWordWrap(true);error->setMaximumWidth(300);error->setStyleSheet("color: #b00020");layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);buttons->setObjectName("smartDimensionButtons");layout->addWidget(buttons);buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons,&QDialogButtonBox::rejected,editor,&QDialog::reject);
    connect(editor,&QDialog::rejected,this,[this,editor]{if(!m_smartDimension || m_smartDimension->editor!=editor)return;const bool resume=m_smartDimension->resume;m_smartDimension->editor=nullptr;editor->deleteLater();if(resume)resetSmartSelection();else closeSmartDimension();updateHistoryActions();m_viewport->setFocus();});
    connect(buttons,&QDialogButtonBox::accepted,this,[this,editor,value,reference,error,dimension]{
        if(!m_smartDimension || m_smartDimension->editor!=editor || !m_editSketch)return;
        try {
            if(!value->hasAcceptableInput())throw std::runtime_error("Enter a valid dimension value.");value->interpretText();auto candidate=dimension;candidate.value=value->value();candidate.driving=!reference->isChecked();
            if(!candidate.driving)candidate.value=modeling::dimensionMeasurement(*m_editSketch,candidate);
            if(!m_smartDimension->editingExisting)candidate.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            acceptConstrainedSketch(modeling::setSketchDimension(*m_editSketch,candidate));m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateEntityList();
            const bool resume=m_smartDimension->resume;m_smartDimension->editor=nullptr;editor->accept();editor->deleteLater();if(resume)resetSmartSelection();else closeSmartDimension();updateHistoryActions();m_viewport->setFocus();
        } catch(const std::exception& failure){const auto message=QString::fromUtf8(failure.what());error->setText(message);value->setStyleSheet("color: #b00020");constraintError(message);value->setFocus();value->selectAll();editor->adjustSize();}
    });
    editor->adjustSize();const auto anchor=m_viewport->mapToGlobal(m_viewport->dimensionScreenPoint(modeling::dimensionAnnotationPosition(*m_editSketch,dimension)));auto location=anchor+QPoint(10,10);
    const auto* screen=QGuiApplication::screenAt(anchor);if(!screen)screen=this->screen();const auto bounds=screen->availableGeometry();
    location.setX(std::clamp(location.x(),bounds.left(),std::max(bounds.left(),bounds.right()-editor->width())));location.setY(std::clamp(location.y(),bounds.top(),std::max(bounds.top(),bounds.bottom()-editor->height())));
    updateHistoryActions();editor->move(location);editor->show();editor->raise();editor->activateWindow();value->setFocus();value->selectAll();
}
}
