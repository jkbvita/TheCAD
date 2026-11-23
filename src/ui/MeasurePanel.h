#pragma once
#include "ui/MeasurePresentation.h"
#include <QDockWidget>
#include <QTimer>
#include <QFutureWatcher>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QListWidget;
class QLabel;
class QTableWidget;
class QPushButton;

namespace thecad::ui
{
struct MeasureValue
{
    QString name, display, number;
};
struct MeasureUnits
{
    QString length = "mm", secondary, angle = "°";
    int decimals = 3, angleDecimals = 3;
    bool scientific = false;
    int fractionDenominator = 0;
    bool nearestFraction = true;
    MeasureValue format(QString name, double value, int power = 1) const;
};
class MeasurePanel final : public QDockWidget
{
    Q_OBJECT
public:
    explicit MeasurePanel(QWidget* parent);
    QSize sizeHint() const override;
    void resizeEvent(QResizeEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void resetSession();
    std::optional<modeling::Measurement> measurement;
    void updateProjectedArea(const TopoDS_Shape&, const gp_Pln&);
    bool computingArea() const { return m_areaPending; }
    std::optional<double> projectedArea;
    void setFrames(const std::vector<std::pair<QString, gp_Ax3>>& frames);
    void setPicks(const std::vector<MeasurePick>& picks);
    void setReading(const std::vector<MeasureValue>& rows, const QString& note);
    void saveHistory(const QString& document);
    MeasureUnits units() const;
    gp_Ax3 frame() const;
    void setArcAvailability(bool center, bool extremes, bool custom);
    void setProjectionPlane(gp_Pln plane, const QString& label);
    bool pinned() const;
    bool showXYZ() const;
    bool callouts() const;
    int resultTextPixels() const;
    QComboBox *circle, *firstCondition, *secondCondition, *projection;
    std::optional<gp_Pln> selectedPlane;
    QString readingText() const;
signals:
    void closed();
    void optionsChanged();
    void pointModeChanged(bool);
    void projectionPick(bool);
    void removePick(int);
    void clearPicks();

private:
    TopoDS_Shape m_areaShape;
    std::optional<gp_Dir> m_areaDirection;
    bool m_areaPending = false;
    QFutureWatcher<std::optional<double>> m_areaJob;
    std::vector<gp_Ax3> m_frames;
    QComboBox *m_frame, *m_unit, *m_dual, *m_angle, *m_copyMode, *m_fractions;
    QSpinBox *m_digits, *m_angleDigits;
    QCheckBox *m_scientific, *m_xyz, *m_visible, *m_roundFraction;
    QCheckBox *m_pin, *m_expand;
    QToolButton *m_point, *m_controlsToggle;
    QPushButton* m_pickPlane;
    QListWidget* m_selection;
    QTableWidget* m_values;
    QLabel* m_note;
    QWidget* m_details;
    QWidget* m_commands;
};
} // namespace thecad::ui
