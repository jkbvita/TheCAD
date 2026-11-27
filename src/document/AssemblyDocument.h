#pragma once
#include "document/PartDocument.h"
#include "modeling/assembly/MateSolver.h"
#include "modeling/assembly/BeltGeometry.h"
#include <memory>
#include <map>

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
    QByteArray
        embeddedPart; // Assembly-owned generated part; linked source files are never rewritten.
    TopoDS_Shape displayShape;
    const TopoDS_Shape& shape() const
    {
        return displayShape.IsNull() ? part->shape() : displayShape;
    }
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
    std::string connectorId;
};
struct PublishedConnector
{
    std::string id, name;
    modeling::GeometryReference point, direction;
    gp_Trsf offset;
};
struct PublishedAsset
{
    std::string componentId;
    std::optional<modeling::GeometryReference> groundPlane;
    std::vector<PublishedConnector> connectors;
    bool grounded = true;
};
struct AssemblyBelt
{
    std::string id, name, componentId;
    std::vector<AssemblyReference> pulleys;
    std::vector<double> diameters; // Zero uses the referenced circular geometry.
    std::vector<int> sides;
    std::optional<AssemblyReference> location;
    bool engaged = true, driving = false, useThickness = false, createPart = false, lockPart = true;
    double length = 100, thickness = 1;
};
struct InPlacePart
{
    std::string id, componentId;
    AssemblyReference support;
    gp_Trsf offset;
    bool linked = true;
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
    friend class AssemblyWorkflowStore;

public:
    static AssemblyDocument load(const QString& path);
    void save(const QString& path) const;
    const std::vector<AssemblyComponent>& components() const { return m_components; }
    const std::vector<AssemblyMate>& mates() const { return m_mates; }
    const std::vector<PublishedAsset>& assets() const { return m_assets; }
    const std::vector<AssemblyBelt>& belts() const { return m_belts; }
    const std::vector<InPlacePart>& inPlaceParts() const { return m_inPlaces; }
    const std::optional<AssemblyReference>& groundPlane() const { return m_groundPlane; }
    const AssemblyComponent& component(const std::string& id) const;
    bool componentMovable(const std::string& id) const;
    const modeling::assembly::MateSolution& solution() const { return m_solution; }
    bool sourcesChangedOnLoad() const { return m_sourcesChanged; }
    std::string insertComponent(const QString& path, const gp_Trsf& placement = {},
                                std::optional<bool> fixed = {});
    void setPlacement(const std::string& id, const gp_Trsf& placement);
    void dragRotation(const std::string& id, const gp_Pnt& localPivot,
                      const gp_Quaternion& rotation);
    void dragPoint(const std::string& id, const gp_Pnt& local, const gp_Pnt& target,
                   const gp_Dir& viewNormal, double tolerance, double rotationDeadband = 0);
    void setFixed(const std::string& id, bool fixed);
    void setVisible(const std::string& id, bool visible);
    std::string setMate(AssemblyMate mate);
    std::string keepUnsolvedMate(AssemblyMate mate);
    std::string forceMate(AssemblyMate mate, const std::vector<std::string>& affectedMates);
    void setMateStatus(const std::vector<std::string>& ids, modeling::assembly::MateStatus status);
    void deleteMates(const std::vector<std::string>& ids);
    std::vector<AssemblyMate> componentDependents(const std::vector<std::string>& ids) const;
    std::vector<std::string> componentDependencyNames(const std::vector<std::string>& ids) const;
    void deleteComponents(const std::vector<std::string>& ids, bool deleteDependentMates = false);
    void reloadSources();
    void publishAsset(PublishedAsset asset);
    void setGroundPlane(std::optional<AssemblyReference> plane);
    std::string setBelt(AssemblyBelt belt);
    void deleteBelt(const std::string& id, bool deleteDependentMates = false);
    modeling::assembly::BeltGeometry beltGeometry(const AssemblyBelt&) const;
    std::string createInPlacePart(const PartDocument& part, const AssemblyReference& support,
                                  const gp_Trsf& offset = {});
    // Associate an independently saved part instance with stable assembly
    // reference geometry. The part definition remains owned by its .thepart.
    std::string linkInPlacePart(const std::string& componentId,
                                const AssemblyReference& support,
                                const gp_Trsf& offset = {});
    std::string setInPlacePart(InPlacePart definition, const PartDocument& part);
    void updateOwnedPart(const std::string& componentId, const PartDocument& part);
    bool snapMagnetic(const std::string& componentId, double distance, int movingConnector = -1,
                      int stationaryConnector = -1, bool locked = false);
    modeling::DatumGeometry referenceGeometry(const AssemblyReference&, bool world = true) const;
    modeling::assembly::MateGeometry mateGeometry(const AssemblyReference&) const;
    std::vector<AssemblyReferenceOption> referenceOptions(const std::string& componentId) const;
    QByteArray stateKey() const;

private:
    void applyDrag(const std::string& id, modeling::assembly::ComponentDragTarget target);
    static AssemblyComponent loadSource(const QString& path);
    static AssemblyComponent ownedSource(const QByteArray& data);
    static void prepareSource(AssemblyComponent&);
    bool updateGeneratedParts();
    void updateGroundMates();
    modeling::assembly::MateGeometry connectorGeometry(const AssemblyReference&) const;
    void solve(const std::string& driven = {}, int generatedPass = 0,
               const modeling::assembly::ComponentDragTarget* drag = nullptr);
    std::vector<AssemblyComponent> m_components;
    std::vector<AssemblyMate> m_mates;
    std::vector<PublishedAsset> m_assets;
    std::vector<AssemblyBelt> m_belts;
    std::vector<InPlacePart> m_inPlaces;
    std::optional<AssemblyReference> m_groundPlane;
    std::map<std::string, QByteArray>
        m_beltLayoutKeys; // Transient, copied with candidate snapshots.
    modeling::assembly::MateSolution m_solution;
    bool m_sourcesChanged = false;
};
} // namespace thecad::document
