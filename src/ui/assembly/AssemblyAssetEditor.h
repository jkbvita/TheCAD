#pragma once
#include "ui/assembly/AssemblyPropertyEditor.h"
class QComboBox;
class QListWidget;
class QLineEdit;
class QCheckBox;
namespace thecad::ui
{
class AssemblyAssetEditor final : public AssemblyPropertyEditor
{
    Q_OBJECT
public:
    AssemblyAssetEditor(document::AssemblyDocument, const std::string& componentId = {},
                        QWidget* parent = nullptr);
    std::vector<document::AssemblyReference> references() const override;
    void pick(const document::AssemblyReference&) override;
    void pickComponent(const std::string&) override;
    void refreshPreview() override;

private:
    struct Draft
    {
        document::PublishedConnector connector;
        int point = -1, direction = -1;
    };
    QComboBox* m_owner;
    QListWidget* m_list;
    QLineEdit* m_name;
    QCheckBox* m_grounded;
    PropertyReference *m_point, *m_direction, *m_ground;
    AssemblyTransformFields* m_offset;
    std::vector<Draft> m_connectors;
    int m_current = -1;
    void loadOwner();
    void saveConnector();
    void loadConnector();
    void rebuildConnectors(int current = 0);
    void refillReferences();
};
} // namespace thecad::ui
