#pragma once
#include "ui/assembly/AssemblyPropertyEditor.h"
class QCheckBox;
namespace thecad::ui
{
class AssemblyMagneticEditor final : public AssemblyPropertyEditor
{
    Q_OBJECT
public:
    AssemblyMagneticEditor(document::AssemblyDocument, const document::AssemblyMate&,
                           QWidget* parent = nullptr);
    std::vector<document::AssemblyReference> references() const override;
    void refreshPreview() override;

private:
    document::AssemblyMate m_mate;
    PropertyReference *m_first, *m_second;
    QCheckBox* m_locked;
};
} // namespace thecad::ui
