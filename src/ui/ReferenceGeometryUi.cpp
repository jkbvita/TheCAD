#include "ui/ReferenceGeometryUi.h"
#include "ui/CadViewport.h"
#include "modeling/Selection.h"
#include <QComboBox>
#include <QCheckBox>
#include <QTreeWidget>
#include <QHBoxLayout>
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
bool same(const GeometryReference& a,const GeometryReference& b){return a.source==b.source&&a.featureId==b.featureId&&a.entityId==b.entityId&&a.role==b.role&&a.index==b.index&&a.signature==b.signature&&a.topologyName==b.topologyName;}
std::vector<DatumVisual> visuals(const document::PartDocument& doc){std::vector<DatumVisual> result;for(const auto& f:doc.features())if(f.kind==document::FeatureKind::ReferenceGeometry)result.push_back({f.id,f.name,f.datum,f.referenceVisible});return result;}
double entered(QDoubleSpinBox* spin){bool okay=false;const double value=spin->locale().toDouble(spin->cleanText(),&okay);if(!okay||!spin->hasAcceptableInput())throw std::runtime_error("Enter a valid number in each visible parameter.");return value;}
}
void MainWindow::startReferenceGeometry(modeling::DatumKind kind,const document::Feature* editing){
    if(featureDraftActive()||m_session.editingSketch()||m_selectingPlane)return;
    auto draft=std::make_unique<ReferenceDraft>();draft->kind=kind;draft->editing=editing!=nullptr;if(editing)draft->feature=*editing;
    draft->support=editing?m_session.document().beforeFeature(editing->id):m_session.document();draft->options=draft->support.referenceOptions();
    std::optional<GeometryReference> selectedPlane;
    std::vector<GeometryReference> selectedReferences;std::vector<DatumKind> selectedKinds;std::optional<DatumMethod> selectedMethod;
    if(!editing)for(const auto& selection:m_viewport->selection())for(const auto& option:draft->options){
        using K=SelectionKind;const auto& ref=option.reference;bool match=false;
        if(selection.kind==K::Face)match=ref.source==ReferenceSource::BodyFace&&ref.index==selection.index;
        else if(selection.kind==K::Edge)match=(ref.source==ReferenceSource::BodyEdge||ref.source==ReferenceSource::BodyCircleCenter)&&ref.index==selection.index;
        else if(selection.kind==K::Vertex)match=ref.source==ReferenceSource::BodyVertex&&ref.index==selection.index;
        else if(selection.kind==K::OriginPlane)match=ref.source==ReferenceSource::BasePlane&&ref.index==selection.index;
        else if(selection.kind==K::ReferencePlane||selection.kind==K::ReferenceAxis||selection.kind==K::ReferencePoint)match=ref.source==ReferenceSource::Datum&&ref.featureId==selection.featureId;
        if(match){selectedReferences.push_back(ref);selectedKinds.push_back(option.geometry.kind);break;}
    }
    if(!editing){
        int requested=int(m_viewport->selection().size());
        if(m_tree->selectedItems().size()>1){selectedReferences.clear();selectedKinds.clear();requested=m_tree->selectedItems().size();for(const auto* node:m_tree->selectedItems()){const auto id=node->data(0,Qt::UserRole).toString();for(const auto& option:draft->options){const auto& ref=option.reference;const bool builtin=ref.source==ReferenceSource::BasePlane&&id==(ref.index==0?"origin:XY":ref.index==1?"origin:XZ":"origin:YZ");if(builtin||(ref.source==ReferenceSource::Datum&&ref.featureId==id.toStdString())||(ref.source==ReferenceSource::Origin&&id=="origin:")){selectedReferences.push_back(ref);selectedKinds.push_back(option.geometry.kind);break;}}}}
        draft->invalidPreselection=requested!=int(selectedReferences.size())||selectedReferences.size()>3;
        if(draft->invalidPreselection){selectedReferences.clear();selectedKinds.clear();}
    }
    if(kind==DatumKind::Axis&&selectedKinds==std::vector<DatumKind>{DatumKind::Axis})selectedMethod=DatumMethod::AxisOnReference;
    else if(kind==DatumKind::Axis&&selectedKinds==std::vector<DatumKind>{DatumKind::Point,DatumKind::Point})selectedMethod=DatumMethod::AxisTwoPoints;
    else if(kind==DatumKind::Plane&&selectedKinds==std::vector<DatumKind>{DatumKind::Plane})selectedMethod=DatumMethod::PlaneOffset;
    else if(kind==DatumKind::Plane&&selectedKinds==std::vector<DatumKind>{DatumKind::Point,DatumKind::Point,DatumKind::Point})selectedMethod=DatumMethod::PlaneThreePoints;
    // Keep consecutive XYZ point creation available when the previous result
    // remains selected. An explicitly selected body vertex supplies a derived
    // point; other point references remain available in the method selector.
    else if(kind==DatumKind::Point&&selectedKinds==std::vector<DatumKind>{DatumKind::Point}&&(selectedReferences.front().source==ReferenceSource::BodyVertex||selectedReferences.front().source==ReferenceSource::BodyCircleCenter))selectedMethod=DatumMethod::PointOnReference;
    if(!editing&&kind==DatumKind::Plane){
        if(m_selectedPlane){GeometryReference ref;ref.source=ReferenceSource::BasePlane;ref.index=int(*m_selectedPlane);selectedPlane=ref;}
        else if(const auto* feature=selectedFeature();feature&&feature->kind==document::FeatureKind::ReferenceGeometry&&feature->datum.kind==DatumKind::Plane){GeometryReference ref;ref.source=ReferenceSource::Datum;ref.featureId=feature->id;selectedPlane=ref;}
        else if(m_selectedFace)for(const auto& option:draft->options)if(option.reference.source==ReferenceSource::BodyFace&&option.reference.index==m_selectedFace&&option.geometry.kind==DatumKind::Plane){selectedPlane=option.reference;break;}
    }
    m_viewport->clearSelection();m_selectedFace=0;m_selectedPlane.reset();
    m_referenceDraft=std::move(draft);auto& d=*m_referenceDraft;m_mode=WorkspaceMode::FeatureDraft;hideRelationsContext();
    const auto title=kind==DatumKind::Point?"Reference Point":kind==DatumKind::Axis?"Reference Axis":"Reference Plane";
    d.page=new PropertyManagerPage(title,"Select references in the viewport or FeatureManager. The construction method and preview update from your selections. Use XYZ for direct entry.",m_managerStack);d.page->setObjectName("referenceGeometryPanel");d.page->buttons->setObjectName("referenceGeometryButtons");
    auto* methodForm=new QFormLayout;methodForm->setRowWrapPolicy(QFormLayout::WrapLongRows);d.method=new QComboBox;d.method->setMinimumContentsLength(18);d.method->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);d.method->setObjectName("datumMethod");for(int i=0;i<datumMethodCount;++i)if(datumKind(DatumMethod(i))==kind)d.method->addItem(datumMethodName(DatumMethod(i)),i);if(!editing&&kind!=DatumKind::Point)d.method->setCurrentIndex(d.method->findData(int(kind==DatumKind::Axis?DatumMethod::AxisOnReference:DatumMethod::PlaneOffset)));methodForm->addRow("Method",d.method);d.page->settings->addLayout(methodForm);d.automatic=new QCheckBox("Determine method from references");d.automatic->setObjectName("datumAutomatic");d.automatic->setChecked(!editing);d.page->settings->addWidget(d.automatic);if(kind==DatumKind::Plane){d.offsetConstraint=new QComboBox;d.offsetConstraint->setObjectName("planeConstraint");d.offsetConstraint->addItems({"Offset Distance","Coincident"});d.page->settings->addWidget(d.offsetConstraint);connect(d.offsetConstraint,&QComboBox::currentIndexChanged,this,[this](int index){if(index==1)m_referenceDraft->value->setValue(0);m_referenceDraft->value->setEnabled(index==0);updateReferenceGeometryPreview();});}
    auto* references=new QGroupBox("References");d.referenceForm=new QFormLayout(references);d.referenceForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for(int i=0;i<3;++i){d.references[i]=new QComboBox;d.references[i]->setMinimumContentsLength(16);d.references[i]->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);d.references[i]->setObjectName(QString("datumReference%1").arg(i+1));d.references[i]->setPlaceholderText("Select a reference");auto* row=new QWidget;auto* rowLayout=new QHBoxLayout(row);rowLayout->setContentsMargins(0,0,0,0);rowLayout->addWidget(d.references[i],1);auto* clear=new QPushButton("Clear");clear->setObjectName(QString("datumClearReference%1").arg(i+1));rowLayout->addWidget(clear);d.referenceForm->addRow(QString(i==0?"First Reference":i==1?"Second Reference":"Third Reference"),row);connect(clear,&QPushButton::clicked,this,[this,i]{m_referenceDraft->invalidPreselection=false;m_referenceDraft->references[i]->setCurrentIndex(-1);activateReferenceGeometryPick(i);updateReferenceGeometryPreview();});}
    d.page->settings->addWidget(references);
    const auto parameter=[&](QFormLayout* form,const QString& label,const QString& name,double value,const QString& suffix){auto* spin=new QDoubleSpinBox;spin->setObjectName(name);spin->setDecimals(6);spin->setRange(-1e9,1e9);spin->setSuffix(suffix);spin->setValue(value);form->addRow(label,spin);return spin;};
    d.positionGroup=new QGroupBox("Position");auto* positions=new QFormLayout(d.positionGroup);d.directionGroup=new QGroupBox(kind==DatumKind::Plane?"Plane normal":"Axis direction");auto* directions=new QFormLayout(d.directionGroup);
    for(int i=0;i<3;++i){const auto axis=QString(QChar('X'+i));d.position[i]=parameter(positions,axis,"datumPosition"+axis,editing?editing->reference.position.Coord(i+1):0," mm");d.direction[i]=parameter(directions,axis,"datumDirection"+axis,editing?editing->reference.direction.Coord(i+1):i==2?1:0,{});}
    d.page->settings->addWidget(d.positionGroup);d.page->settings->addWidget(d.directionGroup);d.valueForm=new QFormLayout;d.value=parameter(d.valueForm,"Distance / angle","datumValue",editing?editing->reference.value:kind==DatumKind::Plane?10:0," mm");d.page->settings->addLayout(d.valueForm);d.reverse=new QPushButton("Reverse offset / angle");d.reverse->setObjectName("datumReverse");d.page->settings->addWidget(d.reverse);connect(d.reverse,&QPushButton::clicked,this,[this]{auto& d=*m_referenceDraft;const double v=d.value->value();d.value->setValue(DatumMethod(d.method->currentData().toInt())==DatumMethod::PlaneTangentParallel?(v>=0?-std::max(1.0,v):-v):-v);});d.page->settings->addStretch();
    connect(d.method,&QComboBox::currentIndexChanged,this,[this]{const QSignalBlocker block(m_referenceDraft->automatic);m_referenceDraft->automatic->setChecked(false);m_referenceDraft->invalidPreselection=false;updateReferenceGeometryForm();});connect(d.method,&QComboBox::activated,this,[this]{const QSignalBlocker blocker(m_referenceDraft->automatic);m_referenceDraft->automatic->setChecked(false);m_referenceDraft->invalidPreselection=false;updateReferenceGeometryForm();});connect(d.automatic,&QCheckBox::toggled,this,[this]{m_referenceDraft->invalidPreselection=false;updateReferenceGeometryForm();});
    for(int i=0;i<3;++i)connect(d.references[i],&QComboBox::currentIndexChanged,this,[this,i]{m_referenceDraft->invalidPreselection=false;activateReferenceGeometryPick(i);updateReferenceGeometryPreview();});
    for(auto* spin:{d.position[0],d.position[1],d.position[2],d.direction[0],d.direction[1],d.direction[2],d.value}){
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{updateReferenceGeometryPreview();});connect(spin->findChild<QLineEdit*>(),&QLineEdit::textChanged,this,[this]{updateReferenceGeometryPreview();});
    }
    connect(d.page->buttons,&QDialogButtonBox::accepted,this,[this]{finishReferenceGeometry(true);});connect(d.page->buttons,&QDialogButtonBox::rejected,this,[this]{finishReferenceGeometry(false);});
    m_viewport->setSelectedSolidFace(0);m_viewport->setShape(d.support.shape());m_viewport->endSketch();m_viewport->setSelectedDatum({});
    if(editing){const QSignalBlocker blocker(d.method);d.method->setCurrentIndex(d.method->findData(int(editing->reference.method)));}
    updateReferenceGeometryForm();
    if(editing){for(int i=0;i<int(editing->reference.references.size());++i)for(int j=0;j<d.references[i]->count();++j){const int option=d.references[i]->itemData(j).toInt();if(same(editing->reference.references[std::size_t(i)],d.options[std::size_t(option)].reference)){const QSignalBlocker blocker(d.references[i]);d.references[i]->setCurrentIndex(j);break;}}updateReferenceGeometryPreview();}
    else if(!selectedReferences.empty()&&selectedReferences.size()<=3&&!(kind==DatumKind::Point&&selectedReferences.size()==1&&selectedReferences.front().source==ReferenceSource::Datum&&selectedKinds.front()==DatumKind::Point)){if(selectedMethod){const QSignalBlocker blocker(d.method);d.method->setCurrentIndex(d.method->findData(int(*selectedMethod)));}updateReferenceGeometryForm();for(int i=0;i<int(selectedReferences.size());++i)for(int j=0;j<d.references[i]->count();++j){const auto& ref=d.options[std::size_t(d.references[i]->itemData(j).toInt())].reference;if(same(ref,selectedReferences[std::size_t(i)])){d.references[i]->setCurrentIndex(j);break;}}}
    else if(selectedPlane){d.method->setCurrentIndex(d.method->findData(int(DatumMethod::PlaneOffset)));for(int j=0;j<d.references[0]->count();++j){const auto& ref=d.options[std::size_t(d.references[0]->itemData(j).toInt())].reference;if(same(ref,*selectedPlane)){d.references[0]->setCurrentIndex(j);break;}}}
    m_managerStack->addWidget(d.page);m_managerStack->setCurrentWidget(d.page);m_historyDock->setWindowTitle("PropertyManager");updateActions();
    const auto method=DatumMethod(d.method->currentData().toInt());if(datumReferenceKinds(method).empty()){d.position[0]->setFocus();d.position[0]->selectAll();}else if(method==DatumMethod::PlaneOffset){d.value->setFocus();d.value->selectAll();}else d.references[0]->setFocus();
}
void MainWindow::updateReferenceGeometryForm(){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;const auto method=DatumMethod(d.method->currentData().toInt());const auto kinds=datumReferenceKinds(method);
    const bool automatic=d.automatic->isChecked();d.referenceForm->parentWidget()->setVisible(automatic||!kinds.empty());
    for(int i=0;i<3;++i){const int previous=d.references[i]->currentIndex()<0?-1:d.references[i]->currentData().toInt();const QSignalBlocker blocker(d.references[i]);d.references[i]->clear();auto* row=d.references[i]->parentWidget();d.referenceForm->setRowVisible(row,automatic||i<int(kinds.size()));if(automatic||i<int(kinds.size()))for(int j=0;j<int(d.options.size());++j)if(automatic||d.options[std::size_t(j)].geometry.kind==kinds[std::size_t(i)])d.references[i]->addItem(QString::fromStdString(d.options[std::size_t(j)].label),j);d.references[i]->setCurrentIndex(d.references[i]->findData(previous));}
    d.positionGroup->setVisible(!automatic&&kinds.empty());d.directionGroup->setVisible(!automatic&&(method==DatumMethod::AxisXYZ||method==DatumMethod::PlaneXYZ));d.valueForm->setRowVisible(d.value,method==DatumMethod::PlaneOffset||method==DatumMethod::PlaneAtAngle);d.reverse->setVisible(method==DatumMethod::PlaneOffset||method==DatumMethod::PlaneAtAngle);
    activateReferenceGeometryPick(0);updateReferenceGeometryPreview();
}
void MainWindow::activateReferenceGeometryPick(int field){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;const auto kinds=datumReferenceKinds(DatumMethod(d.method->currentData().toInt()));
    if(field<0||field>=3||(!d.automatic->isChecked()&&field>=int(kinds.size()))){m_viewport->setReferenceCandidates({},{});return;}
    d.activeReference=field;std::vector<ReferencePickItem> items;for(const auto& option:d.options)items.push_back({option.reference,option.geometry,option.selectionShape});m_viewport->showReferencePlanes(true);m_viewport->setReferenceCandidates(items,d.automatic->isChecked()?std::optional<DatumKind>{}:std::optional<DatumKind>{kinds[std::size_t(field)]});
}
void MainWindow::referenceGeometryPicked(int option){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;if(option<0||option>=int(d.options.size()))return;
    for(auto* combo:d.references)if(combo->currentIndex()>=0&&combo->currentData().toInt()==option){d.page->setStatus("This reference is already selected. Clear or replace a reference to change the definition.");return;}
    auto* combo=d.references[std::size_t(d.activeReference)];const int row=combo->findData(option);if(row<0){d.page->setStatus("This entity cannot be used for the selected construction method.",true);return;}combo->setCurrentIndex(row);
    const auto kinds=datumReferenceKinds(DatumMethod(d.method->currentData().toInt()));const int count=d.automatic->isChecked()?3:int(kinds.size());for(int i=0;i<count;++i)if(d.references[i]->currentIndex()<0){activateReferenceGeometryPick(i);break;}
    if(d.value->isVisible()&&d.candidate){d.value->setFocus();d.value->selectAll();}else m_viewport->setFocus();
}
void MainWindow::referenceGeometryTreePicked(const QString& id){
    if(!m_referenceDraft)return;const auto& options=m_referenceDraft->options;
    for(int i=0;i<int(options.size());++i){const auto& r=options[std::size_t(i)].reference;const bool builtin=r.source==ReferenceSource::BasePlane&&id==(r.index==0?"origin:XY":r.index==1?"origin:XZ":"origin:YZ");if(builtin||(id=="origin:"&&r.source==ReferenceSource::Origin)||(r.source==ReferenceSource::Datum&&id.toStdString()==r.featureId)){referenceGeometryPicked(i);return;}}
    m_referenceDraft->page->setStatus("Select an individual face, straight edge, vertex, sketch point or reference feature. A whole solid or sketch is not a unique reference.",true);
}
void MainWindow::updateReferenceGeometryPreview(){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;d.candidate.reset();d.page->buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    try{if(d.invalidPreselection)throw std::runtime_error("The preselection contains an unsupported entity or more than three references. Select the intended references, or explicitly choose XYZ for direct entry.");DatumDefinition definition;definition.method=DatumMethod(d.method->currentData().toInt());
        std::vector<int> chosen;for(auto* combo:d.references)if(combo->currentIndex()>=0)chosen.push_back(combo->currentData().toInt());
        if(d.automatic->isChecked()&&!chosen.empty()){
            std::vector<int> methods;if(d.kind==DatumKind::Plane&&std::any_of(chosen.begin(),chosen.end(),[&](int i){return d.options[std::size_t(i)].geometry.cylinderRadius>0;}))methods={int(DatumMethod::PlaneTangentAtPoint),int(DatumMethod::PlaneTangentParallel)};for(int m=0;m<datumMethodCount;++m)methods.push_back(m);bool matched=false;for(int m:methods){if(matched)break;const auto method=DatumMethod(m);if(datumKind(method)!=d.kind)continue;const auto expected=datumReferenceKinds(method);if(expected.size()!=chosen.size())continue;std::vector<int> ordered;auto remaining=chosen;bool valid=true;for(const auto kind:expected){const auto found=std::find_if(remaining.begin(),remaining.end(),[&](int i){return d.options[std::size_t(i)].geometry.kind==kind;});if(found==remaining.end()){valid=false;break;}ordered.push_back(*found);remaining.erase(found);}if(valid){definition.method=method;for(int i:ordered)definition.references.push_back(d.options[std::size_t(i)].reference);matched=true;}}
            if(!matched)throw std::runtime_error("These references do not yet define a supported method. Add a required reference, clear an incompatible reference, or choose a method explicitly.");
            const QSignalBlocker blocker(d.method);d.method->setCurrentIndex(d.method->findData(int(definition.method)));
        }else if(d.automatic->isChecked()&&d.kind!=DatumKind::Point)throw std::runtime_error("Select references to define the preview, or choose XYZ for direct entry.");
        const auto kinds=datumReferenceKinds(definition.method);
        d.positionGroup->setVisible(kinds.empty());d.directionGroup->setVisible(definition.method==DatumMethod::AxisXYZ||definition.method==DatumMethod::PlaneXYZ);const bool offset=definition.method==DatumMethod::PlaneOffset;const bool tangent=definition.method==DatumMethod::PlaneTangentParallel;if(d.offsetConstraint)d.offsetConstraint->setVisible(offset);d.value->setEnabled(!offset||!d.offsetConstraint||d.offsetConstraint->currentIndex()==0);const bool parameter=offset||definition.method==DatumMethod::PlaneAtAngle;d.valueForm->setRowVisible(d.value,parameter);d.reverse->setVisible(parameter||tangent);d.reverse->setText(tangent?"Other tangent side":"Flip Offset / Reverse Direction");d.value->setSuffix(definition.method==DatumMethod::PlaneAtAngle?QString::fromUtf8(" °"):" mm");qobject_cast<QLabel*>(d.valueForm->labelForField(d.value))->setText(definition.method==DatumMethod::PlaneAtAngle?"Angle":"Offset distance");
        if(kinds.empty()){definition.position={entered(d.position[0]),entered(d.position[1]),entered(d.position[2])};if(d.kind!=DatumKind::Point)definition.direction={entered(d.direction[0]),entered(d.direction[1]),entered(d.direction[2])};}
        else if(definition.references.empty())for(int i=0;i<int(kinds.size());++i){if(d.references[i]->currentIndex()<0)throw std::runtime_error("Select every required reference to see the preview.");definition.references.push_back(d.options[std::size_t(d.references[i]->currentData().toInt())].reference);}
        if(definition.method==DatumMethod::PlaneOffset||definition.method==DatumMethod::PlaneAtAngle||definition.method==DatumMethod::PlaneTangentParallel)definition.value=entered(d.value);if(definition.method==DatumMethod::PlaneOffset&&d.offsetConstraint&&d.offsetConstraint->currentIndex()==1)definition.value=0;
        auto candidate=m_session.document();std::string id;if(d.editing){auto feature=d.feature;feature.reference=definition;candidate.replaceFeature(feature);id=feature.id;}else id=candidate.addReferenceGeometry(definition);
        auto display=visuals(d.support);const auto& feature=*std::find_if(candidate.features().begin(),candidate.features().end(),[&](const auto& f){return f.id==id;});display.push_back({id,feature.name,feature.datum});m_viewport->setDatums(display);m_viewport->setSelectedDatum(id);
        d.candidate=std::move(candidate);d.candidateId=id;d.page->setStatus(QString("Fully defined — %1. Confirm or Cancel.").arg(datumMethodName(definition.method)));d.page->buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }catch(const std::exception& error){m_viewport->setDatums(visuals(d.support));const bool hasReferences=std::any_of(d.references.begin(),d.references.end(),[](auto* c){return c->currentIndex()>=0;});d.page->setStatus(QString(hasReferences?"Invalid / under-defined: ":"Under-defined: ")+QString::fromUtf8(error.what()),hasReferences);}
}
void MainWindow::finishReferenceGeometry(bool accepted){
    if(!m_referenceDraft)return;auto& d=*m_referenceDraft;if(accepted){updateReferenceGeometryPreview();if(!d.candidate)return;if(!m_viewport->setShape(m_session.document().shape()))return;if(!applyModel(*d.candidate,d.candidateId))return;}
    auto* page=d.page;for(auto* child:page->findChildren<QObject*>())QObject::disconnect(child,nullptr,this,nullptr);m_managerStack->setCurrentIndex(0);m_managerStack->removeWidget(page);page->hide();page->setParent(nullptr);m_referenceDraft.reset();page->deleteLater();m_mode=WorkspaceMode::Part;m_viewport->setReferenceCandidates({},{});m_viewport->setShape(m_session.document().shape());m_viewport->showReferencePlanes(false);m_historyDock->setWindowTitle("FeatureManager");updateDocumentUi();m_viewport->setFocus();
}
void MainWindow::refreshReferenceGeometry(){if(!m_referenceDraft&&!m_solidFeature){m_viewport->setDatums(visuals(m_session.document()));const auto* feature=selectedFeature();m_viewport->setSelectedDatum(feature&&feature->kind==document::FeatureKind::ReferenceGeometry?feature->id:"");}}
void MainWindow::createSketchOnReferencePlane(const std::string& id){
    try{auto candidate=m_session.document();const auto sketchId=candidate.addSketch(candidate.sketchOnReferencePlane(id));m_selectingPlane=false;if(!applyModel(std::move(candidate),sketchId))return;startSketchEditing(*selectedFeature());}catch(const std::exception& error){reportError(QString::fromUtf8(error.what()));}
}
bool MainWindow::referenceGeometryEvent(QObject* watched,QEvent* event){
    if(!m_referenceDraft)return false;auto* widget=qobject_cast<QWidget*>(watched);if(!widget||widget->window()!=this)return false;
    if(event->type()==QEvent::FocusIn){for(int i=0;i<3;++i)if(widget==m_referenceDraft->references[i])activateReferenceGeometryPick(i);}
    if(event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Escape){finishReferenceGeometry(false);key->accept();return true;}if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){bool numeric=false;for(auto* spin:{m_referenceDraft->position[0],m_referenceDraft->position[1],m_referenceDraft->position[2],m_referenceDraft->direction[0],m_referenceDraft->direction[1],m_referenceDraft->direction[2],m_referenceDraft->value})numeric|=widget==spin||spin->isAncestorOf(widget);if(widget==m_viewport||numeric){finishReferenceGeometry(true);key->accept();return true;}}}
    return false;
}
}
