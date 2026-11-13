#include "ui/widgets/PropertyManagerControls.h"
#include <QAbstractItemDelegate>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <algorithm>

namespace thecad::ui {
void PropertySectionButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setFont(font()); painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect().adjusted(2,0,-20,0), Qt::AlignLeft | Qt::AlignVCenter, text());
    QStyleOption option; option.initFrom(this); option.rect=QRect(width()-17,(height()-10)/2,10,10);
    style()->drawPrimitive(isChecked()?QStyle::PE_IndicatorArrowUp:QStyle::PE_IndicatorArrowDown,&option,&painter,this);
    painter.setPen(palette().color(QPalette::Mid)); painter.drawLine(0,height()-1,width(),height()-1);
}
QIcon propertyIcon(PropertySymbol symbol, const QColor& color)
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48}) {
        QPixmap pixmap(size, size); pixmap.fill(Qt::transparent);
        QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 20., size / 20.);
        p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (symbol == PropertySymbol::Confirm) { p.drawLine(4, 10, 8, 14); p.drawLine(8, 14, 16, 5); }
        else if (symbol == PropertySymbol::Cancel) { p.drawLine(5, 5, 15, 15); p.drawLine(5, 15, 15, 5); }
        else if (symbol == PropertySymbol::Preview) {
            QPainterPath eye; eye.moveTo(2, 10); eye.quadTo(10, 0, 18, 10); eye.quadTo(10, 20, 2, 10);
            p.drawPath(eye); p.drawEllipse(QPointF(10, 10), 2.5, 2.5);
        } else if (symbol == PropertySymbol::Reverse) {
            p.drawLine(3, 6, 16, 6); p.drawLine(3, 6, 7, 2); p.drawLine(3, 6, 7, 10);
            p.drawLine(4, 14, 17, 14); p.drawLine(17, 14, 13, 10); p.drawLine(17, 14, 13, 18);
        } else if (symbol == PropertySymbol::Direction) {
            p.drawLine(4, 16, 16, 4); p.drawLine(16, 4, 10, 4); p.drawLine(16, 4, 16, 10);
        } else if (symbol == PropertySymbol::Depth || symbol == PropertySymbol::Thickness) {
            p.drawLine(3, 4, 17, 4); p.drawLine(3, 16, 17, 16); p.drawLine(10, 4, 10, 16);
            p.drawLine(10, 4, 7, 7); p.drawLine(10, 4, 13, 7);
            p.drawLine(10, 16, 7, 13); p.drawLine(10, 16, 13, 13);
        } else if (symbol == PropertySymbol::Draft) {
            p.drawLine(3, 16, 17, 16); p.drawLine(7, 16, 11, 3); p.drawLine(11, 3, 15, 16);
            p.drawArc(QRect(7, 8, 8, 8), 0, 60 * 16);
        } else if (symbol == PropertySymbol::Contour) {
            QPolygon polygon; polygon << QPoint(10, 2) << QPoint(18, 10) << QPoint(10, 18) << QPoint(2, 10);
            p.drawPolygon(polygon);
        } else {
            p.drawRect(4, 4, 12, 12); p.drawLine(1, 10, 7, 10); p.drawLine(10, 1, 10, 7);
        }
        p.end(); icon.addPixmap(pixmap);
    }
    return icon;
}

PropertyDimension::PropertyDimension(QWidget* parent) : QDoubleSpinBox(parent)
{
    setDecimals(6); setMinimumWidth(0); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setProperty("propertyField", true); setAccelerated(true);
}
QString PropertyDimension::textFromValue(double value) const
{
    auto format = locale(); format.setNumberOptions(format.numberOptions() | QLocale::OmitGroupSeparator);
    auto text = format.toString(value, 'f', decimals());
    const auto point = text.lastIndexOf(locale().decimalPoint());
    while (point >= 0 && text.size() - point > 3 && text.endsWith(QLatin1Char('0'))) text.chop(1);
    return text;
}
void PropertyDimension::focusInEvent(QFocusEvent* event)
{
    if (activated) activated();
    QDoubleSpinBox::focusInEvent(event);
}

PropertyReference::PropertyReference(QWidget* parent) : QComboBox(parent)
{
    setProperty("referenceSelection", true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumContentsLength(1);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setFocusPolicy(Qt::StrongFocus);
}
void PropertyReference::setPicking(bool active)
{
    setProperty("picking", active); style()->unpolish(this); style()->polish(this); update();
}
void PropertyReference::showPopup() { if (pick) pick(); }
void PropertyReference::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) { setFocus(Qt::MouseFocusReason); showPopup(); event->accept(); }
    else QComboBox::mousePressEvent(event);
}
void PropertyReference::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        setCurrentIndex(0); event->accept();
    } else if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        showPopup(); event->accept();
    } else QWidget::keyPressEvent(event);
}
void PropertyReference::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this); auto* clear = menu.addAction("Clear selection"); clear->setEnabled(currentIndex() > 0);
    if (menu.exec(event->globalPos()) == clear) setCurrentIndex(0);
}

namespace {
class MemberDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const override
    {
        auto visual = options;
        const auto* view = qobject_cast<const QAbstractItemView*>(parent());
        if (view && index != view->currentIndex()) visual.state &= ~QStyle::State_Selected;
        QStyledItemDelegate::paint(painter, visual, index);
    }
};
}
PropertySelectionList::PropertySelectionList(QWidget* parent) : QListWidget(parent)
{
    setProperty("referenceSelection", true); setSelectionMode(QAbstractItemView::MultiSelection);
    setItemDelegate(new MemberDelegate(this)); setMinimumWidth(0);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(this, &QListWidget::itemSelectionChanged, this, [this] { refreshMembers(); });
    setMinimumHeight(54); setMaximumHeight(100);
}
void PropertySelectionList::refreshMembers()
{
    for (int row = 0; row < count(); ++row) item(row)->setHidden(!item(row)->isSelected());
    viewport()->update();
}
void PropertySelectionList::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (auto* item = itemAt(event->pos())) setCurrentItem(item, QItemSelectionModel::NoUpdate);
        setFocus(Qt::MouseFocusReason); if (pick) pick(); event->accept();
    } else QListWidget::mousePressEvent(event);
}
void PropertySelectionList::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (auto* item = currentItem(); item && item->isSelected()) item->setSelected(false);
        event->accept();
    } else if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        if (pick) pick(); event->accept();
    } else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        const int step = event->key() == Qt::Key_Up ? -1 : 1;
        for (int row = currentRow() + step; row >= 0 && row < count(); row += step)
            if (!item(row)->isHidden()) { setCurrentRow(row, QItemSelectionModel::NoUpdate); break; }
        event->accept();
    } else QWidget::keyPressEvent(event);
}
void PropertySelectionList::contextMenuEvent(QContextMenuEvent* event)
{
    if (auto* member = itemAt(event->pos())) setCurrentItem(member, QItemSelectionModel::NoUpdate);
    QMenu menu(this); auto* remove = menu.addAction("Remove selection");
    auto* clear = menu.addAction("Clear selections");
    remove->setEnabled(currentItem() && currentItem()->isSelected()); clear->setEnabled(!selectedItems().isEmpty());
    const auto* action = menu.exec(event->globalPos());
    if (action == remove && currentItem()) currentItem()->setSelected(false);
    else if (action == clear) clearSelection();
}
void PropertySelectionList::paintEvent(QPaintEvent* event)
{
    QListWidget::paintEvent(event);
    if (!selectedItems().isEmpty()) return;
    QPainter painter(viewport()); painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(viewport()->rect().adjusted(6, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, placeholder);
}
}
