#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace Icons {

namespace {

QPolygonF poly(std::initializer_list<QPointF> points)
{
    return QPolygonF(QList<QPointF>(points));
}

void drawSpeaker(QPainter &p, const QColor &color)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(poly({{4, 9.5}, {7.5, 9.5}, {12, 5.5}, {12, 18.5}, {7.5, 14.5}, {4, 14.5}}));
    p.restore();
}

// All shapes live on a 24×24 grid.
QPixmap render(Name name, const QColor &color)
{
    const qreal ratio = 3;
    QPixmap pixmap(int(24 * ratio), int(24 * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    auto fill = [&](const QPolygonF &shape) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(shape);
        p.restore();
    };
    auto fillRect = [&](const QRectF &rect) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(rect, 1, 1);
        p.restore();
    };
    auto polyline = [&](std::initializer_list<QPointF> points) {
        const QPolygonF line = poly(points);
        p.drawPolyline(line);
    };

    switch (name) {
    case Name::Open: {
        QPainterPath folder;
        folder.moveTo(3, 18);
        folder.lineTo(3, 6.5);
        folder.lineTo(9, 6.5);
        folder.lineTo(11, 8.5);
        folder.lineTo(21, 8.5);
        folder.lineTo(21, 18);
        folder.closeSubpath();
        p.drawPath(folder);
        break;
    }
    case Name::Previous:
        fill(poly({{17.5, 6}, {8.5, 12}, {17.5, 18}}));
        fillRect({6, 6, 2.4, 12});
        break;
    case Name::Play:
        fill(poly({{8, 5}, {19.5, 12}, {8, 19}}));
        break;
    case Name::Pause:
        fillRect({7, 5, 3.6, 14});
        fillRect({13.4, 5, 3.6, 14});
        break;
    case Name::Stop:
        fillRect({6.5, 6.5, 11, 11});
        break;
    case Name::Next:
        fill(poly({{6.5, 6}, {15.5, 12}, {6.5, 18}}));
        fillRect({15.6, 6, 2.4, 12});
        break;
    case Name::Volume:
        drawSpeaker(p, color);
        p.drawArc(QRectF(9.5, 8, 8, 8), -50 * 16, 100 * 16);
        p.drawArc(QRectF(7, 5, 14, 14), -50 * 16, 100 * 16);
        break;
    case Name::Mute:
        drawSpeaker(p, color);
        p.drawLine(QPointF(15.5, 9.5), QPointF(20.5, 14.5));
        p.drawLine(QPointF(20.5, 9.5), QPointF(15.5, 14.5));
        break;
    case Name::Audio:
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(8.5, 17), 3, 2.4);
        p.drawEllipse(QPointF(16.5, 15), 3, 2.4);
        p.restore();
        polyline({{11.4, 17}, {11.4, 5.5}, {19.4, 4}, {19.4, 15}});
        break;
    case Name::Subtitle:
        p.drawRoundedRect(QRectF(3, 5.5, 18, 13), 2.5, 2.5);
        p.drawLine(QPointF(6.5, 12.5), QPointF(10.5, 12.5));
        p.drawLine(QPointF(13, 12.5), QPointF(17.5, 12.5));
        p.drawLine(QPointF(6.5, 15.5), QPointF(14, 15.5));
        break;
    case Name::Info:
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(12, 7.8), 1.2, 1.2);
        p.restore();
        break;
    case Name::Playlist:
        p.drawLine(QPointF(4, 7), QPointF(16, 7));
        p.drawLine(QPointF(4, 12), QPointF(16, 12));
        p.drawLine(QPointF(4, 17), QPointF(11, 17));
        fill(poly({{14.5, 14.2}, {20, 17}, {14.5, 19.8}}));
        break;
    case Name::Fullscreen:
        polyline({{4, 9}, {4, 4}, {9, 4}});
        polyline({{15, 4}, {20, 4}, {20, 9}});
        polyline({{20, 15}, {20, 20}, {15, 20}});
        polyline({{9, 20}, {4, 20}, {4, 15}});
        break;
    case Name::ExitFullscreen:
        polyline({{9, 4}, {9, 9}, {4, 9}});
        polyline({{15, 4}, {15, 9}, {20, 9}});
        polyline({{20, 15}, {15, 15}, {15, 20}});
        polyline({{4, 15}, {9, 15}, {9, 20}});
        break;
    }
    return pixmap;
}

} // namespace

QIcon icon(Name name)
{
    QIcon result;
    result.addPixmap(render(name, QColor(0xd8, 0xd8, 0xdc)), QIcon::Normal);
    result.addPixmap(render(name, QColor(0xff, 0xff, 0xff)), QIcon::Active);
    result.addPixmap(render(name, QColor(0x55, 0x55, 0x5c)), QIcon::Disabled);
    return result;
}

} // namespace Icons
