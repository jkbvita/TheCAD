#pragma once
#include "ui/assembly/AssemblyPropertyEditor.h"
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
namespace thecad::ui
{
class PropertySelectionList;
class AssemblyBeltEditor final : public AssemblyPropertyEditor
{
    Q_OBJECT
public:
    AssemblyBeltEditor(document::AssemblyDocument,
                       const std::optional<document::AssemblyBelt>& editing = {},
                       QWidget* parent = nullptr);
    std::vector<document::AssemblyReference> references() const override;
    void pick(const document::AssemblyReference&) override;
    void refreshPreview() override;
    const document::AssemblyBelt& definition() const { return m_belt; }

private:
    struct Member
    {
        document::AssemblyReference reference;
        double diameter = 0;
        int side = 1;
    };
    document::AssemblyBelt m_belt;
    std::vector<Member> m_members;
    PropertySelectionList* m_list;
    PropertyReference* m_location;
    QLineEdit* m_name;
    QDoubleSpinBox *m_diameter, *m_length, *m_thickness;
    QCheckBox *m_flip, *m_driving, *m_useThickness, *m_engage, *m_createPart, *m_lockPart;
    QLabel* m_measured;
    QPushButton *m_up, *m_down;
    void rebuildMembers(int current = -1);
    void loadMember();
    void memberChanged();
};
} // namespace thecad::ui
