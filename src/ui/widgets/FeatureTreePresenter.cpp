#include "ui/widgets/FeatureTreePresenter.h"
#include "modeling/ReferencePlanes.h"
#include "ui/widgets/CommandBar.h"
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <map>
#include <set>
namespace thecad::ui
{
void FeatureTreePresenter::refresh(const document::PartDocument& document, const QString& name,
                                   const std::string& selectedId,
                                   std::optional<modeling::SketchPlane> selectedPlane)
{
    {
        const QSignalBlocker blocker(m_tree);
        // A shared sketch has several tree entries, all referring to one history ID.
        // Remember its parent too so refreshes retain the selected occurrence.
        const auto* current = m_tree->currentItem();
        const QString currentId = current ? current->data(0, Qt::UserRole).toString() : QString{};
        const QString parentId = current && current->parent()
                                     ? current->parent()->data(0, Qt::UserRole).toString()
                                     : QString{};
        std::map<QString, bool> expanded;
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->childCount())
                expanded[(*it)->data(0, Qt::UserRole).toString()] = (*it)->isExpanded();
        m_tree->clear();
        auto* part = new QTreeWidgetItem(m_tree, {name});
        part->setData(0, Qt::UserRole, "part:");
        part->setIcon(0, commandIcon("Part"));
        part->setExpanded(expanded.count("part:") ? expanded.at("part:") : true);
        for (const auto plane :
             {modeling::SketchPlane::XY, modeling::SketchPlane::XZ, modeling::SketchPlane::YZ})
        {
            const QString key = plane == modeling::SketchPlane::XY   ? "origin:XY"
                                : plane == modeling::SketchPlane::XZ ? "origin:XZ"
                                                                     : "origin:YZ";
            auto* reference = new QTreeWidgetItem(
                part, {QString("%1 Plane").arg(modeling::referencePlaneName(plane))});
            reference->setIcon(0, commandIcon("Plane"));
            reference->setData(0, Qt::UserRole, key);
            reference->setToolTip(
                0, QString("%1 origin reference plane").arg(modeling::referencePlaneName(plane)));
            if (selectedPlane && *selectedPlane == plane)
                m_tree->setCurrentItem(reference);
        }
        auto* origin = new QTreeWidgetItem(part, {"Origin"});
        origin->setData(0, Qt::UserRole, "origin:");
        origin->setIcon(0, commandIcon("Origin"));

        std::map<std::string, const document::Feature*> sketches;
        std::set<std::string> usedSketches;
        for (const auto& feature : document.features())
        {
            if (feature.kind == document::FeatureKind::Sketch)
                sketches.emplace(feature.id, &feature);
            if (document::usesProfile(feature.kind))
                usedSketches.insert(feature.sketchId);
            if (document::isSweepFeature(feature.kind))
                usedSketches.insert(feature.pathSketchId);
        }
        QTreeWidgetItem* selectedItem = nullptr;
        const auto addFeature = [&](QTreeWidgetItem* parent, const document::Feature& feature)
        {
            auto* item = new QTreeWidgetItem(parent, {QString::fromStdString(feature.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(feature.id));
            item->setToolTip(0, QString::fromStdString(feature.name));
            item->setIcon(
                0, commandIcon(
                       feature.kind == document::FeatureKind::ReferenceGeometry
                           ? (feature.datum.kind == modeling::DatumKind::Point  ? "Reference Point"
                              : feature.datum.kind == modeling::DatumKind::Axis ? "Reference Axis"
                                                                                : "Reference Plane")
                       : feature.kind == document::FeatureKind::Sketch ? "Sketch"
                       : feature.kind == document::FeatureKind::Revolve ? "Revolved Boss/Base"
                       : feature.kind == document::FeatureKind::RevolvedCut ? "Revolved Cut"
                       : feature.kind == document::FeatureKind::Fillet ? "Fillet"
                       : feature.kind == document::FeatureKind::Chamfer ? "Chamfer"
                       : feature.kind == document::FeatureKind::LinearPattern ? "Linear Pattern"
                       : feature.kind == document::FeatureKind::CircularPattern ? "Circular Pattern"
                       : feature.kind == document::FeatureKind::Mirror ? "Mirror"
                       : feature.kind == document::FeatureKind::Sweep ? "Swept Boss/Base"
                       : feature.kind == document::FeatureKind::SweptCut ? "Swept Cut"
                       : feature.kind == document::FeatureKind::Cut    ? "Extruded Cut"
                                                                       : "Extrude"));
            if (feature.id == selectedId &&
                (!selectedItem || (currentId == QString::fromStdString(feature.id) &&
                                   parentId == parent->data(0, Qt::UserRole).toString())))
                selectedItem = item;
            return item;
        };
        for (const auto& feature : document.features())
        {
            if (feature.kind == document::FeatureKind::Sketch && usedSketches.count(feature.id))
                continue;
            auto* item = addFeature(part, feature);
            if (document::usesProfile(feature.kind))
            {
                const auto source = sketches.find(feature.sketchId);
                if (source != sketches.end())
                    addFeature(item, *source->second);
            }
            if (document::isSweepFeature(feature.kind))
            {
                const auto path = sketches.find(feature.pathSketchId);
                if (path != sketches.end())
                    addFeature(item, *path->second);
            }
            const auto key = QString::fromStdString(feature.id);
            item->setExpanded(expanded.count(key) ? expanded.at(key) : true);
        }
        if (selectedItem)
            m_tree->setCurrentItem(selectedItem);
        else if (currentId == "part:" && !selectedPlane)
            m_tree->setCurrentItem(part);
        else if (currentId == "origin:" && !selectedPlane)
            m_tree->setCurrentItem(origin);
    }
}
} // namespace thecad::ui
