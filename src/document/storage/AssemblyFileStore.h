#pragma once
#include <QString>
namespace thecad::document
{
class AssemblyDocument;
class AssemblyFileStore final
{
public:
    static AssemblyDocument load(const QString& path);
    static void save(const AssemblyDocument& document, const QString& path);
};
} // namespace thecad::document
