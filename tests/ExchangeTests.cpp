#include "exchange/ShapeExporter.h"
#include "document/PartDocument.h"
#include "modeling/Box.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Poly_Triangulation.hxx>
#include <Bnd_Box.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>
#include <QDir>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thecad;
namespace
{
void require(bool ok, const char* why)
{
    if (!ok)
        throw std::runtime_error(why);
}
double volume(const TopoDS_Shape& s)
{
    GProp_GProps mass;
    BRepGProp::VolumeProperties(s, mass);
    return mass.Mass();
}
std::array<double, 6> bounds(const TopoDS_Shape& s)
{
    Bnd_Box box;
    BRepBndLib::Add(s, box, false);
    std::array<double, 6> b;
    box.Get(b[0], b[1], b[2], b[3], b[4], b[5]);
    return b;
}
QByteArray bytes(const QString& p)
{
    QFile f(p);
    require(f.open(QIODevice::ReadOnly), "Cannot read fixture");
    return f.readAll();
}
int solids(const TopoDS_Shape& s)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(s, TopAbs_SOLID, map);
    return map.Extent();
}
std::vector<Handle(Poly_Triangulation)> meshes(const TopoDS_Shape& s)
{
    std::vector<Handle(Poly_Triangulation)> result;
    for (TopExp_Explorer e(s, TopAbs_FACE); e.More(); e.Next())
    {
        TopLoc_Location l;
        result.push_back(BRep_Tool::Triangulation(TopoDS::Face(e.Current()), l));
    }
    return result;
}
void checkStl(const QString& path, const TopoDS_Shape& source)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open STL");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    require(stream.skipRawData(80) == 80, "Missing STL header");
    quint32 count = 0;
    stream >> count;
    require(count > 0 && file.size() == 84 + qint64(count) * 50,
            "Invalid binary STL triangle count/length");
    double signedVolume = 0;
    Bnd_Box meshBounds;
    for (quint32 i = 0; i < count; ++i)
    {
        float nx, ny, nz;
        stream >> nx >> ny >> nz;
        require(std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz), "Invalid STL normal");
        std::array<gp_Pnt, 3> p;
        for (auto& point : p)
        {
            float x, y, z;
            stream >> x >> y >> z;
            require(std::isfinite(x) && std::isfinite(y) && std::isfinite(z), "Invalid STL vertex");
            point = {x, y, z};
            meshBounds.Add(point);
        }
        quint16 attribute;
        stream >> attribute;
        signedVolume +=
            gp_Vec(gp_Pnt(), p[0]).Dot(gp_Vec(gp_Pnt(), p[1]).Crossed(gp_Vec(gp_Pnt(), p[2]))) / 6;
    }
    require(stream.status() == QDataStream::Ok, "Truncated STL");
    const auto expected = volume(source);
    require(signedVolume > 0 && std::abs(signedVolume - expected) < std::max(1e-5, expected * 0.01),
            "STL volume/winding/units changed");
    const auto original = bounds(source);
    std::array<double, 6> b;
    meshBounds.Get(b[0], b[1], b[2], b[3], b[4], b[5]);
    for (int i = 0; i < 6; ++i)
        require(std::abs(b[i] - original[i]) < 0.11, "STL placement or bounds changed");
}
void checkStep(const QString& path, const TopoDS_Shape& source)
{
    const auto data = bytes(path);
    require(data.startsWith("ISO-10303-21;") && data.contains(".MILLI.,.METRE."),
            "STEP header or millimetre units missing");
    STEPControl_Reader reader;
    require(reader.ReadFile(path.toUtf8().constData()) == IFSelect_RetDone,
            "Cannot read exported STEP");
    reader.SetSystemLengthUnit(1.0);
    require(reader.TransferRoots() > 0, "STEP contains no transferable roots");
    const auto loaded = reader.OneShape();
    require(!loaded.IsNull() && BRepCheck_Analyzer(loaded).IsValid(), "STEP is invalid");
    require(solids(loaded) == solids(source), "STEP lost solid bodies");
    require(std::abs(volume(loaded) - volume(source)) <
                std::max(1e-5, std::abs(volume(source)) * 1e-8),
            "STEP volume/units changed");
    const auto a = bounds(source), b = bounds(loaded);
    for (int i = 0; i < 6; ++i)
        require(std::abs(a[i] - b[i]) < 1e-5, "STEP bounds or orientation changed");
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        if (argc > 1)
            exchange::configureResources(QString::fromLocal8Bit(argv[1]) + "/resources");
        QTemporaryDir directory;
        require(directory.isValid(), "No temporary directory");
        const auto output = directory.filePath(QStringLiteral("export \u00ed\u4e2d"));
        require(QDir().mkpath(output), "Cannot create Unicode export folder");
        std::vector<document::PartDocument> fixtures{
            document::PartDocument::createBox({17, 19, 23})};
        auto cut = document::PartDocument::createEmpty();
        modeling::Sketch base;
        base.entities = {{modeling::EntityKind::Rectangle, {-20, -15}, {20, 15}, 1, "base"}};
        const auto profile = cut.addSketch(base);
        cut.addExtrude(profile, {"base"}, 20, modeling::Direction::Positive);
        modeling::Sketch hole;
        hole.entities = {{modeling::EntityKind::Circle, {0, 0}, {}, 4, "hole"}};
        const auto tool = cut.addSketch(hole);
        cut.addCut(tool, {"hole"}, 20, true, modeling::Direction::Positive);
        fixtures.push_back(cut);
        auto multiple = document::PartDocument::createEmpty();
        modeling::Sketch circles;
        circles.plane = modeling::SketchPlane::XZ;
        circles.entities = {{modeling::EntityKind::Circle, {12, 8}, {}, 5, "a"},
                            {modeling::EntityKind::Circle, {40, 8}, {}, 7, "b"}};
        const auto circleSketch = multiple.addSketch(circles);
        multiple.addExtrudeRegions(circleSketch, {{"a"}, {"b"}}, 14, modeling::Direction::Negative);
        fixtures.push_back(multiple);
        for (std::size_t i = 0; i < fixtures.size(); ++i)
        {
            const auto& part = fixtures[i];
            const auto source = part.shape();
            BRepMesh_IncrementalMesh coarse(source, 1.0, false, 0.5, true);
            require(coarse.IsDone(), "Coarse fixture mesh failed");
            const auto beforeMeshes = meshes(source);
            const auto native = QDir(output).filePath(QString::number(i) + ".thepart");
            part.save(native);
            const auto before = bytes(native);
            for (const auto& extension : {QString("stl"), QString("step"), QString("STP")})
            {
                const auto path = QDir(output).filePath(QString::number(i) +
                                                        QStringLiteral(" d\u00edl.") + extension);
                exchange::ShapeExporter::write(source, path);
                if (extension == "stl")
                    checkStl(path, source);
                else
                    checkStep(path, source);
                require(meshes(source) == beforeMeshes, "Export modified the displayed model mesh");
                require(part.shape().IsSame(source), "Export replaced source geometry");
                part.save(native);
                require(bytes(native) == before, "Export changed editable document data");
            }
        }
        const auto protectedPath = QDir(output).filePath("protected.stl");
        QFile original(protectedPath);
        require(original.open(QIODevice::WriteOnly), "Cannot create protected fixture");
        original.write("keep existing contents");
        original.close();
        for (const auto& shape :
             {TopoDS_Shape{}, BRepBuilderAPI_MakeEdge(gp_Pnt(), gp_Pnt(10, 0, 0)).Shape()})
        {
            bool failed = false;
            try
            {
                exchange::ShapeExporter::write(shape, protectedPath);
            }
            catch (const std::exception&)
            {
                failed = true;
            }
            require(failed && bytes(protectedPath) == "keep existing contents",
                    "Failed export overwrote existing file");
        }
        bool failed = false;
        try
        {
            exchange::ShapeExporter::write(fixtures.front().shape(),
                                           QDir(output).filePath("missing/fail.step"));
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, "Missing export directory accepted");
        failed = false;
        try
        {
            exchange::ShapeExporter::write(fixtures.front().shape(),
                                           QDir(output).filePath("unsupported.obj"));
        }
        catch (const std::exception&)
        {
            failed = true;
        }
        require(failed, "Unsupported export accepted");
        require(QDir(output).entryList({".thecad-export-*"}, QDir::Files | QDir::Hidden).isEmpty(),
                "Temporary exports leaked");
        std::cout << "PASS: binary STL triangles/winding/volume, STEP solids/units/roundtrip, cut "
                     "and multiple tilted bodies, Unicode paths, source mesh/document preservation "
                     "and safe failure\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
