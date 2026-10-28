#pragma once
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

// Small, consistent outline icons rendered by Qt; no platform icon theme or SVG plugin required.
namespace Icons {
enum Name { Map, New, Open, Save, Undo, Redo, Fit, Plus, Minus, Pen, Eraser, Line, Rectangle, Fill, Hand, Picker, Polygon, Chevron, Close };
inline QIcon get(Name name, QColor color = QColor("#49534e")) {
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64}) {
        QPixmap pixmap(size, size); pixmap.fill(Qt::transparent);
        QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24., size / 24.);
        p.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        switch (name) {
        case Map:
            path.moveTo(3, 5); path.lineTo(9, 3); path.lineTo(15, 5); path.lineTo(21, 3); path.lineTo(21, 19);
            path.lineTo(15, 21); path.lineTo(9, 19); path.lineTo(3, 21); path.closeSubpath();
            path.moveTo(9, 3); path.lineTo(9, 19); path.moveTo(15, 5); path.lineTo(15, 21); break;
        case New:
            path.moveTo(14, 3); path.lineTo(5, 3); path.lineTo(5, 21); path.lineTo(19, 21); path.lineTo(19, 8); path.closeSubpath();
            path.moveTo(14, 3); path.lineTo(14, 8); path.lineTo(19, 8);
            path.moveTo(9, 14); path.lineTo(15, 14); path.moveTo(12, 11); path.lineTo(12, 17); break;
        case Open:
            path.moveTo(3, 18); path.lineTo(3, 5); path.lineTo(9, 5); path.lineTo(11, 7); path.lineTo(20, 7); path.lineTo(20, 10);
            path.moveTo(3, 19); path.lineTo(6, 10); path.lineTo(22, 10); path.lineTo(19, 19); path.closeSubpath(); break;
        case Save:
            path.moveTo(4, 3); path.lineTo(17, 3); path.lineTo(21, 7); path.lineTo(21, 21); path.lineTo(4, 21); path.closeSubpath();
            path.moveTo(8, 3); path.lineTo(8, 9); path.lineTo(16, 9); path.lineTo(16, 3);
            path.moveTo(8, 21); path.lineTo(8, 14); path.lineTo(17, 14); path.lineTo(17, 21); break;
        case Undo: case Redo:
            if (name == Redo) { p.translate(24, 0); p.scale(-1, 1); }
            path.moveTo(8, 5); path.lineTo(3, 10); path.lineTo(8, 15);
            path.moveTo(3, 10); path.lineTo(14, 10); path.cubicTo(22, 10, 22, 20, 15, 20); break;
        case Fit:
            path.moveTo(8, 3); path.lineTo(3, 3); path.lineTo(3, 8); path.moveTo(16, 3); path.lineTo(21, 3); path.lineTo(21, 8);
            path.moveTo(3, 16); path.lineTo(3, 21); path.lineTo(8, 21); path.moveTo(16, 21); path.lineTo(21, 21); path.lineTo(21, 16);
            p.drawRoundedRect(QRectF(8, 8, 8, 8), 1, 1); break;
        case Plus: case Minus:
            path.moveTo(5, 12); path.lineTo(19, 12);
            if (name == Plus) { path.moveTo(12, 5); path.lineTo(12, 19); } break;
        case Pen:
            path.moveTo(4, 20); path.lineTo(5, 15); path.lineTo(17, 3); path.lineTo(21, 7); path.lineTo(9, 19); path.closeSubpath();
            path.moveTo(14, 6); path.lineTo(18, 10); break;
        case Eraser:
            path.moveTo(3, 14); path.lineTo(13, 4); path.lineTo(21, 12); path.lineTo(13, 20); path.lineTo(9, 20); path.closeSubpath();
            path.moveTo(8, 9); path.lineTo(16, 17); path.moveTo(13, 20); path.lineTo(21, 20); break;
        case Line:
            path.moveTo(5, 19); path.lineTo(19, 5); p.drawEllipse(QPointF(5, 19), 1.6, 1.6); p.drawEllipse(QPointF(19, 5), 1.6, 1.6); break;
        case Rectangle: p.drawRoundedRect(QRectF(4, 5, 16, 14), 1, 1); break;
        case Fill:
            path.moveTo(5, 8); path.lineTo(12, 15); path.moveTo(9, 3); path.lineTo(18, 12); path.lineTo(10, 20); path.lineTo(2, 12); path.closeSubpath();
            path.moveTo(20, 15); path.cubicTo(24, 20, 22, 22, 20, 22); path.cubicTo(18, 22, 16, 20, 20, 15); break;
        case Hand:
            path.moveTo(7, 13); path.lineTo(7, 6); path.cubicTo(7, 3, 10, 3, 10, 6); path.lineTo(10, 11);
            path.moveTo(10, 9); path.lineTo(10, 4); path.cubicTo(10, 1, 13, 1, 13, 4); path.lineTo(13, 11);
            path.moveTo(13, 6); path.cubicTo(13, 3, 16, 3, 16, 6); path.lineTo(16, 12);
            path.moveTo(16, 8); path.cubicTo(16, 5, 19, 5, 19, 8); path.lineTo(19, 15); path.cubicTo(19, 23, 9, 24, 6, 18);
            path.lineTo(3, 13); path.cubicTo(2, 11, 4, 10, 5, 11); path.lineTo(7, 13); break;
        case Picker:
            path.moveTo(4, 20); path.lineTo(5, 15); path.lineTo(14, 6); path.lineTo(18, 10); path.lineTo(9, 19); path.closeSubpath();
            path.moveTo(12, 4); path.lineTo(20, 12); path.moveTo(16, 8); path.lineTo(19, 5); path.cubicTo(22, 2, 24, 5, 21, 8); path.lineTo(18, 11); break;
        case Polygon:
            path.moveTo(4, 7); path.lineTo(17, 3); path.lineTo(21, 16); path.lineTo(10, 21); path.closeSubpath();
            for (auto point : {QPointF(4, 7), QPointF(17, 3), QPointF(21, 16), QPointF(10, 21)}) p.drawEllipse(point, 1.6, 1.6);
            break;
        case Chevron: path.moveTo(8, 5); path.lineTo(15, 12); path.lineTo(8, 19); break;
        case Close: path.moveTo(6, 6); path.lineTo(18, 18); path.moveTo(18, 6); path.lineTo(6, 18); break;
        }
        p.drawPath(path); p.end();
        icon.addPixmap(pixmap);
        QImage disabled = pixmap.toImage();
        for (int y = 0; y < disabled.height(); ++y) for (int x = 0; x < disabled.width(); ++x) {
            auto c = disabled.pixelColor(x, y); c.setAlpha(c.alpha() * .35); disabled.setPixelColor(x, y, c);
        }
        icon.addPixmap(QPixmap::fromImage(disabled), QIcon::Disabled);
    }
    return icon;
}
}
