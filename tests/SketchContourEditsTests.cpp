#include "document/PartDocument.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/SketchContourEdits.h"
#include "modeling/SketchOffset.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace thecad::document;
using namespace thecad::modeling;

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void near(Point2 point, Point2 expected, const char* message)
{
    check(std::hypot(point.x - expected.x, point.y - expected.y) < 1e-6, message);
}

template <typename Operation>
void rejects(Operation operation, const char* message)
{
    try
    {
        operation();
    }
    catch (const std::runtime_error&)
    {
        return;
    }
    throw std::runtime_error(message);
}

Sketch rectangle()
{
    Sketch sketch;
    sketch.entities = {{EntityKind::Line, {0, 0}, {20, 0}, 1, "bottom"},
                       {EntityKind::Line, {20, 0}, {20, 12}, 1, "right"},
                       {EntityKind::Line, {20, 12}, {0, 12}, 1, "top"},
                       {EntityKind::Line, {0, 12}, {0, 0}, 1, "left"}};
    for (std::size_t i = 0; i < sketch.entities.size(); ++i)
    {
        const auto& id = sketch.entities[i].id;
        sketch.relations.push_back({id + "-axis", i % 2 ? RelationKind::Vertical : RelationKind::Horizontal,
                                    {id, PointRole::First}, {id, PointRole::Second}});
        sketch.relations.push_back({id + "-join", RelationKind::Coincident, {id, PointRole::Second},
                                    {sketch.entities[(i + 1) % 4].id, PointRole::First}});
    }
    return sketch;
}

void trimAndExtend()
{
    Sketch sketch;
    sketch.entities = {{EntityKind::Line, {-15, 0}, {15, 0}, 1, "work"},
                       {EntityKind::Circle, {0, 0}, {}, 5, "circle"}};
    sketch.relations = {{"level", RelationKind::Horizontal, {"work", PointRole::First},
                         {"work", PointRole::Second}}};
    auto clipped = editSketchLine(sketch, SketchModifier::Trim, "work", {-12, 0});
    near(clipped.entities.front().first, {-5, 0}, "Trim chose the wrong circle intersection.");
    check(clipped.entities.front().id == "work" &&
              clipped.relations.back().kind == RelationKind::PointOnCircle,
          "Trim must retain the line identity and attach to the boundary.");
    (void)requireSolvedSketch(solvePlanarSketch(clipped));
    auto middle = editSketchLine(sketch, SketchModifier::Trim, "work", {0, 0});
    check(middle.entities.size() == 3, "Trimming between two hits must keep both outer pieces.");
    near(middle.entities.front().second, {-5, 0}, "Incorrect left trim point.");
    near(middle.entities.back().first, {5, 0}, "Incorrect right trim point.");
    (void)requireSolvedSketch(solvePlanarSketch(middle));

    sketch.entities = {{EntityKind::Line, {-2, 0}, {2, 0}, 1, "work"},
                       {EntityKind::Line, {4, 2}, {4, 7}, 1, "miss"},
                       {EntityKind::Line, {-6, -4}, {-6, 4}, 1, "left"},
                       {EntityKind::Circle, {10, 0}, {}, 2, "circle"},
                       {EntityKind::Line, {18, -4}, {18, 4}, 1, "far"}};
    const auto right = editSketchLine(sketch, SketchModifier::Extend, "work", {1, 0});
    near(right.entities.front().second, {8, 0}, "Extend must choose the closest finite boundary.");
    check(right.relations.back().second.entityId == "circle", "Extend must retain its boundary relation.");
    const auto left = editSketchLine(sketch, SketchModifier::Extend, "work", {-1, 0});
    near(left.entities.front().first, {-6, 0}, "Extend chose the wrong end.");
    (void)requireSolvedSketch(solvePlanarSketch(left));

    SketchDimension length;
    length.id = "original-length";
    length.kind = DimensionKind::LineLength;
    length.first = {"work", PointRole::First};
    length.second = {"work", PointRole::Second};
    length.value = 4;
    sketch.dimensions.push_back(length);
    rejects([&] { editSketchLine(sketch, SketchModifier::Extend, "work", {1, 0}); },
            "An incompatible driving dimension should block extension.");
    near(sketch.entities.front().second, {2, 0}, "An invalid edit changed the source sketch.");
}

