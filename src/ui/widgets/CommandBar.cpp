#include "ui/widgets/CommandBar.h"
#include "ui/Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QLabel>
#include <QFrame>
namespace thecad::ui
{
// Original line-art icons, painted at device-independent resolution.
QIcon commandIcon(const QString& name)
{
    QPixmap image(64, 64);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    painter.setPen(
        QPen(themeColor(ThemeColor::Accent), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (name.contains("Point"))
    {
        painter.setBrush(themeColor(ThemeColor::Accent));
        painter.drawEllipse(QPointF(16, 16), 3.5, 3.5);
    }
    else if (name.contains("Polygon"))
        painter.drawPolygon(QPolygonF{QPointF(16, 4), QPointF(26, 10), QPointF(26, 22),
                                      QPointF(16, 28), QPointF(6, 22), QPointF(6, 10)});
    else if (name.contains("Centerline") || name.contains("Construction"))
    {
        painter.setPen(QPen(themeColor(ThemeColor::Accent), 1.6, Qt::DashDotLine));
        painter.drawLine(QPointF(4, 27), QPointF(28, 5));
    }
    else if (name.contains("Circle"))
        painter.drawEllipse(QRectF(6, 6, 20, 20));
    else if (name.contains("Rectangle"))
        painter.drawRect(QRectF(5, 8, 22, 16));
    else if (name == "Linear Pattern" || name == "Linear Sketch Pattern")
    {
        for (int x : {3, 13, 23})
            painter.drawRect(QRectF(x, 8, 6, 8));
        painter.drawLine(4, 23, 28, 23);
        painter.drawLine(24, 19, 28, 23);
        painter.drawLine(24, 27, 28, 23);
    }
    else if (name == "Circular Sketch Pattern")
    {
        painter.drawArc(QRectF(5, 5, 22, 22), 35 * 16, 290 * 16);
        for (const auto& point : {QPointF(16, 5), QPointF(5, 16), QPointF(16, 27), QPointF(27, 16)})
            painter.drawEllipse(point, 2.5, 2.5);
        painter.drawPoint(QPointF(16, 16));
    }
    else if (name == "Trim Entities" || name == "Extend Entities" || name == "Split Entities")
    {
        painter.drawLine(5, 19, name == "Trim Entities" ? 18 : 27, 19);
        if (name == "Split Entities")
        {
            painter.drawLine(16, 8, 16, 26);
            painter.drawEllipse(QPointF(16, 19), 2.8, 2.8);
        }
        else
        {
            painter.drawLine(18, 7, 18, 26);
            if (name == "Extend Entities")
            {
                painter.drawLine(23, 15, 27, 19);
                painter.drawLine(23, 23, 27, 19);
            }
            else
            {
                painter.setPen(QPen(QColor("#b83b42"), 1.6, Qt::DashLine));
                painter.drawLine(19, 19, 28, 19);
            }
        }
    }
    else if (name == "Offset Entities")
    {
        painter.drawPolyline(QPolygonF{QPointF(5, 25), QPointF(12, 13), QPointF(27, 13)});
        painter.drawPolyline(QPolygonF{QPointF(5, 15), QPointF(10, 6), QPointF(27, 6)});
        painter.drawLine(22, 6, 22, 13);
    }
    else if (name == "Convert Entities")
    {
        painter.drawPolygon(QPolygonF{QPointF(4, 8), QPointF(19, 4), QPointF(27, 11), QPointF(11, 15)});
        painter.drawPolygon(QPolygonF{QPointF(4, 21), QPointF(19, 17), QPointF(27, 24), QPointF(11, 28)});
        painter.drawLine(16, 11, 16, 21);
        painter.drawLine(12, 18, 16, 21);
        painter.drawLine(20, 18, 16, 21);
    }
    else if (name == "Mirror" || name == "Mirror Entities")
    {
        painter.drawPolygon(QPolygonF{QPointF(5, 25), QPointF(12, 25), QPointF(12, 7)});
        painter.drawPolygon(QPolygonF{QPointF(27, 25), QPointF(20, 25), QPointF(20, 7)});
        painter.setPen(QPen(themeColor(ThemeColor::Accent), 1.4, Qt::DashLine));
        painter.drawLine(16, 4, 16, 28);
    }
    else if (name.contains("Line"))
    {
        painter.drawLine(QPointF(5, 26), QPointF(27, 6));
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(3, 24, 4, 4));
        painter.drawRect(QRectF(25, 4, 4, 4));
    }
    else if (name == "Straight Slot")
    {
        painter.drawRoundedRect(QRectF(4, 10, 24, 12), 6, 6);
        painter.drawPoint(10, 16);
        painter.drawPoint(22, 16);
    }
    else if (name.contains("Arc"))
    {
        painter.drawArc(QRectF(5, 5, 22, 22), 15 * 16, 270 * 16);
    }
    else if (name.contains("Spline"))
    {
        QPainterPath path(QPointF(4, 25));
        path.cubicTo(12, -10, 17, 43, 28, 5);
        painter.drawPath(path);
    }
    else if (name.contains("Sketch") || name.contains("Exit"))
    {
        painter.drawRect(QRectF(5, 6, 19, 21));
        painter.setPen(QPen(QColor("#d49024"), 2.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(13, 22), QPointF(28, 5));
    }
    else if (name.contains("Origin"))
    {
        painter.setPen(QPen(QColor("#b83b42"), 1.6));
        painter.drawLine(5, 25, 27, 25);
        painter.drawLine(8, 28, 8, 4);
        painter.setBrush(QColor("#b83b42"));
        painter.drawEllipse(QPointF(8, 25), 2.5, 2.5);
    }
    else if (name.contains("Axis"))
    {
        painter.setPen(QPen(themeColor(ThemeColor::Accent), 1.6, Qt::DashDotLine));
        painter.drawLine(5, 27, 26, 5);
        painter.setPen(QPen(themeColor(ThemeColor::Accent), 1.6));
        painter.drawLine(26, 5, 25, 13);
        painter.drawLine(26, 5, 18, 6);
    }
    else if (name.contains("Plane"))
    {
        painter.drawPolygon(
            QPolygonF{QPointF(4, 11), QPointF(23, 5), QPointF(28, 23), QPointF(9, 28)});
    }
    else if(name.contains("Revol"))
    {
        painter.drawEllipse(QRectF(7,6,17,20));painter.drawArc(QRectF(3,9,25,14),30*16,290*16);
        painter.drawLine(16,3,16,29);painter.drawLine(27,15,25,10);painter.drawLine(27,15,22,16);
        if(name.contains("Cut")){painter.setPen(QPen(QColor("#b83b42"),2));painter.drawLine(8,9,23,23);}
    }
    else if(name=="Fillet"||name=="Chamfer")
    {
        painter.drawLine(5,27,5,7);painter.drawLine(5,7,26,7);painter.drawLine(26,7,26,27);
        if(name=="Fillet")painter.drawArc(QRectF(5,7,20,20),0,90*16);
        else painter.drawLine(5,22,20,7);
    }
    else if (name.contains("Swept"))
    {
        QPainterPath path(QPointF(6, 24));
        path.cubicTo(6, 13, 12, 7, 24, 7);
        path.moveTo(12, 24);
        path.cubicTo(12, 17, 16, 13, 24, 13);
        painter.drawPath(path);
        painter.drawEllipse(QRectF(21, 7, 6, 6));
        painter.drawEllipse(QRectF(6, 21, 6, 6));
        if (name.contains("Cut"))
        {
            painter.setPen(QPen(QColor("#b83b42"), 2));
            painter.drawLine(17, 23, 25, 17);
        }
    }
    else if (name.contains("Extrude") || name.contains("Part"))
    {
        painter.drawPolygon(
            QPolygonF{QPointF(6, 10), QPointF(20, 5), QPointF(27, 11), QPointF(13, 17)});
        painter.drawPolygon(
            QPolygonF{QPointF(6, 10), QPointF(13, 17), QPointF(13, 28), QPointF(6, 21)});
        painter.drawPolygon(
            QPolygonF{QPointF(13, 17), QPointF(27, 11), QPointF(27, 22), QPointF(13, 28)});
        if (name.contains("Cut"))
        {
            painter.setPen(QPen(QColor("#b83b42"), 2));
            painter.drawLine(18, 12, 24, 19);
        }
    }
    else if (name == "Measure")
    {
        painter.save();
        painter.translate(16, 16);
        painter.rotate(-35);
        painter.drawRoundedRect(QRectF(-13, -4.5, 26, 9), 1.5, 1.5);
        for (int x : {-8, -4, 0, 4, 8})
            painter.drawLine(x, -4, x, x == 0 ? 1 : -1);
        painter.restore();
    }
    else if (name.contains("Select"))
    {
        painter.drawPolygon(QPolygonF{QPointF(8, 4), QPointF(8, 26), QPointF(14, 20),
                                      QPointF(20, 29), QPointF(24, 27), QPointF(18, 18),
                                      QPointF(27, 17)});
    }
    else
    {
        painter.drawRect(QRectF(6, 7, 20, 18));
        painter.drawLine(8, 24, 25, 8);
        painter.drawEllipse(QPointF(16, 16), 3, 3);
    }
    return QIcon(image);
}

QWidget* commandGroup(QHBoxLayout* row, const QString& name, QGridLayout*& grid)
{
    auto* group = new QWidget;
    auto* column = new QVBoxLayout(group);
    column->setContentsMargins(6, 0, 6, 0);
    column->setSpacing(1);
    grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(2);
    grid->setVerticalSpacing(1);
    column->addLayout(grid);
    auto* caption = new QLabel(name);
    caption->setAlignment(Qt::AlignCenter);
    caption->setObjectName("secondaryCaption");
    column->addWidget(caption);
    row->addWidget(group);
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::VLine);

    row->addWidget(separator);
    return group;
}

QToolButton* commandButton(QAction* action, QGridLayout* grid, int row, int column, bool large)
{
    auto* button = new QToolButton;
    button->setDefaultAction(action);
    button->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
    button->setIconSize(large ? QSize(28, 28) : QSize(18, 18));
    button->setAutoRaise(true);
    button->setAttribute(Qt::WA_AlwaysShowToolTips);
    grid->addWidget(button, row, column, large ? 3 : 1, 1);
    return button;
}

} // namespace thecad::ui
