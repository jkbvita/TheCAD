#include "ui/MeasureIcons.h"
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>

namespace thecad::ui
{
namespace
{
class InspectionIcon final : public QIconEngine
{
public:
    explicit InspectionIcon(MeasureIcon kind) : m_kind(kind) {}
    QIconEngine* clone() const override { return new InspectionIcon(m_kind); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap image(size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, image.rect(), mode, state);
        return image;
    }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        auto image = pixmap(size * scale, mode, state);
        image.setDevicePixelRatio(scale);
        return image;
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(QRectF(rect).center());
        double scale = std::min(rect.width(), rect.height()) / 24.;
        p->scale(scale, scale);
        p->translate(-12, -12);
        auto color = QApplication::palette().color(
            mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
        p->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        switch (m_kind)
        {
        case MeasureIcon::Circular:
            p->drawEllipse(QPointF(7, 14), 4., 4.);
            p->drawEllipse(QPointF(18, 14), 3., 3.);
            p->drawLine(7, 6, 18, 6);
            p->drawLine(7, 4, 7, 9);
            p->drawLine(18, 4, 18, 10);
            break;
        case MeasureIcon::Units:
            p->drawRect(QRectF(2, 6, 20, 12));
            for (int x = 5; x < 22; x += 3)
                p->drawLine(x, 6, x, x % 2 ? 12 : 10);
            break;
        case MeasureIcon::Points:
            p->drawLine(6, 17, 18, 7);
            p->setBrush(color);
            p->drawEllipse(QPointF(5, 18), 2., 2.);
            p->drawEllipse(QPointF(19, 6), 2., 2.);
            break;
        case MeasureIcon::Frame:
            p->setPen(QPen(QColor(226, 83, 77), 2));
            p->drawLine(9, 16, 21, 16);
            p->setPen(QPen(QColor(74, 181, 85), 2));
            p->drawLine(9, 16, 9, 3);
            p->setPen(QPen(QColor(81, 143, 232), 2));
            p->drawLine(9, 16, 3, 21);
            break;
        case MeasureIcon::Project:
            p->drawPolygon(QPolygonF{{2, 19}, {7, 13}, {22, 13}, {17, 19}});
            p->drawLine(12, 2, 12, 14);
            p->drawLine(9, 10, 12, 14);
            p->drawLine(15, 10, 12, 14);
            break;
        case MeasureIcon::History:
            p->drawArc(QRectF(4, 4, 16, 16), -140 * 16, 300 * 16);
            p->drawLine(3, 10, 4, 15);
            p->drawLine(4, 15, 8, 12);
            p->drawLine(12, 7, 12, 12);
            p->drawLine(12, 12, 16, 14);
            break;
        case MeasureIcon::Display:
            p->drawLine(3, 7, 21, 7);
            p->drawLine(3, 17, 21, 17);
            p->setBrush(color);
            p->drawEllipse(QPointF(9, 7), 2., 2.);
            p->drawEllipse(QPointF(16, 17), 2., 2.);
            break;
        }
        p->restore();
    }

private:
    MeasureIcon m_kind;
};
} // namespace
QIcon measureIcon(MeasureIcon kind)
{
    return QIcon(new InspectionIcon(kind));
}
} // namespace thecad::ui
