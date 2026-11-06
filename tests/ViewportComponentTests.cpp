#include "viewport/SketchPicker.h"
#include "viewport/DimensionPresenter.h"
#include "viewport/CameraNavigation.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace thecad;
namespace
{
void require(bool ok, const char* why)
{
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        modeling::Sketch sketch;
        sketch.entities = {{modeling::EntityKind::Line, {10, 0}, {30, 0}, 1, "line"},
                           {modeling::EntityKind::Circle, {100, 100}, {}, 5, "circle"}};
        modeling::SelectionPolicy policy;
        for (double scale : {0.25, 3.0})
            for (double ratio : {1.0, 1.5, 2.0})
            {
                viewport::SketchPicker picker(sketch, policy, "sketch", ratio, 8, 1 / scale,
                                              [scale](modeling::Point2 p)
                                              { return QPointF(p.x * scale, p.y * scale); });
                const auto candidates = picker.sketchEntityCandidates({20 * scale, 7});
                require(candidates.size() == 1 && candidates.front().reference.entityId == "line",
                        "Screen-space line tolerance changed");
                require(picker.sketchEntityCandidates({20 * scale, 9}).empty(),
                        "Picking accepts unrelated geometry");
                const auto midpoint = picker.pointAt({20 * scale, 0}, true);
                require(midpoint && midpoint->reference.role == modeling::PointRole::Midpoint,
                        "Midpoint snap changed");
                require(!picker.selectionPointAt({105 * scale, 100 * scale}),
                        "Circle center swallowed rim");
                const auto circle = picker.sketchEntityCandidates({105 * scale, 100 * scale});
                require(circle.size() == 1 && circle.front().reference.entityId == "circle",
                        "Circle picking changed");
            }
        modeling::SketchDimension radius;
        radius.kind = modeling::DimensionKind::CircleRadius;
        radius.first = {"circle", modeling::PointRole::Center};
        radius.value = 5;
        require(viewport::DimensionPresenter::text(sketch, radius) == "R5 mm",
                "Dimension text changed");
        radius.driving = false;
        require(viewport::DimensionPresenter::text(sketch, radius) == "(R5 mm)",
                "Reference text changed");
        viewport::CameraNavigation camera;
        camera.begin(Qt::NoModifier, {1, 2}, {2, 4}, {10, 20, 30}, {});
        require(camera.mode() == viewport::CameraNavigation::Mode::Orbit && camera.anchor() &&
                    camera.anchor()->Distance({10, 20, 30}) < 1e-12,
                "Pivot activation changed");
        camera.stop();
        require(camera.button() == Qt::NoButton, "Navigation stop changed");
        require(viewport::CameraNavigation::modeFor(Qt::ControlModifier | Qt::ShiftModifier) ==
                    viewport::CameraNavigation::Mode::Pan,
                "Navigation modifier priority changed");
        std::cout << "PASS: independent sketch picking across scales/DPI, annotation text and "
                     "camera gesture state\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
