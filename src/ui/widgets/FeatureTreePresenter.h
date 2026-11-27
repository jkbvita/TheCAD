#pragma once
#include "document/PartDocument.h"
#include <QString>
class QTreeWidget;
namespace thecad::ui
{
class FeatureTreePresenter final
{
public:
    explicit FeatureTreePresenter(QTreeWidget& tree) : m_tree(&tree) {}
    void refresh(const document::PartDocument& document, const QString& name,
                 const std::string& selectedId, std::optional<modeling::SketchPlane> selectedPlane);

private:
    QTreeWidget* m_tree;
};
} // namespace thecad::ui
