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
    else if (name == "Linear Pattern")
    {
        for (int x : {3, 13, 23})
            painter.drawRect(QRectF(x, 8, 6, 8));
        painter.drawLine(4, 23, 28, 23);
        painter.drawLine(24, 19, 28, 23);
        painter.drawLine(24, 27, 28, 23);
    }
    else if (name == "Mirror")
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
