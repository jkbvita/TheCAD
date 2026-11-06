#pragma once
#include <QString>
namespace thecad::document
{
class PartDocument;
// The existing format, migration checks and atomic file replacement live here.
class PartFileStore final
{
public:
    static PartDocument load(const QString& path);
    static void save(const PartDocument& document, const QString& path);
};
} // namespace thecad::document
