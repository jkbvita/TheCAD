#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/SketchTools.h"
#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace thecad::ui {
void MainWindow::initializeSelectionUi(QMenu* viewMenu){
    auto* filters=new QToolBar("Selection Filters",this);filters->setObjectName("selectionFilterToolbar");filters->setMovable(false);filters->setFloatable(false);addToolBar(Qt::BottomToolBarArea,filters);
    auto* show=filters->toggleViewAction();show->setObjectName("actionSelectionFilterToolbar");show->setShortcut(QKeySequence(Qt::Key_F5));viewMenu->addAction(show);
    auto* enabled=filters->addAction("Enable filters (F6)");enabled->setObjectName("actionToggleSelectionFilters");enabled->setCheckable(true);enabled->setChecked(true);enabled->setShortcut(QKeySequence(Qt::Key_F6));addAction(enabled);
    std::vector<std::pair<QAction*,std::uint32_t>> actions;
    for(const auto& item:std::vector<std::pair<const char*,std::uint32_t>>{{"Faces",modeling::FilterFaces},{"Edges",modeling::FilterEdges},{"Vertices",modeling::FilterVertices},{"Sketch geometry",modeling::FilterSketch},{"Reference geometry",modeling::FilterReferences},{"Solid bodies",modeling::FilterBodies},{"Dimensions / relations",modeling::FilterAnnotations}}){
        auto* action=filters->addAction(item.first);action->setObjectName(QString("selectionFilter%1").arg(item.second));action->setCheckable(true);actions.push_back({action,item.second});
    }
    const auto update=[this,actions,enabled]{std::uint32_t mask=0;for(const auto& item:actions)if(item.first->isChecked())mask|=item.second;m_viewport->setSelectionFilters(mask,enabled->isChecked());};
    for(const auto& item:actions)connect(item.first,&QAction::toggled,this,update);connect(enabled,&QAction::toggled,this,update);
    auto* clear=filters->addAction("Clear filters");clear->setObjectName("actionClearSelectionFilters");connect(clear,&QAction::triggered,this,[actions,update]{for(const auto& item:actions){const QSignalBlocker block(item.first);item.first->setChecked(false);}update();});
    filters->addSeparator();filters->addWidget(new QLabel("Pick tolerance: "));
    auto* tolerance=new QDoubleSpinBox; tolerance->setObjectName("pickingTolerance");tolerance->setDecimals(1);tolerance->setRange(1,32);tolerance->setSuffix(" px");tolerance->setValue(m_viewport->pickingTolerance());filters->addWidget(tolerance);connect(tolerance,&QDoubleSpinBox::valueChanged,m_viewport,&CadViewport::setPickingTolerance);
    filters->hide();
    m_constructionPanel=new QGroupBox("Selected sketch geometry",m_propertyPanel->parentWidget());m_constructionPanel->setObjectName("constructionProperties");
    auto* layout=new QVBoxLayout(m_constructionPanel);m_forConstruction=new QCheckBox("For Construction");m_forConstruction->setObjectName("forConstruction");m_forConstruction->setTristate(true);layout->addWidget(m_forConstruction);
    m_constructionSummary=new QLabel;m_constructionSummary->setWordWrap(true);layout->addWidget(m_constructionSummary);
    auto* geometryLayout=qobject_cast<QVBoxLayout*>(m_propertyPanel->parentWidget()->layout());geometryLayout->insertWidget(geometryLayout->indexOf(m_propertyPanel),m_constructionPanel);m_constructionPanel->hide();
    connect(m_forConstruction,&QCheckBox::checkStateChanged,this,[this](Qt::CheckState state){setSelectionConstruction(state!=Qt::Unchecked);});
    connect(m_viewport,&CadViewport::constructionContextRequested,this,[this](QMenu* menu){
        if(!m_editSketch)return;const auto state=modeling::constructionState(*m_editSketch,m_viewport->selectedSketchEntities());if(state==modeling::ConstructionState::Unavailable)return;
        menu->addSeparator();auto* action=menu->addAction(state==modeling::ConstructionState::Mixed?"For Construction (mixed selection)":"For Construction");action->setObjectName("contextForConstruction");action->setCheckable(true);action->setChecked(state==modeling::ConstructionState::Construction);connect(action,&QAction::triggered,this,[this,state]{setSelectionConstruction(state!=modeling::ConstructionState::Construction);});
        if(state==modeling::ConstructionState::Mixed){auto* normal=menu->addAction("Make all normal geometry");connect(normal,&QAction::triggered,this,[this]{setSelectionConstruction(false);});}
    });
    m_referenceVisibility=viewMenu->addAction("Show Reference Geometry");m_referenceVisibility->setObjectName("actionReferenceVisibility");m_referenceVisibility->setCheckable(true);connect(m_referenceVisibility,&QAction::triggered,this,&MainWindow::toggleReferenceVisibility);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);connect(m_tree,&QTreeWidget::customContextMenuRequested,this,[this](const QPoint& p){const auto* item=m_tree->itemAt(p);if(!item)return;const auto id=item->data(0,Qt::UserRole).toString().toStdString();const auto f=std::find_if(m_document.features().begin(),m_document.features().end(),[&](const auto& f){return f.id==id;});if(f==m_document.features().end()||f->kind!=document::FeatureKind::ReferenceGeometry)return;m_tree->setCurrentItem(const_cast<QTreeWidgetItem*>(item));auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);menu->addAction(m_referenceVisibility);menu->popup(m_tree->viewport()->mapToGlobal(p));});
    connect(m_viewport,&CadViewport::selectionChanged,this,[this]{updateActions();const auto refs=m_viewport->selection();if(refs.size()==1)statusBar()->showMessage(QString::fromLatin1(modeling::selectionKindName(refs.front().kind))+" selected",3000);else if(refs.size()>1)statusBar()->showMessage(QString("%1 entities selected").arg(refs.size()),3000);});
}
void MainWindow::updateConstructionControls(){
    if(!m_forConstruction)return;
    const auto state=m_editSketch?modeling::constructionState(*m_editSketch,m_viewport->selectedSketchEntities()):modeling::ConstructionState::Unavailable;
    const bool editable=m_editSketch&&!m_operation&&!m_referenceDraft&&!m_constraints&&state!=modeling::ConstructionState::Unavailable;
    m_constructionPanel->setVisible(editable);const QSignalBlocker block(m_forConstruction);m_forConstruction->setCheckState(state==modeling::ConstructionState::Mixed?Qt::PartiallyChecked:state==modeling::ConstructionState::Construction?Qt::Checked:Qt::Unchecked);
    m_forConstruction->setEnabled(editable);m_construction->setEnabled(editable);m_construction->setChecked(state==modeling::ConstructionState::Construction);
    m_constructionSummary->setText(state==modeling::ConstructionState::Mixed?"Mixed normal and construction geometry. Check to make all construction; clear to make all normal.":"Construction curves keep their dimensions and relations and do not bound extrusion or cut profiles.");
    const auto* f=selectedFeature();const bool datum=f&&f->kind==document::FeatureKind::ReferenceGeometry;
    if(m_referenceVisibility){m_referenceVisibility->setEnabled(datum&&!m_editSketch&&!m_operation&&!m_referenceDraft);m_referenceVisibility->setChecked(datum&&f->referenceVisible);m_referenceVisibility->setText(datum&&f->referenceVisible?"Hide Reference Geometry":"Show Reference Geometry");}
}
void MainWindow::setSelectionConstruction(bool construction){
    if(!m_editSketch||m_operation||m_constraints||m_referenceDraft)return;
    const auto ids=m_viewport->selectedSketchEntities();const auto state=modeling::constructionState(*m_editSketch,ids);if(state==modeling::ConstructionState::Unavailable)return;
    if((state==modeling::ConstructionState::Construction&&construction)||(state==modeling::ConstructionState::Normal&&!construction))return;
    try{acceptConstrainedSketch(modeling::setSketchConstruction(*m_editSketch,ids,construction));m_viewport->setSketch(*m_editSketch);setWindowModified(true);updateEntityList();updateActions();statusBar()->showMessage(construction?"Selected curves are construction geometry.":"Selected curves are normal geometry.",3000);}
    catch(const std::exception& e){constraintError(QString::fromUtf8(e.what()));updateActions();}m_viewport->setFocus();
}
void MainWindow::toggleReferenceVisibility(){
    const auto* selected=selectedFeature();if(!selected||selected->kind!=document::FeatureKind::ReferenceGeometry||m_editSketch||m_operation||m_referenceDraft)return;
    auto feature=*selected;feature.referenceVisible=!feature.referenceVisible;auto candidate=m_document;
    try{candidate.replaceFeature(feature);m_viewport->clearSelection();applyModel(std::move(candidate),feature.id);}
    catch(const std::exception& e){reportError(QString::fromUtf8(e.what()));}
}
}
