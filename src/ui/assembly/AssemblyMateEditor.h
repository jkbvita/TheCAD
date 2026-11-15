#pragma once
#include "document/AssemblyDocument.h"
#include <QWidget>
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
namespace thecad::ui
{
class PropertyManagerPage;
class AssemblyMateEditor final : public QWidget
{
    Q_OBJECT
public:
    AssemblyMateEditor(document::AssemblyDocument source,
                       const std::optional<document::AssemblyMate>& editing = {},
                       QWidget* parent = nullptr);
    PropertyManagerPage* page() const { return m_page; }
    const std::optional<document::AssemblyDocument>& candidate() const { return m_candidate; }
    const std::vector<document::AssemblyReferenceOption>& options() const { return m_options; }
    std::vector<document::AssemblyReference> references() const;
    void pick(const document::AssemblyReference& reference);
    void refreshPreview();
signals:
    void previewChanged();

private:
    document::AssemblyDocument m_source;
    document::AssemblyMate m_mate;
    std::vector<document::AssemblyReferenceOption> m_options;
    std::optional<document::AssemblyDocument> m_candidate;
    PropertyManagerPage* m_page;
    QComboBox *m_first, *m_second, *m_kind;
    QDoubleSpinBox* m_value;
    QCheckBox* m_flip;
    QFormLayout* m_form;
    int m_active = 0;
    bool m_refreshing = false, m_initializing = true;
    void activate(int index);
    int optionIndex(const document::AssemblyReference&) const;
};
} // namespace thecad::ui
