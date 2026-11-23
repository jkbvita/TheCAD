#pragma once
#include "modeling/assembly/MateTypes.h"
#include <QIcon>

namespace thecad::ui
{
// TheCAD's own compact geometry symbols for assembly mate types.
QIcon assemblyMateIcon(modeling::assembly::MateKind kind);
}
