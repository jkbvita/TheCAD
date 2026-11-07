#pragma once
#include "document/PartDocument.h"
#include "modeling/FeaturePreview.h"
namespace thecad::application
{
struct FeatureOperationResult
{
    document::PartDocument document;
    std::string featureId;
    modeling::MaterialChanges changes;
    TopoDS_Shape previewShape;
};
class FeatureOperation final
{
public:
    static FeatureOperationResult preview(const document::PartDocument& source,
                                          const document::Feature& feature,
                                          const modeling::RegionReferences& regions, bool editing);
};
} // namespace thecad::application
