#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <QAction>
#include <QDataStream>
#include <QIODevice>
#include <QStatusBar>
#include <QTabWidget>

namespace thecad::ui {
namespace {
// Exact edit identity, independent of solver diagnostics and generated topology.
QByteArray sketchKey(const modeling::Sketch& sketch) {
    QByteArray key;QDataStream out(&key,QIODevice::WriteOnly);
    const auto point=[&](modeling::Point2 p){out<<p.x<<p.y;};
    const auto ref=[&](const modeling::SketchPointRef& r){out<<QString::fromStdString(r.entityId)<<int(r.role);};
    out<<int(sketch.plane)<<int(sketch.space)<<bool(sketch.attachment);
    if(sketch.attachment){const auto& a=*sketch.attachment;out<<QString::fromStdString(a.sourceFeature)<<a.faceIndex<<QString::fromStdString(a.sourceSignature);
        const auto& f=a.frame;out<<f.Location().X()<<f.Location().Y()<<f.Location().Z()<<f.XDirection().X()<<f.XDirection().Y()<<f.XDirection().Z()<<f.Direction().X()<<f.Direction().Y()<<f.Direction().Z();}
    out<<quint64(sketch.entities.size());for(const auto& e:sketch.entities){out<<QString::fromStdString(e.id)<<int(e.kind)<<e.construction;point(e.first);point(e.second);out<<e.radius;}
    out<<quint64(sketch.relations.size());for(const auto& r:sketch.relations){out<<QString::fromStdString(r.id)<<int(r.kind);ref(r.first);ref(r.second);point(r.fixedPosition);}
    out<<quint64(sketch.dimensions.size());for(const auto& d:sketch.dimensions){out<<QString::fromStdString(d.id)<<int(d.kind);ref(d.first);ref(d.second);out<<d.value<<d.driving<<d.sense;point(d.annotation);out<<bool(d.placement);if(d.placement)point(*d.placement);}
    out<<quint64(sketch.polygons.size());for(const auto& g:sketch.polygons){out<<QString::fromStdString(g.id)<<QString::fromStdString(g.centerId)<<quint64(g.edges.size());for(const auto& id:g.edges)out<<QString::fromStdString(id);}
    return key;
}
}
MainWindow::EditState MainWindow::editState() const {
    return {m_document,m_editSketch,m_editSketchId,m_selectedId,m_viewport->displayedShape(),m_revision};
}
void MainWindow::rememberChange(EditState previous) {
    m_editHistory.record(std::move(previous));m_revision=++m_nextRevision;updateHistoryActions();
}
void MainWindow::rememberSketchChange(EditState previous) {
    if(previous.sketch && m_editSketch && sketchKey(*previous.sketch)!=sketchKey(*m_editSketch))rememberChange(std::move(previous));
}
void MainWindow::updateHistoryActions() {
    if(!m_undo || !m_redo)return;
    const bool ready=!m_operation && !m_selectingPlane && !(m_smartDimension && m_smartDimension->editor);
    m_undo->setEnabled(ready && m_editHistory.canUndo());m_redo->setEnabled(ready && m_editHistory.canRedo());
}
void MainWindow::traverseHistory(bool redo) {
    updateHistoryActions();
    if(!(redo?m_redo:m_undo)->isEnabled())return;
    auto previous=editState();const auto target=redo?m_editHistory.redoTarget():m_editHistory.undoTarget();
    const bool sameSketch=m_editSketch && target.sketch && m_editSketchId==target.sketchId;
    const bool constructionTool=m_viewport->constructionTool();
    const auto tool=sameSketch?m_viewport->sketchTool():std::optional<modeling::EntityKind>{};
    if(!m_viewport->setShape(target.displayedShape)){reportError("The history state could not be displayed; the current edit was retained.");return;}
    closeConstraints();closeSmartDimension();m_constraintPoint.reset();m_viewport->selectSketchTool();
    m_document=target.document;m_editSketch=target.sketch;m_editSketchId=target.sketchId;m_selectedId=target.selectedId;m_revision=target.revision;
    m_selectedFace=0;m_selectedPlane.reset();m_viewport->setSelectedReferencePlane({});
    if(m_editSketch){
        m_mode=WorkspaceMode::EditingSketch;
        if(sameSketch)m_viewport->setSketch(*m_editSketch);else m_viewport->beginSketch(*m_editSketch);
        if(tool)m_viewport->setSketchTool(*tool,constructionTool);else m_viewport->selectSketchTool();m_tabs->setCurrentIndex(1);
    }else {
        m_mode=WorkspaceMode::Part;m_viewport->endSketch();
        if(const auto* feature=selectedFeature();feature && feature->kind==document::FeatureKind::Sketch)m_viewport->setSketch(feature->sketch);
        m_tabs->setCurrentIndex(0);
    }
    m_viewport->showReferencePlanes(!m_editSketch && m_document.features().empty());
    if(redo)m_editHistory.commitRedo(std::move(previous));else m_editHistory.commitUndo(std::move(previous));
    setWindowModified(m_path.isEmpty() || m_revision!=m_savedRevision);updateDocumentUi();m_viewport->setFocus();
    statusBar()->showMessage(redo?"Redo":"Undo",2000);
}
}
