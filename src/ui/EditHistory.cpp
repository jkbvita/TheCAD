#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include "modeling/SketchContourEdits.h"
#include <QAction>
#include <QDataStream>
#include <QIODevice>
#include <QStatusBar>
#include <QTabWidget>

namespace thecad::ui {
MainWindow::EditState MainWindow::editState() const {
    return m_session.snapshot(m_viewport->displayedShape());
}
void MainWindow::rememberChange(EditState previous) {
    m_session.record(std::move(previous));updateHistoryActions();
}
void MainWindow::rememberSketchChange(EditState previous) {
    if(m_session.recordSketchChange(std::move(previous)))updateHistoryActions();
}
void MainWindow::updateHistoryActions() {
    if(!m_undo || !m_redo)return;
    const bool clickingLines=m_sketchModifier && modeling::isSketchLineEdit(m_sketchModifier->kind);
    const bool ready=(!featureDraftActive() || clickingLines) && !m_selectingPlane &&
        !(m_smartDimension && m_smartDimension->editor);
    m_undo->setEnabled(ready && m_session.canUndo());m_redo->setEnabled(ready && m_session.canRedo());
}
void MainWindow::traverseHistory(bool redo) {
    updateHistoryActions();
    if(!(redo?m_redo:m_undo)->isEnabled())return;
    if(m_sketchModifier && modeling::isSketchLineEdit(m_sketchModifier->kind))
        finishSketchModifier(false);
    auto previous=editState();const auto target=m_session.historyTarget(redo);
    const bool sameSketch=m_session.editingSketch() && target.sketch && m_session.editingSketchId()==target.sketchId;
    const bool constructionTool=m_viewport->constructionTool();
    const auto tool=sameSketch?m_viewport->sketchTool():std::optional<modeling::EntityKind>{};
    if(!m_viewport->setShape(target.displayedShape)){reportError("The history state could not be displayed; the current edit was retained.");return;}
    setMeasureActive(false);
    closeConstraints();closeSmartDimension();m_constraintPoint.reset();m_viewport->selectSketchTool();
    m_session.restore(target);
    m_selectedFace=0;m_selectedPlane.reset();m_viewport->setSelectedReferencePlane({});
    if(m_session.editingSketch()){
        m_mode=WorkspaceMode::EditingSketch;
        if(sameSketch)m_viewport->setSketch(*m_session.editingSketch());else m_viewport->beginSketch(*m_session.editingSketch());
        if(tool)m_viewport->setSketchTool(*tool,constructionTool);else m_viewport->selectSketchTool();m_tabs->setCurrentIndex(1);
    }else {
        m_mode=WorkspaceMode::Part;m_viewport->endSketch();
        if(const auto* feature=selectedFeature();feature && feature->kind==document::FeatureKind::Sketch)m_viewport->setSketch(feature->sketch);
        m_tabs->setCurrentIndex(0);
    }
    m_viewport->showReferencePlanes(!m_session.editingSketch() && m_session.document().features().empty());
    m_session.commitTraversal(redo,std::move(previous));
    setWindowModified(m_session.modified());updateDocumentUi();m_viewport->setFocus();
    statusBar()->showMessage(redo?"Redo":"Undo",2000);
}
}
