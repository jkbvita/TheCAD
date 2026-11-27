#pragma once
#include "ui/assembly/AssemblyPropertyEditor.h"
class QLabel;
class QCheckBox;
namespace thecad::ui
{
class AssemblyGroundEditor final : public AssemblyPropertyEditor
{
    Q_OBJECT
public:
    AssemblyGroundEditor(document::AssemblyDocument, QWidget* parent = nullptr);
    std::vector<document::AssemblyReference> references() const override;
    void refreshPreview() override;

private:
    PropertyReference* m_plane;
    bool m_remove = false;
};
class AssemblyInPlaceEditor : public AssemblyPropertyEditor
{
    Q_OBJECT
public:
    AssemblyInPlaceEditor(document::AssemblyDocument,
                          const std::optional<document::InPlacePart>& editing = {},
                          QWidget* parent = nullptr);
    std::vector<document::AssemblyReference> references() const override;
    void refreshPreview() override;
    bool setTemplateFile(const QString& path);
    const document::InPlacePart& definition() const { return m_definition; }

protected:
    virtual QString chooseTemplatePath();

private:
    document::InPlacePart m_definition;
    std::optional<document::PartDocument> m_part;
    PropertyReference* m_support;
    AssemblyTransformFields* m_offset;
    QLabel* m_template;
    QCheckBox* m_linked;
};
} // namespace thecad::ui
