#pragma once
#include "modeling/Sketch.h"
#include <QImage>
#include <QIcon>
namespace thecad::ui {
// Original line art shared by the floating command toolbar and OCCT sprites.
QImage relationArtwork(modeling::RelationKind,int pixels,bool symbol=false,bool hover=false,bool selected=false);
QIcon relationIcon(modeling::RelationKind);
}
