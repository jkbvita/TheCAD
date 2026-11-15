#pragma once
#include "document/AssemblyDocument.h"
#include <QJsonObject>
namespace thecad::document
{
QJsonObject writeAssemblyMate(const AssemblyMate&);
AssemblyMate readAssemblyMate(const QJsonObject&);
QJsonObject writeMateDefinition(const AssemblyMate::Definition&);
QJsonObject writeAssemblyReference(const AssemblyReference&);
AssemblyReference readAssemblyReference(const QJsonValue&);
QJsonArray writeAssemblyTransform(const gp_Trsf&);
gp_Trsf readAssemblyTransform(const QJsonValue&);
} // namespace thecad::document
