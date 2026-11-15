#include "ui/assembly/AssemblyWindow.h"
#include "ui/widgets/CommandBar.h"
#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
namespace thecad::ui
{
namespace
{
constexpr int kindRole = Qt::UserRole + 1, referenceRole = Qt::UserRole + 2;
bool geometryEqual(const modeling::GeometryReference& a, const modeling::GeometryReference& b)
{
    return a.source == b.source &&
           (!b.topologyName.empty() ? a.topologyName == b.topologyName
                                    : a.featureId == b.featureId && a.entityId == b.entityId &&
                                          a.role == b.role && a.index == b.index);
}
} // namespace
void AssemblyWindow::addWorkflowTree(QTreeWidgetItem* root)
{
    const auto addReference =
        [&](QTreeWidgetItem* parent, const document::AssemblyReferenceOption& option,
            const QString& label, const QString& key, const char* kind = "reference")
    {
        const int index = int(m_treeReferences.size());
        m_treeReferences.push_back(option);
        auto* item = new QTreeWidgetItem(parent, {label});
        item->setIcon(0, commandIcon("Reference Geometry"));
        item->setData(0, Qt::UserRole, key);
        item->setData(0, kindRole, kind);
        item->setData(0, referenceRole, index);
    };
    if (document().groundPlane())
    {
        const auto& ref = *document().groundPlane();
        for (const auto& option : document().referenceOptions(ref.componentId))
            if (option.reference.connectorId == ref.connectorId &&
                geometryEqual(option.reference.geometry, ref.geometry))
            {
                addReference(root, option, "Ground Plane", "assembly-ground-plane", "ground");
                break;
            }
    }
    for (const auto& asset : document().assets())
    {
        if (asset.connectors.empty() && !asset.groundPlane)
            continue;
        QTreeWidgetItem* parent = nullptr;
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->data(0, kindRole).toString() == "component" &&
                (*it)->data(0, Qt::UserRole).toString() ==
                    QString::fromStdString(asset.componentId))
            {
                parent = *it;
                break;
            }
        if (!parent)
            continue;
        auto* folder = new QTreeWidgetItem(parent, {"Published References"});
        folder->setData(0, Qt::UserRole, "published-" + QString::fromStdString(asset.componentId));
        folder->setData(0, kindRole, "asset");
        folder->setData(0, referenceRole, QString::fromStdString(asset.componentId));
        for (const auto& option : document().referenceOptions(asset.componentId))
        {
            if (!option.reference.connectorId.empty())
                addReference(folder, option, option.label.section(" / ", -1),
                             "published-ref-" +
                                 QString::fromStdString(asset.componentId + "-" +
                                                        option.reference.connectorId));
            else if (asset.groundPlane &&
                     geometryEqual(option.reference.geometry, *asset.groundPlane))
                addReference(folder, option, "Ground Plane",
                             "published-ground-" + QString::fromStdString(asset.componentId));
        }
    }
    for (const auto& belt : document().belts())
    {
        auto* feature =
            new QTreeWidgetItem(root, {QString::fromStdString(belt.name) +
                                       QString(" (%1 mm)").arg(belt.length, 0, 'f', 3)});
        feature->setIcon(0, commandIcon("Belt/Chain"));
        feature->setData(0, Qt::UserRole, QString::fromStdString(belt.id));
        feature->setData(0, kindRole, "belt");
        feature->setExpanded(true);
        feature->setToolTip(0, belt.engaged ? "Engaged belt · double-click to edit"
                                            : "Disengaged belt · double-click to edit");
        if (!belt.componentId.empty())
            for (int i = 0; i < root->childCount(); ++i)
            {
                auto* child = root->child(i);
                if (child->data(0, kindRole).toString() == "component" &&
                    child->data(0, Qt::UserRole).toString() ==
                        QString::fromStdString(belt.componentId))
                {
                    feature->addChild(root->takeChild(i));
                    break;
                }
            }
    }
}
void AssemblyWindow::workflowContextMenu(QMenu& menu, QTreeWidgetItem* item)
{
    const auto kind = item->data(0, kindRole).toString();
    const auto id = item->data(0, Qt::UserRole).toString().toStdString();
    if (kind == "belt")
    {
        auto* edit = menu.addAction("Edit Belt/Chain");
        connect(edit, &QAction::triggered, this, [this, id] { startWorkflow(Workflow::Belt, id); });
    }
    if (kind == "ground")
    {
        auto* edit = menu.addAction("Edit Ground Plane");
        connect(edit, &QAction::triggered, this, [this] { startWorkflow(Workflow::Ground); });
    }
    if (kind == "asset")
    {
        const auto owner = item->data(0, referenceRole).toString().toStdString();
        auto* edit = menu.addAction("Edit published references");
        connect(edit, &QAction::triggered, this,
                [this, owner] { startWorkflow(Workflow::Asset, owner); });
    }
    if (kind == "component")
    {
        for (const auto& part : document().inPlaceParts())
            if (part.componentId == id)
            {
                auto* edit = menu.addAction("Edit in-context part");
                connect(edit, &QAction::triggered, this,
                        [this, key = part.id] { startWorkflow(Workflow::InPlace, key); });
            }
        if (!document().component(id).embeddedPart.isEmpty())
        {
            auto* exportPart = menu.addAction("Save owned part as…");
            connect(exportPart, &QAction::triggered, this,
                    [this, id]
                    {
                        auto path = QFileDialog::getSaveFileName(
                            this, "Save owned part",
                            QString::fromStdString(document().component(id).name) + ".thepart",
                            "TheCAD Part (*.thepart)");
                        if (path.isEmpty())
                            return;
                        if (!path.endsWith(".thepart", Qt::CaseInsensitive))
                            path += ".thepart";
                        try
                        {
                            document().component(id).part->save(path);
                        }
                        catch (const std::exception& e)
                        {
                            reportError(QString::fromUtf8(e.what()));
                        }
                    });
        }
    }
    if (kind == "mate")
        for (const auto& mate : document().mates())
            if (mate.id == id && mate.kind == document::AssemblyMateKind::Magnetic &&
                mate.definition)
            {
                const bool locked = mate.definition->parameters.magnetic.locked;
                auto* lock = menu.addAction(locked ? "Unlock magnetic mate" : "Lock magnetic mate");
                connect(lock, &QAction::triggered, this,
                        [this, mate, locked]
                        {
                            mutate(
                                [&](auto& doc)
                                {
                                    auto updated = mate;
                                    updated.definition->parameters.magnetic.locked = !locked;
                                    doc.setMate(updated);
                                });
                        });
            }
}
} // namespace thecad::ui
