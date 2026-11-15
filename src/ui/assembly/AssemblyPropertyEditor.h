#pragma once
#include "document/AssemblyDocument.h"
#include <QWidget>
#include <functional>
class QFormLayout;
class QDoubleSpinBox;
namespace thecad::ui
{
class PropertyManagerPage;
class PropertyReference;
class AssemblyTransformFields final : public QWidget
{
public:
    explicit AssemblyTransformFields(const QString& prefix, QWidget* parent = nullptr);
    gp_Trsf value() const;
    void setValue(const gp_Trsf&);
    std::function<void()> changed;

private:
    QDoubleSpinBox* m_values[6];
    bool m_loading = false;
};
class AssemblyPropertyEditor : public QWidget
{
    Q_OBJECT
public:
    AssemblyPropertyEditor(document::AssemblyDocument, const QString& title,
                           const QString& description, QWidget* parent = nullptr);
    PropertyManagerPage* page() const { return m_page; }
    const std::optional<document::AssemblyDocument>& candidate() const { return m_candidate; }
    virtual std::vector<document::AssemblyReference> references() const = 0;
    virtual void pick(const document::AssemblyReference&);
    virtual void pickComponent(const std::string& id) { pick({id, {}}); }
    virtual void refreshPreview() = 0;
signals:
    void previewChanged();

protected:
    document::AssemblyDocument m_source;
    std::vector<document::AssemblyReferenceOption> m_options;
    std::optional<document::AssemblyDocument> m_candidate, m_lastValid;
    PropertyManagerPage* m_page;
    bool m_building = true;
    PropertyReference*
    referenceField(QFormLayout*, const char* id, const QString& label,
                   const std::function<bool(const document::AssemblyReferenceOption&)>& filter);
    std::optional<document::AssemblyReference> selected(PropertyReference*) const;
    void select(PropertyReference*, const document::AssemblyReference&);
    int optionIndex(const document::AssemblyReference&) const;
    void activate(PropertyReference*);
    void preview(const std::function<void(document::AssemblyDocument&)>&);
    std::vector<PropertyReference*> m_fields;
    PropertyReference* m_active = nullptr;
};
} // namespace thecad::ui
