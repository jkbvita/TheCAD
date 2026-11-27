#pragma once
#include "document/PartDocument.h"
namespace thecad::document::detail
{
inline constexpr qint64 maximumDocumentBytes = 1024 * 1024;
inline constexpr std::size_t maximumFeatures = 500, maximumEntities = 1000;
inline constexpr double maximumLength = 1e9;
[[noreturn]] void fail(const QString& message);
QString q(const std::string& value);
void validId(const std::string& id);
const char* typeName(FeatureKind kind);
QString planeName(modeling::SketchPlane plane);
QString directionName(modeling::Direction direction);
std::string bodySignature(const TopoDS_Shape& body);
gp_Ax3 faceFrame(const TopoDS_Shape& body, int index);
void validSketch(const modeling::Sketch& sketch);
} // namespace thecad::document::detail
