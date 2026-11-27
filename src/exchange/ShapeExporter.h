#pragma once
#include <TopoDS_Shape.hxx>
#include <QString>
#include <optional>
namespace thecad::exchange
{
enum class ExportFormat
{
    Stl,
    Step
};
// Exports an isolated copy of final solid geometry, with atomic target replacement.
class ShapeExporter final
{
public:
    static std::optional<ExportFormat> formatForPath(const QString& path);
    static void write(const TopoDS_Shape& source, const QString& path);
};
// Process bootstrap for the OCCT resources bundled beside the executable.
void configureResources(const QString& root);
} // namespace thecad::exchange
