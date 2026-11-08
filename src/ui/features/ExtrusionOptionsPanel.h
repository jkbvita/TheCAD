#pragma once

#include "document/PartDocument.h"
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace thecad::ui {
class ExtrusionOptionsPanel {
public:
    using Changed = std::function<void()>;
    using Pick = std::function<void(const std::vector<int>&)>;

    ExtrusionOptionsPanel(QVBoxLayout*, QWidget*, const document::Feature&,
                          std::vector<document::ReferenceOption>, Changed, Pick);
    modeling::ExtrudeDefinition definition() const;
    modeling::Direction direction() const;
    void setOpenProfile(bool available, bool onlyOpen, bool canFillet);
    bool thinEnabled() const;
    void picked(int);
    std::vector<TopoDS_Shape> selectedReferences() const;
    const std::vector<document::ReferenceOption>& references() const { return m_options; }

    QFormLayout* contourForm() const { return m_contourForm; }
    QDoubleSpinBox* depth() const { return m_first.depth; }
    QDoubleSpinBox* secondDepth() const { return m_second.depth; }

    static QFormLayout* section(QVBoxLayout*, const QString&, QCheckBox** enabled = nullptr);

private:
    struct ReferenceField {
        QWidget* row = nullptr;
        QComboBox* choices = nullptr;
    };
    struct Leg {
        QFormLayout* form = nullptr;
        QComboBox* end = nullptr;
        QDoubleSpinBox* depth = nullptr;
        ReferenceField target;
        QDoubleSpinBox* offset = nullptr;
        QCheckBox* reverseOffset = nullptr;
        QCheckBox* translate = nullptr;
        QCheckBox* draft = nullptr;
        QDoubleSpinBox* angle = nullptr;
        QCheckBox* outward = nullptr;
    };
    void changed();
    void refresh();
    void fill(ReferenceField&, const std::vector<int>&);
    ReferenceField referenceField(QFormLayout*, const QString&, const char*);
    void startPicking(ReferenceField&);
    void addLeg(Leg&, QFormLayout*, const modeling::ExtrudeLeg&, bool second);
    void refreshLeg(Leg&);
    modeling::ExtrudeLeg readLeg(const Leg&) const;
    std::optional<modeling::GeometryReference> reference(const ReferenceField&) const;
    std::vector<int> choices(modeling::DatumKind, bool faces = false, bool bodies = false) const;
    void restore(const ReferenceField&, const std::optional<modeling::GeometryReference>&);

    QWidget* m_context;
    Changed m_changed;
    Pick m_pick;
    bool m_cut;
    bool m_refreshing = false;
    bool m_openProfile = false;
    bool m_canFillet = false;
    std::vector<document::ReferenceOption> m_options;
    QFormLayout* m_contourForm = nullptr;
    QFormLayout* m_fromForm = nullptr;
    QComboBox* m_from = nullptr;
    QDoubleSpinBox* m_startOffset = nullptr;
    QCheckBox* m_reverseStart = nullptr;
    ReferenceField m_start, m_direction;
    QComboBox* m_reverse = nullptr;
    Leg m_first, m_second;
    QCheckBox* m_secondOn = nullptr;
    QCheckBox* m_thinOn = nullptr;
    QFormLayout* m_thinForm = nullptr;
    QComboBox* m_thinMode = nullptr;
    QDoubleSpinBox* m_wall = nullptr;
    QDoubleSpinBox* m_otherWall = nullptr;
    QCheckBox* m_reverseWall = nullptr;
    QCheckBox* m_autoFillet = nullptr;
    QDoubleSpinBox* m_filletRadius = nullptr;
    QCheckBox* m_cap = nullptr;
    QDoubleSpinBox* m_capThickness = nullptr;
    QCheckBox* m_merge = nullptr;
    QCheckBox* m_flip = nullptr;
    QCheckBox* m_scopeOn = nullptr;
    QListWidget* m_scope = nullptr;
    ReferenceField* m_picking = nullptr;
    bool m_pickingScope = false;
};
}
