#include "exchange/ShapeExporter.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <STEPControl_Writer.hxx>
#include <StepData_StepModel.hxx>
#include <DESTEP_Parameters.hxx>
#include <StlAPI_Writer.hxx>
#include <Standard_Failure.hxx>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace thecad::exchange
{
namespace
{
[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}
void meshForStl(const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds, Standard_False);
    double x0, y0, z0, x1, y1, z1;
    bounds.Get(x0, y0, z0, x1, y1, z1);
    const double diagonal = gp_Pnt(x0, y0, z0).Distance(gp_Pnt(x1, y1, z1));
    const double deflection = std::clamp(diagonal * 0.001, 1e-6, 0.05);
    BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False, 0.1, Standard_True);
    if (!mesher.IsDone())
        fail("The solid could not be triangulated for STL.");
    std::uint64_t nodes = 0, triangles = 0;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next())
    {
        TopLoc_Location location;
        const auto mesh = BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location);
        if (mesh.IsNull() || mesh->NbTriangles() == 0)
            fail("STL export could not triangulate every face. No partial mesh was saved.");
        nodes += mesh->NbNodes();
        triangles += mesh->NbTriangles();
    }
    if (triangles == 0 || nodes > std::uint64_t(std::numeric_limits<Standard_Integer>::max()) ||
        triangles > std::uint64_t(std::numeric_limits<Standard_Integer>::max()))
        fail("The STL mesh exceeds the supported triangle or vertex count.");
}
void replaceAtomically(const QString& temporary, const QString& path)
{
    QFile input(temporary);
    if (!input.open(QIODevice::ReadOnly) || input.size() == 0)
        fail("The exported file is empty or could not be read.");
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        fail("Cannot write export: " + output.errorString());
    while (!input.atEnd())
    {
        const auto bytes = input.read(1024 * 1024);
        if (input.error() != QFile::NoError)
            fail("Cannot read the generated export: " + input.errorString());
        if (output.write(bytes) != bytes.size())
            fail("Cannot write export: " + output.errorString());
    }
    if (!output.commit())
        fail("Cannot save export atomically: " + output.errorString());
}
} // namespace
std::optional<ExportFormat> ShapeExporter::formatForPath(const QString& path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "stl")
        return ExportFormat::Stl;
    if (suffix == "step" || suffix == "stp")
        return ExportFormat::Step;
    return {};
}
void ShapeExporter::write(const TopoDS_Shape& source, const QString& path)
{
    const auto format = formatForPath(path);
    if (!format)
        fail("Export as .stl, .step or .stp.");
    if (source.IsNull() || !TopExp_Explorer(source, TopAbs_SOLID).More())
        fail("Create a solid body before exporting STL or STEP.");
    try
    {
        if (!BRepCheck_Analyzer(source).IsValid())
            fail("The model is invalid and cannot be exported.");
        // Meshing and STEP shape processing must never mutate the active model.
        BRepBuilderAPI_Copy copy(source, Standard_True, Standard_False);
        const auto shape = copy.Shape();
        const auto parent = QFileInfo(path).absoluteDir();
        QTemporaryFile temporary(parent.filePath(".thecad-export-XXXXXX"));
        if (!temporary.open())
            fail("Cannot create an export in this folder: " + temporary.errorString());
        const auto temporaryPath = temporary.fileName();
        temporary.close();
        if (*format == ExportFormat::Stl)
        {
            meshForStl(shape);
            StlAPI_Writer writer;
            writer.ASCIIMode() = Standard_False;
            if (!writer.Write(shape, temporaryPath.toUtf8().constData()))
                fail("The STL mesh could not be written.");
        }
        else
        {
            STEPControl_Writer writer;
            writer.Model()->SetLocalLengthUnit(1.0);
            DESTEP_Parameters parameters;
            parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
            if (writer.Transfer(shape, STEPControl_AsIs, parameters) != IFSelect_RetDone)
                fail("The solid could not be translated to STEP.");
            std::ofstream stream(std::filesystem::path(temporaryPath.toStdWString()),
                                 std::ios::binary | std::ios::trunc);
            if (!stream || writer.WriteStream(stream) != IFSelect_RetDone)
                fail("The STEP file could not be written.");
            stream.flush();
            if (!stream)
                fail("The STEP file could not be flushed to disk.");
            stream.close();
            if (stream.fail())
                fail("The STEP file could not be closed.");
        }
        replaceAtomically(temporaryPath, path);
    }
    catch (const Standard_Failure& error)
    {
        fail("Export failed: " + QString::fromUtf8(error.GetMessageString()));
    }
}
void configureResources(const QString& root)
{
    const QDir directory(root);
    for (const auto& item : {std::pair<const char*, const char*>{"CSF_XSMessage", "XSMessage"},
                             {"CSF_SHMessage", "SHMessage"},
                             {"CSF_STEPDefaults", "XSTEPResource"},
                             {"CSF_UnitsDefinition", "UnitsAPI/Units.dat"}})
    {
        const auto path = directory.filePath(QString::fromLatin1(item.second));
        if (QFileInfo::exists(path))
            qputenv(item.first, QDir::toNativeSeparators(path).toUtf8());
    }
}
} // namespace thecad::exchange
