#pragma once
#include "document/PartDocument.h"
#include "modeling/assembly/MateSolver.h"
#include <memory>

namespace thecad::document
{
using AssemblyMateKind = modeling::assembly::MateKind;
struct AssemblySourceReference
{
    std::string label;
    modeling::GeometryReference reference;
    modeling::DatumGeometry geometry;
    TopoDS_Shape selectionShape;
    modeling::assembly::MateGeometry mateGeometry;
};
struct AssemblyComponent
{
    std::string id, name;
    QString path, digest;
    std::shared_ptr<const PartDocument> part;
    // Immutable local picking data shared by all instances of this source.
    std::shared_ptr<const std::vector<AssemblySourceReference>> references;
    gp_Trsf placement;
    bool fixed = false, visible = true;
    double size = 100;
};
struct AssemblyReference
{
    std::string componentId;
    modeling::GeometryReference geometry;
};
struct AssemblyMate
{
    std::string id, name;
    AssemblyMateKind kind = AssemblyMateKind::Coincident;
    AssemblyReference first, second;
    double value = 0;
    int alignment = 1;
    struct Group
    {
        std::string role;
        std::vector<AssemblyReference> references;
    };
    struct Definition
    {
        std::vector<Group> groups;
        modeling::assembly::MateParameters parameters;
        modeling::assembly::MateMotionState motion;
    };
    std::optional<Definition> definition;
    modeling::assembly::MateStatus status = modeling::assembly::MateStatus::Active;
    std::string diagnostic;
};
struct AssemblyReferenceOption
{
    QString label;
    AssemblyReference reference;
    modeling::DatumGeometry geometry;
    TopoDS_Shape shape;
    modeling::assembly::MateGeometry mateGeometry;
};
class AssemblyFileStore;
class AssemblyDocument final
{
    friend class AssemblyFileStore;

public:
    static AssemblyDocument load(const QString& path);
    void save(const QString& path) const;
    const std::vector<AssemblyComponent>& components() const { return m_components; }
    const std::vector<AssemblyMate>& mates() const { return m_mates; }
    const AssemblyComponent& component(const std::string& id) const;
    const modeling::assembly::MateSolution& solution() const { return m_solution; }
    bool sourcesChangedOnLoad() const { return m_sourcesChanged; }
    std::string insertComponent(const QString& path, const gp_Trsf& placement = {},
                                std::optional<bool> fixed = {});
    void setPlacement(const std::string& id, const gp_Trsf& placement);
    void setFixed(const std::string& id, bool fixed);
    void setVisible(const std::string& id, bool visible);
    std::string setMate(AssemblyMate mate);
    std::string keepUnsolvedMate(AssemblyMate mate);
    std::string forceMate(AssemblyMate mate, const std::vector<std::string>& affectedMates);
    void setMateStatus(const std::vector<std::string>& ids, modeling::assembly::MateStatus status);
    void deleteMates(const std::vector<std::string>& ids);
    std::vector<AssemblyMate> componentDependents(const std::vector<std::string>& ids) const;
    void deleteComponents(const std::vector<std::string>& ids, bool deleteDependentMates = false);
    void reloadSources();
    modeling::DatumGeometry referenceGeometry(const AssemblyReference&, bool world = true) const;
    modeling::assembly::MateGeometry mateGeometry(const AssemblyReference&) const;
    std::vector<AssemblyReferenceOption> referenceOptions(const std::string& componentId) const;
    QByteArray stateKey() const;

private:
    static AssemblyComponent loadSource(const QString& path);
    void solve(const std::string& driven = {});
    std::vector<AssemblyComponent> m_components;
    std::vector<AssemblyMate> m_mates;
    modeling::assembly::MateSolution m_solution;
    bool m_sourcesChanged = false;
};
} // namespace thecad::document
