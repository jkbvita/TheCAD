#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <algorithm>
namespace thecad::ui {
void MainWindow::toggleConstruction(){
    if(!m_editSketch)return;const auto id=m_viewport->selectedSketchEntity();
    const auto found=std::find_if(m_editSketch->entities.begin(),m_editSketch->entities.end(),[&](const auto& e){return e.id==id && e.kind==modeling::EntityKind::Line;});
    if(found==m_editSketch->entities.end())return;auto e=*found;e.construction=!e.construction;applySketchEntityEdit(e);updateActions();
}
}
