#pragma once
#include <QJsonObject>
namespace thecad::document
{
class AssemblyDocument;
class AssemblyWorkflowStore final
{
public:
    static QJsonObject write(const AssemblyDocument&);
    static void read(AssemblyDocument&, const QJsonObject&);
};
} // namespace thecad::document
