#pragma once
#include "modeling/assembly/MateTypes.h"
#include "modeling/assembly/MateGeometry.h"
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
    void geometryRatio(double firstRadius, double secondRadius);
    void coordinateFrames(bool available);
    void
    selectionGeometry(modeling::assembly::MateKind,
                      const std::vector<std::pair<std::string, modeling::assembly::MateGeometry>>&);
    modeling::assembly::MateParameters parameters() const;
signals:
    void changed();

private:
    QFormLayout* m_form;
    std::map<std::string, QWidget*> m_fields;
    void visible(const char*, bool);
    bool m_setting = false;
};
} // namespace thecad::ui
