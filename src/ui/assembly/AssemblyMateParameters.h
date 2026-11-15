#pragma once
#include "modeling/assembly/MateTypes.h"
#include <QWidget>
#include <map>
class QFormLayout;
namespace thecad::ui
{
class AssemblyMateParameters final : public QWidget
{
    Q_OBJECT
public:
    explicit AssemblyMateParameters(QWidget* parent = nullptr);
    void configure(modeling::assembly::MateKind);
    void setParameters(const modeling::assembly::MateParameters&);
    modeling::assembly::MateParameters parameters() const;
signals:
    void changed();

private:
    QFormLayout* m_form;
    std::map<std::string, QWidget*> m_fields;
    void visible(const char*, bool);
};
} // namespace thecad::ui
