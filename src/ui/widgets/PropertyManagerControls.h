#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QListWidget>
#include <QToolButton>
#include <functional>

namespace thecad::ui {
enum class PropertySymbol { Confirm, Cancel, Preview, Reverse, Direction, Depth, Draft, Reference, Contour, Thickness };
QIcon propertyIcon(PropertySymbol symbol, const QColor& color);
class PropertySectionButton final : public QToolButton {
public:
    using QToolButton::QToolButton;
protected:
    void paintEvent(QPaintEvent* event) override;
};

class PropertyDimension final : public QDoubleSpinBox {
public:
    explicit PropertyDimension(QWidget* parent = nullptr);
    std::function<void()> activated;
protected:
    QString textFromValue(double value) const override;
    void focusInEvent(QFocusEvent* event) override;
};

// The combo model retains the valid references and their persistent IDs.
// Its presentation is a selection box: clicking it starts geometric picking.
class PropertyReference final : public QComboBox {
public:
    explicit PropertyReference(QWidget* parent = nullptr);
    std::function<void()> pick;
    void setPicking(bool active);
    void showPopup() override;
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
};

// The model stores the available contours; only chosen members are displayed.
// Clicking a row highlights it. Delete removes membership rather than geometry.
class PropertySelectionList final : public QListWidget {
public:
    explicit PropertySelectionList(QWidget* parent = nullptr);
    std::function<void()> pick;
    QString placeholder;
    void refreshMembers();
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
};
}