void splitAndDependencies()
{
    Sketch sketch;
    sketch.entities = {{EntityKind::Line, {-10, 3}, {10, 3}, 1, "work"},
                       {EntityKind::Point, {10, 3}, {}, 1, "tip"}};
    sketch.relations = {{"level", RelationKind::Horizontal, {"work", PointRole::First},
                         {"work", PointRole::Second}},
                        {"tip-join", RelationKind::Coincident, {"work", PointRole::Second},
                         {"tip", PointRole::First}}};
    SketchDimension length;
    length.id = "overall-length";
    length.kind = DimensionKind::LineLength;
    length.first = {"work", PointRole::First};
    length.second = {"work", PointRole::Second};
    length.value = 20;
    sketch.dimensions.push_back(length);
    const auto split = requireSolvedSketch(solvePlanarSketch(
        editSketchLine(sketch, SketchModifier::Split, "work", {2, 3})));
    near(split.entities.front().second, {2, 3}, "Split point is incorrect.");
    near(split.entities.back().first, {2, 3}, "Split segments must share the split point.");
    check(split.entities.front().id == "work" && split.relations[1].first.entityId == split.entities.back().id,
          "Split must preserve the original ID and transfer the old end's references.");
    check(split.dimensions.front().id == length.id &&
              split.dimensions.front().kind == DimensionKind::PointDistance &&
              std::abs(dimensionMeasurement(split, split.dimensions.front()) - 20) < 1e-6,
          "Split should preserve the original driving length between the outer endpoints.");

    auto part = PartDocument::createEmpty();
    const auto sketchId = part.addSketch(rectangle());
    part.addExtrude(sketchId, {"bottom", "right", "top", "left"}, 5, Direction::Positive);
    auto feature = part.features().front();
    feature.sketch = editSketchLine(feature.sketch, SketchModifier::Split, "bottom", {8, 0});
    const auto body = part.shape();
    rejects([&] { part.replaceFeature(feature); }, "Invalidated dependent contours should block a split.");
    check(part.shape().IsSame(body) && part.features().front().sketch.entities.size() == 4,
          "A failed contour edit must retain the previous solid and sketch.");
}

void linkedOffsets()
{
    Sketch line;
    line.entities = {{EntityKind::Line, {0, 0}, {20, 0}, 1, "seed"}};
    auto offset = offsetSketchEntities(line, {"seed"}, {3});
    near(offset.entities.back().first, {0, 3}, "A line offset must follow the selected side.");
    auto rotated = line.entities.front();
    rotated.first = {2, 3};
    rotated.second = {14, 15};
    offset = editSketchEntity(offset, rotated);
    const double side = 3 / std::sqrt(2.);
    near(offset.entities.back().first, {2 - side, 3 + side}, "A line offset must follow source rotation.");
    near(offset.entities.back().second, {14 - side, 15 + side}, "The offset ends must stay aligned.");

    Sketch elbow;
    elbow.entities = {{EntityKind::Line, {0, 0}, {12, 0}, 1, "first"},
                      {EntityKind::Line, {12, 8}, {12, 0}, 1, "reversed"}};
    const auto open = offsetSketchEntities(elbow, {"first"}, {2, false, false, true});
    check(!open.offsets.front().closed && open.offsets.front().segments.back().reversed,
          "Select chain must order open lines independently of their drawing directions.");
    near(open.entities[2].first, {0, 2}, "An open offset must keep its first end aligned.");
    near(open.entities[2].second, {10, 2}, "An open corner must miter.");
    near(open.entities[3].second, {10, 8}, "An open offset must keep its last end aligned.");

    const auto outline = rectangle();
    auto rim = offsetSketchEntities(outline, {"bottom"}, {2, false, false, true});
    check(rim.offsets.front().closed && rim.offsets.front().segments.size() == 4 &&
              rim.entities.size() == 8 && closedContours(rim).size() == 2,
          "Select chain must produce a complete mitered loop.");
    near(rim.entities[4].first, {-2, -2}, "An outer miter must be at the shifted line intersection.");
    near(rim.entities[4].second, {22, -2}, "Incorrect outer miter length.");
    rejects([&] { offsetSketchEntities(rim, {"bottom"}, {2, false, false, true}); },
            "A repeated offset must not create coincident geometry.");
    auto inset = offsetSketchEntities(outline, {"bottom"}, {2, true, false, true});
    near(inset.entities[4].first, {2, 2}, "Reverse must offset inward for a closed loop.");
    rejects([&] { offsetSketchEntities(outline, {"bottom"}, {7, true, false, true}); },
            "An offset that collapses the loop must be rejected.");

    Sketch circle;
    circle.entities = {{EntityKind::Circle, {8, 6}, {}, 5, "circle"}};
    auto rings = offsetSketchEntities(circle, {"circle"}, {2, false, true, false, true});
    check(rings.offsets.size() == 2 && std::abs(rings.entities[1].radius - 7) < 1e-6 &&
              std::abs(rings.entities[2].radius - 3) < 1e-6 && rings.entities[1].construction,
          "Both sides must create concentric offsets at the requested distance.");
    auto grown = circle.entities.front();
    grown.radius = 8;
    rings = editSketchEntity(rings, grown);
    check(std::abs(rings.entities[1].radius - 10) < 1e-6 &&
              std::abs(rings.entities[2].radius - 6) < 1e-6,
          "Circle offsets must track source radius changes.");

    auto part = PartDocument::createEmpty();
    part.addSketch(rim);
    QTemporaryDir temp;
    const auto path = temp.filePath("offset.thepart");
    part.save(path);
    QFile file(path);
    check(file.open(QIODevice::ReadOnly) &&
              QJsonDocument::fromJson(file.readAll()).object()["version"].toInt() == 21,
          "Linked offsets require schema 21.");
    const auto reopened = PartDocument::load(path);
    const auto& saved = reopened.features().front().sketch;
    check(saved.offsets.size() == 1 && saved.offsets.front().segments.size() == 4,
          "Offset associations must survive save and reopen.");
    check(removeSketchEntity(saved, "bottom").offsets.empty(),
          "Deleting a source must release the remaining offset group.");
}
}

int main()
{
    try
    {
        trimAndExtend();
        splitAndDependencies();
        linkedOffsets();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
