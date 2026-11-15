#pragma once
#include "document/AssemblyDocument.h"
#include <QWidget>
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QTabBar;
class QVBoxLayout;
class QLineEdit;
namespace thecad::ui
{
class PropertyManagerPage;
class AssemblyMateParameters;
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
    void pick(const document::AssemblyReference&);
    void preselect(const std::vector<document::AssemblyReference>&);
    void refreshPreview();
    bool keepOpen() const;
signals:
    void previewChanged();

private:
    struct Selection
    {
        modeling::assembly::MateRole role;
        QWidget* widget;
    };
    document::AssemblyDocument m_source;
    document::AssemblyMate m_mate, m_proposed;
    std::vector<document::AssemblyReferenceOption> m_options;
    std::vector<Selection> m_selections;
    std::optional<document::AssemblyDocument> m_candidate;
    QByteArray m_previewKey;
    PropertyManagerPage* m_page;
    QComboBox* m_kind;
    QTabBar* m_family;
    QDoubleSpinBox* m_value;
    QLineEdit* m_name;
    QCheckBox *m_flip, *m_keepOpen, *m_multiple;
    AssemblyMateParameters* m_parameters;
    QWidget *m_selectionPanel, *m_conflicts;
    QVBoxLayout* m_selectionLayout;
    QFormLayout* m_form;
    int m_active = 0;
    bool m_refreshing = false, m_initializing = true;
    void activate(int);
    void buildSelections(const std::vector<document::AssemblyMate::Group>& selected = {});
    std::vector<document::AssemblyMate::Group> groups() const;
    void changeFamily();
    void changeKind();
    void keepUnsolved();
    void force();
    int optionIndex(const document::AssemblyReference&) const;
};
} // namespace thecad::ui
