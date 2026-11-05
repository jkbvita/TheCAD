#include "ui/ReferenceGeometryUi.h"
#include "ui/CadViewport.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QDockWidget>
#include <QAction>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
namespace thecad::ui {
namespace {
using namespace modeling;
bool same(const GeometryReference& a,const GeometryReference& b){return a.source==b.source&&a.featureId==b.featureId&&a.entityId==b.entityId&&a.role==b.role&&a.index==b.index&&a.signature==b.signature;}
std::vector<DatumVisual> visuals(const document::PartDocument& doc){std::vector<DatumVisual> result;for(const auto& f:doc.features())if(f.kind==document::FeatureKind::ReferenceGeometry)result.push_back({f.id,f.name,f.datum});return result;}
double entered(QDoubleSpinBox* spin){bool okay=false;const double value=spin->locale().toDouble(spin->cleanText(),&okay);if(!okay||!spin->hasAcceptableInput())throw std::runtime_error("Enter a valid number in each visible parameter.");return value;}
}
void MainWindow::startReferenceGeometry(modeling::DatumKind kind,const document::Feature* editing){
    if(m_referenceDraft||m_operation||m_editSketch||m_selectingPlane)return;
    auto draft=std::make_unique<ReferenceDraft>();draft->kind=kind;draft->editing=editing!=nullptr;if(editing)draft->feature=*editing;
    draft->support=editing?m_document.beforeFeature(editing->id):m_document;draft->options=draft->support.referenceOptions();
    std::optional<GeometryReference> selectedPlane;
    if(!editing&&kind==DatumKind::Plane){
        if(m_selectedPlane){GeometryReference ref;ref.source=ReferenceSource::BasePlane;ref.index=int(*m_selectedPlane);selectedPlane=ref;}
        else if(const auto* feature=selectedFeature();feature&&feature->kind==document::FeatureKind::ReferenceGeometry&&feature->datum.kind==DatumKind::Plane){GeometryReference ref;ref.source=ReferenceSource::Datum;ref.featureId=feature->id;selectedPlane=ref;}
        else if(m_selectedFace)for(const auto& option:draft->options)if(option.reference.source==ReferenceSource::BodyFace&&option.reference.index==m_selectedFace&&option.geometry.kind==DatumKind::Plane){selectedPlane=option.reference;break;}
    }
    m_selectedFace=0;m_selectedPlane.reset();
    m_referenceDraft=std::move(draft);auto& d=*m_referenceDraft;m_mode=WorkspaceMode::FeatureDraft;hideRelationsContext();
    const auto title=kind==DatumKind::Point?"Reference Point":kind==DatumKind::Axis?"Reference Axis":"Reference Plane";
    d.page=new PropertyManagerPage(title,"Choose a construction method. Select references in the viewport or the lists, or enter XYZ coordinates.",m_managerStack);d.page->setObjectName("referenceGeometryPanel");d.page->buttons->setObjectName("referenceGeometryButtons");
    auto* methodForm=new QFormLayout;methodForm->setRowWrapPolicy(QFormLayout::WrapLongRows);d.method=new QComboBox;d.method->setMinimumContentsLength(18);d.method->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);d.method->setObjectName("datumMethod");for(int i=0;i<15;++i)if(datumKind(DatumMethod(i))==kind)d.method->addItem(datumMethodName(DatumMethod(i)),i);methodForm->addRow("Method",d.method);d.page->settings->addLayout(methodForm);
    auto* references=new QGroupBox("References");d.referenceForm=new QFormLayout(references);d.referenceForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for(int i=0;i<3;++i){d.references[i]=new QComboBox;d.references[i]->setMinimumContentsLength(16);d.references[i]->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);d.references[i]->setObjectName(QString("datumReference%1").arg(i+1));d.references[i]->setPlaceholderText("Select a reference");d.referenceForm->addRow(QString("Reference %1").arg(i+1),d.references[i]);}
    d.page->settings->addWidget(references);
    const auto parameter=[&](QFormLayout* form,const QString& label,const QString& name,double value,const QString& suffix){auto* spin=new QDoubleSpinBox;spin->setObjectName(name);spin->setDecimals(6);spin->setRange(-1e9,1e9);spin->setSuffix(suffix);spin->setValue(value);form->addRow(label,spin);return spin;};
    d.positionGroup=new QGroupBox("Position");auto* positions=new QFormLayout(d.positionGroup);d.directionGroup=new QGroupBox(kind==DatumKind::Plane?"Plane normal":"Axis direction");auto* directions=new QFormLayout(d.directionGroup);
    for(int i=0;i<3;++i){const auto axis=QString(QChar('X'+i));d.position[i]=parameter(positions,axis,"datumPosition"+axis,editing?editing->reference.position.Coord(i+1):0," mm");d.direction[i]=parameter(directions,axis,"datumDirection"+axis,editing?editing->reference.direction.Coord(i+1):i==2?1:0,{});}
    d.page->settings->addWidget(d.positionGroup);d.page->settings->addWidget(d.directionGroup);d.valueForm=new QFormLayout;d.value=parameter(d.valueForm,"Distance / angle","datumValue",editing?editing->reference.value:0," mm");d.page->settings->addLayout(d.valueForm);d.page->settings->addStretch();
    connect(d.method,&QComboBox::currentIndexChanged,this,[this]{updateReferenceGeometryForm();});
    for(int i=0;i<3;++i)connect(d.references[i],&QComboBox::currentIndexChanged,this,[this,i]{activateReferenceGeometryPick(i);updateReferenceGeometryPreview();});
    for(auto* spin:{d.position[0],d.position[1],d.position[2],d.direction[0],d.direction[1],d.direction[2],d.value}){
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{updateReferenceGeometryPreview();});connect(spin->findChild<QLineEdit*>(),&QLineEdit::textChanged,this,[this]{updateReferenceGeometryPreview();});
    }
    connect(d.page->buttons,&QDialogButtonBox::accepted,this,[this]{finishReferenceGeometry(true);});connect(d.page->buttons,&QDialogButtonBox::rejected,this,[this]{finishReferenceGeometry(false);});
    m_viewport->setSelectedSolidFace(0);m_viewport->setShape(d.support.shape());m_viewport->endSketch();m_viewport->setSelectedDatum({});
    if(editing){const QSignalBlocker blocker(d.method);d.method->setCurrentIndex(d.method->findData(int(editing->reference.method)));}
    updateReferenceGeometryForm();
    if(editing){for(int i=0;i<int(editing->reference.references.size());++i)for(int j=0;j<d.references[i]->count();++j){const int option=d.references[i]->itemData(j).toInt();if(same(editing->reference.references[std::size_t(i)],d.options[std::size_t(option)].reference)){const QSignalBlocker blocker(d.references[i]);d.references[i]->setCurrentIndex(j);break;}}updateReferenceGeometryPreview();}
    else if(selectedPlane){d.method->setCurrentIndex(d.method->findData(int(DatumMethod::PlaneOffset)));for(int j=0;j<d.references[0]->count();++j){const auto& ref=d.options[std::size_t(d.references[0]->itemData(j).toInt())].reference;if(same(ref,*selectedPlane)){d.references[0]->setCurrentIndex(j);break;}}}
    m_managerStack->addWidget(d.page);m_managerStack->setCurrentWidget(d.page);m_historyDock->setWindowTitle("PropertyManager");updateActions();
    const auto method=DatumMethod(d.method->currentData().toInt());if(datumReferenceKinds(method).empty()){d.position[0]->setFocus();d.position[0]->selectAll();}else if(method==DatumMethod::PlaneOffset){d.value->setFocus();d.value->selectAll();}else d.references[0]->setFocus();
}
void MainWindow::updateReferenceGeometryForm(){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;const auto method=DatumMethod(d.method->currentData().toInt());const auto kinds=datumReferenceKinds(method);
    d.referenceForm->parentWidget()->setVisible(!kinds.empty());
    for(int i=0;i<3;++i){const QSignalBlocker blocker(d.references[i]);d.references[i]->clear();d.referenceForm->setRowVisible(d.references[i],i<int(kinds.size()));if(i<int(kinds.size()))for(int j=0;j<int(d.options.size());++j)if(d.options[std::size_t(j)].geometry.kind==kinds[std::size_t(i)])d.references[i]->addItem(QString::fromStdString(d.options[std::size_t(j)].label),j);d.references[i]->setCurrentIndex(-1);}
    d.positionGroup->setVisible(kinds.empty());d.directionGroup->setVisible(method==DatumMethod::AxisXYZ||method==DatumMethod::PlaneXYZ);d.valueForm->setRowVisible(d.value,method==DatumMethod::PlaneOffset||method==DatumMethod::PlaneAtAngle);d.value->setSuffix(method==DatumMethod::PlaneAtAngle?QString::fromUtf8(" °"):" mm");
    qobject_cast<QLabel*>(d.valueForm->labelForField(d.value))->setText(method==DatumMethod::PlaneAtAngle?"Angle":"Offset distance");
    activateReferenceGeometryPick(0);updateReferenceGeometryPreview();
}
void MainWindow::activateReferenceGeometryPick(int field){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;const auto kinds=datumReferenceKinds(DatumMethod(d.method->currentData().toInt()));
    if(field<0||field>=int(kinds.size())){m_viewport->setReferenceCandidates({},{});return;}
    d.activeReference=field;std::vector<ReferencePickItem> items;for(const auto& option:d.options)items.push_back({option.reference,option.geometry,option.selectionShape});m_viewport->showReferencePlanes(true);m_viewport->setReferenceCandidates(items,kinds[std::size_t(field)]);
}
void MainWindow::referenceGeometryPicked(int option){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;if(option<0||option>=int(d.options.size()))return;auto* combo=d.references[std::size_t(d.activeReference)];const int row=combo->findData(option);if(row<0)return;combo->setCurrentIndex(row);
    const auto kinds=datumReferenceKinds(DatumMethod(d.method->currentData().toInt()));for(int i=0;i<int(kinds.size());++i)if(d.references[i]->currentIndex()<0){activateReferenceGeometryPick(i);d.references[i]->setFocus();return;}
    if(d.value->isVisible()){d.value->setFocus();d.value->selectAll();}else m_viewport->setFocus();
}
void MainWindow::updateReferenceGeometryPreview(){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;d.candidate.reset();d.page->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    try{DatumDefinition definition;definition.method=DatumMethod(d.method->currentData().toInt());const auto kinds=datumReferenceKinds(definition.method);
        if(kinds.empty()){definition.position={entered(d.position[0]),entered(d.position[1]),entered(d.position[2])};if(d.kind!=DatumKind::Point)definition.direction={entered(d.direction[0]),entered(d.direction[1]),entered(d.direction[2])};}
        else for(int i=0;i<int(kinds.size());++i){if(d.references[i]->currentIndex()<0)throw std::runtime_error("Select every required reference to see the preview.");definition.references.push_back(d.options[std::size_t(d.references[i]->currentData().toInt())].reference);}
        if(definition.method==DatumMethod::PlaneOffset||definition.method==DatumMethod::PlaneAtAngle)definition.value=entered(d.value);
        auto candidate=m_document;std::string id;if(d.editing){auto feature=d.feature;feature.reference=definition;candidate.replaceFeature(feature);id=feature.id;}else id=candidate.addReferenceGeometry(definition);
        auto display=visuals(d.support);const auto& feature=*std::find_if(candidate.features().begin(),candidate.features().end(),[&](const auto& f){return f.id==id;});display.push_back({id,feature.name,feature.datum});m_viewport->setDatums(display);m_viewport->setSelectedDatum(id);
        d.candidate=std::move(candidate);d.candidateId=id;d.page->setStatus("Reference fully defined. Confirm to add it, or Cancel to keep the part unchanged.");d.page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }catch(const std::exception& error){m_viewport->setDatums(visuals(d.support));d.page->setStatus(QString::fromUtf8(error.what()),true);}
}
void MainWindow::finishReferenceGeometry(bool accepted){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;if(accepted){updateReferenceGeometryPreview();if(!d.candidate)return;if(!m_viewport->setShape(m_document.shape()))return;if(!applyModel(*d.candidate,d.candidateId))return;}
    auto* page=d.page;for(auto* child:page->findChildren<QObject*>())QObject::disconnect(child,nullptr,this,nullptr);m_managerStack->setCurrentIndex(0);m_managerStack->removeWidget(page);page->hide();page->setParent(nullptr);m_referenceDraft.reset();page->deleteLater();m_mode=WorkspaceMode::Part;m_viewport->setReferenceCandidates({},{});m_viewport->setShape(m_document.shape());m_viewport->showReferencePlanes(false);m_historyDock->setWindowTitle("FeatureManager");updateDocumentUi();m_viewport->fitAll();m_viewport->setFocus();
}
void MainWindow::refreshReferenceGeometry(){if(!m_referenceDraft){m_viewport->setDatums(visuals(m_document));const auto* feature=selectedFeature();m_viewport->setSelectedDatum(feature&&feature->kind==document::FeatureKind::ReferenceGeometry?feature->id:"");}}
void MainWindow::createSketchOnReferencePlane(const std::string& id){
    try{auto candidate=m_document;const auto sketchId=candidate.addSketch(candidate.sketchOnReferencePlane(id));m_selectingPlane=false;if(!applyModel(std::move(candidate),sketchId))return;startSketchEditing(*selectedFeature());}catch(const std::exception& error){reportError(QString::fromUtf8(error.what()));}
}
bool MainWindow::referenceGeometryEvent(QObject* watched,QEvent* event){
    if(!m_referenceDraft)return false;auto* widget=qobject_cast<QWidget*>(watched);if(!widget||widget->window()!=this)return false;
    if(event->type()==QEvent::FocusIn){for(int i=0;i<3;++i)if(widget==m_referenceDraft->references[i])activateReferenceGeometryPick(i);}
    if(event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Escape){finishReferenceGeometry(false);key->accept();return true;}if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){bool numeric=false;for(auto* spin:{m_referenceDraft->position[0],m_referenceDraft->position[1],m_referenceDraft->position[2],m_referenceDraft->direction[0],m_referenceDraft->direction[1],m_referenceDraft->direction[2],m_referenceDraft->value})numeric|=widget==spin||spin->isAncestorOf(widget);if(widget==m_viewport||numeric){finishReferenceGeometry(true);key->accept();return true;}}}
    return false;
}
}
