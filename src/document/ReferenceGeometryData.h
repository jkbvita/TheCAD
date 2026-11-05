#pragma once
#include "document/PartDocument.h"
#include <QJsonObject>
namespace thecad::document {
std::string referenceBodySignature(const TopoDS_Shape&);
modeling::DatumGeometry resolveGeometryReference(const modeling::GeometryReference&,
    const std::map<std::string,modeling::DatumGeometry>&,
    const std::map<std::string,modeling::Sketch>&,
    const std::map<std::string,TopoDS_Shape>&);
QJsonObject writeDatumDefinition(const modeling::DatumDefinition&);
modeling::DatumDefinition readDatumDefinition(const QJsonObject&);
}
