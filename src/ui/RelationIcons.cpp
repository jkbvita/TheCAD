#include "ui/Theme.h"
#include "ui/RelationIcons.h"
#include <QPainter>
#include <QPixmap>
namespace thecad::ui {
QImage relationArtwork(modeling::RelationKind kind,int pixels,bool symbol,bool hover,bool selected){
    QImage image(pixels,pixels,QImage::Format_RGBA8888);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.scale(pixels/20.0,pixels/20.0);
    if(symbol){p.setBrush(ThemeManager::instance().isDark()?(selected?QColor("#27593c"):QColor("#203e2b")):(selected?QColor("#b6edc9"):QColor("#e5f6e9")));p.setPen(QPen(hover?QColor("#ed8a14"):selected?QColor("#2587dc"):QColor("#239543"),1));p.drawRoundedRect(QRectF(0.8,0.8,18.4,18.4),1,1);}
    p.setPen(QPen(symbol?(ThemeManager::instance().isDark()?QColor("#89D9A2"):QColor("#176a31")):themeColor(ThemeColor::Accent),1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    using K=modeling::RelationKind;
    switch(kind){
    case K::Horizontal:p.drawLine(4,10,16,10);break;
    case K::Vertical:p.drawLine(10,4,10,16);break;
    case K::Parallel:p.drawLine(4,5,12,15);p.drawLine(8,5,16,15);break;
    case K::Perpendicular:p.drawLine(5,4,5,15);p.drawLine(5,15,16,15);p.drawRect(QRectF(5,11,4,4));break;
    case K::Collinear:p.drawLine(3,15,8,10);p.drawLine(12,6,17,1);p.setPen(QPen(p.pen().color(),1,Qt::DotLine));p.drawLine(7,11,14,4);break;
    case K::Equal:p.drawLine(4,7,16,7);p.drawLine(4,13,16,13);break;
    case K::EqualCurveLength:p.drawArc(QRectF(3,2,14,12),0,180*16);p.drawLine(5,13,15,13);p.drawLine(5,16,15,16);break;
    case K::Concentric:p.drawEllipse(QRectF(3,3,14,14));p.drawEllipse(QRectF(7,7,6,6));break;
    case K::Tangent:p.drawEllipse(QRectF(4,7,9,9));p.drawLine(8,3,17,12);break;
    case K::FixedEntity:case K::FixedPoint:p.drawRoundedRect(QRectF(5,9,10,8),1,1);p.drawArc(QRectF(7,3,6,12),0,180*16);p.drawPoint(10,13);break;
    case K::Coincident:p.drawEllipse(QRectF(4,6,9,9));p.drawEllipse(QRectF(8,3,8,8));break;
    case K::PointOnLine:p.drawLine(3,15,17,4);p.setBrush(p.pen().color());p.drawEllipse(QPointF(10,9.5),2,2);break;
    case K::PointOnCircle:p.drawEllipse(QRectF(4,4,12,12));p.setBrush(p.pen().color());p.drawEllipse(QPointF(16,10),2,2);break;
    case K::Midpoint:p.drawLine(3,15,17,15);p.drawLine(10,5,5,13);p.drawLine(5,13,15,13);p.drawLine(15,13,10,5);break;
    }
    return image;
}
QIcon relationIcon(modeling::RelationKind kind){return QIcon(QPixmap::fromImage(relationArtwork(kind,64)));}
}
