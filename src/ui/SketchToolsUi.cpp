#include "ui/MainWindow.h"
#include "ui/CadViewport.h"
#include <algorithm>
namespace thecad::ui {
void MainWindow::toggleConstruction(){
    applyContextConstruction(m_viewport->selectedSketchEntities());
}
}
