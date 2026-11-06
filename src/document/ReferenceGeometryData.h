#pragma once
#include "document/PartDocument.h"
#include <QJsonObject>
namespace thecad::document {
std::string referenceBodySignature(const TopoDS_Shape&);
modeling::DatumGeometry resolveGeometryReference(const modeling::GeometryReference&,
    const std::map<std::string,modeling::DatumGeometry>&,
    const std::map<std::string,modeling::Sketch>&,
    const std::map<std::string,TopoDS_Shape>&,const std::map<std::string,modeling::TopologyNames>& names={});
void bindGeometryReference(modeling::GeometryReference&,const std::map<std::string,TopoDS_Shape>&,const std::map<std::string,modeling::TopologyNames>&);
QJsonObject writeDatumDefinition(const modeling::DatumDefinition&);
modeling::DatumDefinition readDatumDefinition(const QJsonObject&,int version=16);
}
