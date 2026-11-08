#pragma once
#include "application/FeatureOperation.h"
#include "modeling/Selection.h"
#include <QWidget>
#include <QFutureWatcher>
#include <optional>
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;
namespace thecad::ui
{
class PropertyManagerPage;
// Owns solid feature forms and their uncommitted document candidate. The
// shell connects value-only selections/previews to the viewport and session.
class SolidFeaturePanel final : public QWidget
{
    Q_OBJECT
public:
    SolidFeaturePanel(document::PartDocument source, document::Feature feature, bool editing,
                      QWidget* parent = nullptr);
    PropertyManagerPage* page() const { return m_page; }
    const document::PartDocument& support() const { return m_support; }
    const std::optional<application::FeatureOperationResult>& candidate() const
    {
        return m_candidate;
    }
    const std::vector<document::ReferenceOption>& options() const { return m_options; }
    bool pickingContours() const { return m_contourMode; }
    bool pickingMirrorPlane() const { return m_feature.kind == document::FeatureKind::Mirror; }
    const modeling::Sketch* sketch() const;
    std::vector<TopoDS_Face> selectedFaces() const;
    std::vector<TopoDS_Shape> selectedReferences() const;
    void pickReference(int option);
    void pickRegion(modeling::Point2);
    void pickTree(const QString& id);
    void preselect(const std::vector<modeling::SelectionRef>&);
    void refreshPreview();
    bool readyToConfirm();
    void stopEdgePreview();
    void focusParameter();
    bool parameterContains(QWidget*) const;
    QDoubleSpinBox* parameter() const { return m_parameter; }
signals:
    void previewChanged();
    void pickingChanged();

private:
    void buildPatternForm(QFormLayout* form);
    void buildSweepForm(QFormLayout* form);
    document::Feature readPattern() const;
    void sketchChanged();
    void setContourMode(bool);
    void rebuildOptions();
    void initializeEdgePreview();
    void queueEdgePreview();
    void startEdgePreview();
    void finishEdgePreview();
    struct EdgePreview {
        std::optional<application::FeatureOperationResult> candidate;
        QString error;
    };
    QTimer* m_edgeTimer = nullptr;
    QFutureWatcher<EdgePreview>* m_edgeWorker = nullptr;
    std::size_t m_edgeRevision = 0;
    std::size_t m_activeEdgeRevision = 0;
    bool m_edgePending = false;
    bool m_edgeInFlight = false;
    bool m_edgePreviewClosed = false;
    bool m_confirmEdgeWhenReady = false;
    document::PartDocument m_source, m_support;
    document::Feature m_feature;
    bool m_editing = false, m_contourMode = false, m_initializing = true;
    PropertyManagerPage* m_page;
    QComboBox *m_sketches = nullptr, *m_axis = nullptr, *m_direction = nullptr;
    QComboBox* m_pathSketch = nullptr;
    QListWidget *m_regions = nullptr, *m_edges = nullptr;
    QDoubleSpinBox* m_parameter = nullptr;
    QComboBox *m_patternReference = nullptr, *m_patternScope = nullptr;
    QListWidget* m_patternSeeds = nullptr;
    QSpinBox* m_instances = nullptr;
    QCheckBox *m_reverse = nullptr, *m_equalSpacing = nullptr;
    QPushButton *m_pickReference = nullptr, *m_pickRegions = nullptr;
    std::vector<modeling::Contour> m_contours;
    std::vector<document::ReferenceOption> m_options;
    std::optional<application::FeatureOperationResult> m_candidate;
};
} // namespace thecad::ui
