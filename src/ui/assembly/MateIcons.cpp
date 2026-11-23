#include "ui/assembly/MateIcons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
QIcon assemblyMateIcon(modeling::assembly::MateKind kind)
{
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    const QColor main("#D9E2EC"), accent("#68B7F5"), secondary("#78D6C2");
    QPen pen(main, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const auto line = [&](QPointF a, QPointF b, QColor color)
    {
        pen.setColor(color);
        painter.setPen(pen);
        painter.drawLine(a, b);
    };
    const auto dot = [&](QPointF p, QColor color)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(p, 2.2, 2.2);
        painter.setBrush(Qt::NoBrush);
    };
    const auto arrow = [&](QPointF tip, QPointF direction, QColor color)
    {
        direction = direction / std::max(1e-9, std::hypot(direction.x(), direction.y()));
        const QPointF side(-direction.y(), direction.x());
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(tip - direction * 4 + side * 2.1);
        path.lineTo(tip - direction * 4 - side * 2.1);
        path.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(path);
        painter.setBrush(Qt::NoBrush);
    };
    switch (kind)
    {
    case modeling::assembly::MateKind::Coincident:
        painter.drawEllipse(QRectF(3.8, 6.4, 8.5, 8.5));
        painter.drawEllipse(QRectF(7.7, 10.4, 8.5, 8.5));
        dot({10, 14}, accent);
        break;
    case modeling::assembly::MateKind::Concentric:
        painter.drawEllipse(QRectF(2.6, 2.6, 14.8, 14.8));
        painter.drawEllipse(QRectF(6.5, 6.5, 7, 7));
        dot({10, 10}, accent);
        break;
    case modeling::assembly::MateKind::Parallel:
        line({3, 6}, {17, 6}, accent);
        line({3, 13}, {17, 13}, main);
        break;
    case modeling::assembly::MateKind::Perpendicular:
        line({4, 4}, {4, 16}, accent);
        line({4, 16}, {16, 16}, main);
        line({8, 12}, {8, 8}, main);
        line({8, 8}, {12, 8}, main);
        break;
    case modeling::assembly::MateKind::Tangent:
        painter.drawEllipse(QRectF(2.8, 3, 12, 12));
        line({8.8, 15}, {18, 15}, accent);
        dot({8.8, 15}, secondary);
        break;
    case modeling::assembly::MateKind::Lock:
        painter.drawRoundedRect(QRectF(4.3, 8.5, 11.4, 8), 1.3, 1.3);
        painter.drawArc(QRectF(6.6, 3, 6.8, 10), 0, 180 * 16);
        dot({10, 12}, accent);
        break;
    case modeling::assembly::MateKind::Distance:
    case modeling::assembly::MateKind::LimitDistance:
        line({4, 3}, {4, 17}, main);
        line({16, 3}, {16, 17}, main);
        line({6, 10}, {14, 10}, accent);
        arrow({6, 10}, {-1, 0}, accent);
        arrow({14, 10}, {1, 0}, accent);
        break;
    case modeling::assembly::MateKind::Angle:
    case modeling::assembly::MateKind::LimitAngle:
        line({4, 15}, {17, 15}, main);
        line({4, 15}, {13, 5}, accent);
        painter.drawArc(QRectF(5.8, 6.8, 12, 12), 0, 135 * 16);
        arrow({14.3, 8.2}, {0.7, -1}, accent);
        break;
    default:
        // Advanced/mechanical mates use a distinct pair-and-link glyph.
        painter.drawRoundedRect(QRectF(2.8, 5, 6.7, 10), 2.5, 2.5);
        painter.drawRoundedRect(QRectF(10.5, 5, 6.7, 10), 2.5, 2.5);
        line({8.5, 10}, {11.5, 10}, accent);
        dot({10, 10}, secondary);
        break;
    }
    return QIcon(pixmap);
}
} // namespace thecad::ui
