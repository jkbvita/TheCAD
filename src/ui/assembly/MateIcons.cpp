#include "ui/assembly/MateIcons.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace thecad::ui
{
QIcon assemblyMateIcon(modeling::assembly::MateKind kind)
{
    // Original geometry symbols; render at twice the logical size for HiDPI menus.
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = QApplication::palette().color(QPalette::WindowText);
    const QColor blue("#398FCB"), teal("#22A6A6");
    QColor fill = blue; fill.setAlpha(65);
    QColor pale = ink; pale.setAlpha(105);
    const auto pen = [&](QColor color, qreal width = 1.35, Qt::PenStyle style = Qt::SolidLine) {
        p.setPen(QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
    };
    const auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2, QColor color = {}) {
        pen(color.isValid() ? color : ink);
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto ring = [&](qreal x, qreal y, qreal radius, QColor color = {}) {
        pen(color.isValid() ? color : ink);
        p.drawEllipse(QPointF(x, y), radius, radius);
    };
    const auto dot = [&](qreal x, qreal y, QColor color = {}) {
        p.setPen(Qt::NoPen); p.setBrush(color.isValid() ? color : blue);
        p.drawEllipse(QPointF(x, y), 1.25, 1.25);
        p.setBrush(Qt::NoBrush);
    };
    const auto polygon = [&](std::initializer_list<QPointF> points, QColor color = {}, bool filled = true) {
        pen(color.isValid() ? color : ink);
        if (filled) p.setBrush(fill);
        p.drawPolygon(QPolygonF(points));
        p.setBrush(Qt::NoBrush);
    };
    const auto arrow = [&](QPointF tip, QPointF direction) {
        direction /= std::max(0.001, std::hypot(direction.x(), direction.y()));
        const QPointF side(-direction.y(), direction.x());
        QPainterPath head;
        head.moveTo(tip);
        head.lineTo(tip - direction * 3 + side * 1.4);
        head.lineTo(tip - direction * 3 - side * 1.4);
        head.closeSubpath();
        p.setPen(Qt::NoPen); p.setBrush(blue); p.drawPath(head); p.setBrush(Qt::NoBrush);
    };
    const auto lock = [&] {
        pen(ink);
        p.drawRoundedRect(QRectF(6, 9, 8, 7), 1.1, 1.1);
        p.drawArc(QRectF(7.5, 4, 5, 9), 0, 180 * 16);
        dot(10, 12.5);
    };
    using K = modeling::assembly::MateKind;
    switch (kind)
    {
    case K::Coincident:
        polygon({{2.5, 4.5}, {13, 4.5}, {17.5, 9}, {7, 9}}, pale);
        polygon({{7, 9}, {17.5, 9}, {13, 14}, {2.5, 14}}, ink);
        line(7, 9, 17.5, 9, blue);
        break;
    case K::Concentric:
        ring(10, 10, 7.2, ink); ring(10, 10, 4.4, blue);
        line(10, 1.5, 10, 5, pale); line(10, 15, 10, 18.5, pale);
        line(1.5, 10, 5, 10, pale); line(15, 10, 18.5, 10, pale);
        dot(10, 10);
        break;
    case K::Distance:
    case K::LimitDistance:
        line(3.5, 4, 3.5, 16); line(16.5, 4, 16.5, 16);
        line(6, 10, 14, 10, blue); arrow({6, 10}, {-1, 0}); arrow({14, 10}, {1, 0});
        if (kind == K::LimitDistance) {
            line(5.5, 4, 8, 4, teal); line(12, 16, 14.5, 16, teal);
        }
        break;
    case K::Angle:
    case K::LimitAngle:
        line(3, 16, 18, 16); line(3, 16, 14, 3, blue);
        pen(teal); p.drawArc(QRectF(5.5, 6, 12, 12), 0, 116 * 16);
        if (kind == K::LimitAngle) {
            line(13.5, 13.2, 16, 11.8, teal); line(8, 5.5, 9.8, 7.2, teal);
        }
        break;
    case K::Parallel:
        polygon({{2, 5}, {13, 5}, {17, 8}, {6, 8}});
        polygon({{2, 12}, {13, 12}, {17, 15}, {6, 15}}, blue);
        break;
    case K::Perpendicular:
        polygon({{3, 12}, {13, 12}, {17, 15}, {7, 15}});
        polygon({{7, 3}, {11, 5}, {11, 13}, {7, 12}}, blue);
        line(8, 10, 10, 10, teal);
        break;
    case K::Tangent:
        ring(8, 8.5, 5.2, blue); line(3, 14, 18, 14); dot(8, 14, teal);
        break;
    case K::Lock:
        lock();
        break;
    case K::ProfileCenter:
        polygon({{3, 4}, {15, 4}, {15, 16}, {3, 16}}, ink, false);
        polygon({{7, 7}, {12, 7}, {12, 12}, {7, 12}}, blue, false);
        dot(9.5, 9.5, teal);
        break;
    case K::Symmetric:
        pen(teal, 1.1, Qt::DashLine); p.drawLine(QPointF(10, 2), QPointF(10, 18));
        polygon({{2.5, 6}, {6.5, 4}, {6.5, 14}, {2.5, 16}});
        polygon({{17.5, 6}, {13.5, 4}, {13.5, 14}, {17.5, 16}}, blue);
        break;
    case K::Width:
        line(3, 3, 3, 17); line(17, 3, 17, 17);
        polygon({{7, 6}, {13, 6}, {13, 14}, {7, 14}}, blue);
        line(5, 10, 7, 10, teal); line(13, 10, 15, 10, teal);
        break;
    case K::Path: {
        QPainterPath path; path.moveTo(2, 16); path.cubicTo(5, 4, 12, 17, 18, 3);
        pen(blue, 1.7); p.drawPath(path);
        polygon({{8, 7}, {13, 8}, {12, 13}, {7, 12}});
        break;
    }
    case K::LinearCoupler:
        line(3, 5, 17, 5); line(3, 15, 17, 15);
        arrow({15, 5}, {1, 0}); arrow({5, 15}, {-1, 0}); line(10, 7, 10, 13, teal);
        break;
    case K::Cam:
        ring(8, 10, 5.5, blue); dot(6.5, 10, teal);
        line(14, 4, 14, 16); line(14, 10, 18, 10);
        break;
    case K::Slot:
        pen(ink); p.drawRoundedRect(QRectF(2.5, 5.5, 15, 9), 4.5, 4.5);
        ring(10, 10, 2.3, blue);
        break;
    case K::Hinge:
        polygon({{2, 5}, {8, 5}, {8, 15}, {2, 15}});
        polygon({{12, 5}, {18, 5}, {18, 15}, {12, 15}}, blue);
        ring(10, 10, 2.5, teal);
        break;
    case K::Gear:
    case K::RackPinion: {
        const QPointF center = kind == K::Gear ? QPointF(8, 10) : QPointF(7.5, 8);
        const qreal radius = kind == K::Gear ? 5.4 : 4.6;
        for (int i = 0; i < 8; ++i) {
            const double a = i * 0.7853981633974483;
            line(center.x() + radius * std::cos(a), center.y() + radius * std::sin(a),
                 center.x() + (radius + 2) * std::cos(a), center.y() + (radius + 2) * std::sin(a), blue);
        }
        ring(center.x(), center.y(), radius, blue); ring(center.x(), center.y(), 1.8);
        if (kind == K::Gear) ring(16, 13.5, 2.3, teal);
        else {
            line(2, 16, 18, 16);
            for (int x = 4; x <= 16; x += 4) line(x, 16, x + 1.5, 13.5);
        }
        break;
    }
    case K::Screw:
        line(9, 2, 9, 18);
        for (int y = 4; y <= 14; y += 4) {
            line(4, y, 14, y + 3, blue); line(14, y + 3, 16, y + 2, blue);
        }
        break;
    case K::UniversalJoint:
        ring(10, 10, 2.6, teal);
        line(10, 2, 10, 7.5, blue); line(10, 12.5, 10, 18, blue);
        line(2, 10, 7.5, 10); line(12.5, 10, 18, 10);
        break;
    case K::Magnetic:
        pen(blue, 2); p.drawArc(QRectF(3, 2.5, 14, 14), 180 * 16, 180 * 16);
        line(3, 9.5, 3, 16); line(17, 9.5, 17, 16); dot(10, 17, teal);
        break;
    case K::Belt:
        ring(5.5, 10, 3.3); ring(15, 10, 3.3, blue);
        line(5.5, 6.7, 15, 6.7, teal); line(5.5, 13.3, 15, 13.3, teal);
        break;
    case K::LockToSketch:
        line(2, 17, 18, 17, teal); line(3, 14.5, 7, 14.5, teal); lock();
        break;
    case K::InPlace:
        polygon({{5, 5}, {13, 5}, {17, 9}, {9, 9}});
        polygon({{9, 9}, {17, 9}, {17, 16}, {9, 16}}, blue);
        line(2, 18, 8, 18, teal); line(2, 18, 2, 12, teal);
        break;
    }
    p.end();
    return QIcon(pixmap);
}
} // namespace thecad::ui
