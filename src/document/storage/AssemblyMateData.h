#pragma once
#include "document/AssemblyDocument.h"
#include <QJsonObject>
namespace thecad::document
{
QJsonObject writeAssemblyMate(const AssemblyMate&);
AssemblyMate readAssemblyMate(const QJsonObject&);
QJsonObject writeMateDefinition(const AssemblyMate::Definition&);
} // namespace thecad::document
