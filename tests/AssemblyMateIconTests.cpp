#include "ui/assembly/MateIcons.h"
#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <algorithm>
#include <stdexcept>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    for (int i = 0; i < 22; ++i)
    {
        const auto icon = thecad::ui::assemblyMateIcon(thecad::modeling::assembly::MateKind(i));
        if (icon.isNull()) throw std::runtime_error("Mate icon is empty");
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
        if (opaque < 8 || maxX < 10 || maxY < 10)
            throw std::runtime_error("Mate icon did not fill its logical size");
    }
    return 0;
}
