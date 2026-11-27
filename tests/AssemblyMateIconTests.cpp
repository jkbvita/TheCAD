#include "ui/assembly/MateIcons.h"
#include "modeling/assembly/MateEquations.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QImage sheet(600, 7 * 50, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor("#303843"));
    QPainter sheetPainter(&sheet);
    sheetPainter.setPen(QColor("#E3E8ED"));
    std::set<QByteArray> images;
    for (int i = 0; i < 26; ++i)
    {
        const auto kind = thecad::modeling::assembly::MateKind(i);
        const auto icon = thecad::ui::assemblyMateIcon(kind);
        const int x = (i % 4) * 150, y = (i / 4) * 50;
        sheetPainter.drawPixmap(x + 8, y + 11, icon.pixmap(QSize(20, 20), 1.0));
        sheetPainter.drawText(x + 33, y + 25,
                              QString::fromUtf8(thecad::modeling::assembly::mateName(kind)));
        if (icon.isNull()) {
            std::cerr << "Mate icon is empty: " << i << '\n';
            return 1;
        }
        const QImage image = icon.pixmap(QSize(20, 20), 1.0).toImage();
        int opaque = 0, maxX = -1, maxY = -1;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).alpha() > 20)
                {
                    ++opaque;
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
        if (opaque < 8 || maxX < 10 || maxY < 10) {
            std::cerr << "Mate icon lacks visual coverage: " << i << " pixels=" << opaque
                      << " maxX=" << maxX << " maxY=" << maxY << '\n';
            return 1;
        }
        const QByteArray pixels(reinterpret_cast<const char*>(image.constBits()),
                                image.sizeInBytes());
        if (!images.insert(pixels).second) {
            std::cerr << "Mate icon duplicates another kind: " << i << '\n';
            return 1;
        }
    }
    sheetPainter.end();
    if (argc > 1 && !sheet.save(QString::fromLocal8Bit(argv[1]))) {
        std::cerr << "Could not save Mate icon sheet\n";
        return 1;
    }
    return 0;
}
