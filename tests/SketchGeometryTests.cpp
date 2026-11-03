#include "modeling/Sketch.h"
#include "modeling/Box.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <vector>

namespace {
using namespace thecad::modeling;
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void rejected(const std::function<void()>& action)
{
    static int caseNumber = 0;
    ++caseNumber;
    bool failed = false;
    try {
        action();
    } catch (const std::runtime_error& error) {
        failed = error.what()[0] != '\0';
    }
    if (!failed) { throw std::runtime_error("Unsuitable geometry did not reject case " + std::to_string(caseNumber)); }
}

Sketch rectangle(SketchPlane plane = SketchPlane::XY)
{
    return {plane, {{EntityKind::Rectangle, {0, 0}, {40, 30}, 1}}};
}

Sketch circle(SketchPlane plane = SketchPlane::XY)
{
    return {plane, {{EntityKind::Circle, {20, 15}, {}, 5}}};
}

double volume(const TopoDS_Shape& shape)
{
    require(!shape.IsNull(), "Operation must generate real geometry");
    require(BRepCheck_Analyzer(shape).IsValid(), "Operation must generate valid topology");
    int solids = 0;
    for (TopExp_Explorer item(shape, TopAbs_SOLID); item.More(); item.Next()) { ++solids; }
    require(solids == 1, "The current modeling milestone requires one solid body");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

void requireNear(double value, double expected)
{
    require(std::abs(value - expected) < std::max(std::abs(expected) * 1e-8, 1e-6),
            "Generated precise geometry must match its numeric parameters");
}

void checkPlane(SketchPlane plane)
{
    const auto sketch = rectangle(plane);
    const auto body = makeExtrusion(sketch, 25, Direction::Positive);
    requireNear(volume(body), 40 * 30 * 25);
    const auto cut = makeCut(body, circle(plane), 7, true, Direction::Positive);
    requireNear(volume(cut), 40 * 30 * 25 - pi * 5 * 5 * 25);
    Bnd_Box bounds;
    BRepBndLib::Add(body, bounds);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    if (plane == SketchPlane::XY) {
        requireNear(xmax, 40); requireNear(ymax, 30); requireNear(zmax, 25); requireNear(zmin, 0);
    } else if (plane == SketchPlane::XZ) {
        requireNear(xmax, 40); requireNear(ymin, -25); requireNear(zmax, 30); requireNear(ymax, 0);
    } else {
        requireNear(xmax, 25); requireNear(ymax, 40); requireNear(zmax, 30); requireNear(xmin, 0);
    }
    const auto negative = makeExtrusion(sketch, 25, Direction::Negative);
    requireNear(volume(negative), 40 * 30 * 25);
    requireNear(volume(makeCut(negative, circle(plane), 1, true, Direction::Negative)),
         40 * 30 * 25 - pi * 5 * 5 * 25);
}
}

int main()
{
    try {
        for (const auto plane : {SketchPlane::XY, SketchPlane::XZ, SketchPlane::YZ}) {
            checkPlane(plane);
        }
        const auto profile = rectangle();
        validateClosedProfile(profile);
        const auto original = makeExtrusion(profile, 25, Direction::Positive);
        std::ostringstream originalBrep;
        BRepTools::Write(original, originalBrep);
        requireNear(volume(original), 30000);
        requireNear(volume(makeCut(original, circle(), 7, false, Direction::Positive)), 30000 - pi * 25 * 7);
        requireNear(volume(makeCut(original, circle(), 1, true, Direction::Positive)), 30000 - pi * 25 * 25);
        const auto regenerated = makeExtrusion(profile, 60, Direction::Positive);
        requireNear(volume(makeCut(regenerated, circle(), 1, true, Direction::Positive)), 72000 - pi * 25 * 60);

        // Reversed, unordered lines still form the same one-loop profile.
        Sketch lines{SketchPlane::XY, {
            {EntityKind::Line, {40, 30}, {40, 0}, 1},
            {EntityKind::Line, {0, 0}, {0, 30}, 1},
            {EntityKind::Line, {40, 0}, {0, 0}, 1},
            {EntityKind::Line, {0, 30}, {40, 30}, 1}}};
        requireNear(volume(makeExtrusion(lines, 25, Direction::Positive)), 30000);
        Sketch partial{SketchPlane::XY, {{EntityKind::Line, {0, 0}, {10, 10}, 1}}};
        require(!sketchShape(partial).IsNull(), "Open sketch lines must be displayable");
        rejected([&] { validateClosedProfile(partial); });
        rejected([&] { makeExtrusion(partial, 10, Direction::Positive); });
        lines.entities.pop_back();
        rejected([&] { makeExtrusion(lines, 10, Direction::Positive); });
        Sketch crossed{SketchPlane::XY, {
            {EntityKind::Line, {0, 0}, {10, 10}, 1},
            {EntityKind::Line, {10, 10}, {0, 10}, 1},
            {EntityKind::Line, {0, 10}, {10, 0}, 1},
            {EntityKind::Line, {10, 0}, {0, 0}, 1}}};
        rejected([&] { makeExtrusion(crossed, 10, Direction::Positive); });
        auto branched = profile;
        branched.entities = {
            {EntityKind::Line, {0, 0}, {10, 0}, 1},
            {EntityKind::Line, {10, 0}, {5, 10}, 1},
            {EntityKind::Line, {5, 10}, {0, 0}, 1},
            {EntityKind::Line, {0, 0}, {-10, 0}, 1}};
        rejected([&] { makeExtrusion(branched, 10, Direction::Positive); });
        auto mixed = rectangle();
        mixed.entities.push_back(circle().entities[0]);
        rejected([&] { makeExtrusion(mixed, 10, Direction::Positive); });
        rejected([&] { makeExtrusion(Sketch{}, 10, Direction::Positive); });
        rejected([&] { makeExtrusion(profile, 0, Direction::Positive); });
        rejected([&] { makeExtrusion(profile, -10, Direction::Positive); });
        rejected([&] { makeExtrusion(profile, std::numeric_limits<double>::infinity(), Direction::Positive); });
        rejected([&] { makeExtrusion(profile, 10, static_cast<Direction>(99)); });
        rejected([&] { sketchShape({static_cast<SketchPlane>(99), {}}); });
        auto bad = rectangle();
        bad.entities[0].second.x = 0;
        rejected([&] { sketchShape(bad); });
        bad = circle();
        bad.entities[0].radius = 0;
        rejected([&] { sketchShape(bad); });
        bad = rectangle();
        bad.entities[0].first.x = std::numeric_limits<double>::quiet_NaN();
        rejected([&] { sketchShape(bad); });

        Sketch selectable = rectangle();
        selectable.entities[0].id = "left-rectangle";
        auto secondRectangle = rectangle().entities[0];
        secondRectangle.first = {60, 0};
        secondRectangle.second = {100, 30};
        secondRectangle.id = "right-rectangle";
        selectable.entities.push_back(secondRectangle);
        selectable.entities.push_back({EntityKind::Line, {0, 60}, {10, 60}, 1, "open-line"});
        const auto contours = closedContours(selectable);
        require(contours.size() == 2, "Independent closed regions must be identified beside open geometry");
        require(contourContains(contours[0], SketchPlane::XY, {20, 15}), "Point in first region must select it");
        require(!contourContains(contours[0], SketchPlane::XY, {80, 15}), "Point in another region must not select first");
        require(contourContains(contours[1], SketchPlane::XY, {80, 15}), "Second region must have its own geometric hit test");
        require(!contourContains(contours[0], SketchPlane::XY, {50, 15})
                && !contourContains(contours[1], SketchPlane::XY, {50, 15}), "Point between profiles must select none");
        require(contours[0].entityIds == std::vector<std::string>{"left-rectangle"}
                && contours[1].entityIds == std::vector<std::string>{"right-rectangle"}, "Contours must retain stable source IDs");
        const auto selected = selectContour(selectable, {"right-rectangle"});
        require(selected.entities.size() == 1 && selected.entities[0].id == "right-rectangle",
                "Explicit region selection must retain only referenced geometry");
        requireNear(volume(makeExtrusion(selected, 10, Direction::Positive)), 12000);
        rejected([&] { selectContour(selectable, {}); });
        rejected([&] { selectContour(selectable, {"missing"}); });
        rejected([&] { selectContour(selectable, {"open-line"}); });
        rejected([&] { selectContour(selectable, {"left-rectangle", "right-rectangle"}); });
        rejected([&] { selectContour(selectable, {"left-rectangle", "left-rectangle"}); });
        auto nested = rectangle();
        nested.entities.push_back(circle().entities[0]);
        rejected([&] { closedContours(nested); });
        auto touching = rectangle();
        secondRectangle.first = {40, 0};
        secondRectangle.second = {60, 30};
        touching.entities.push_back(secondRectangle);
        rejected([&] { closedContours(touching); });
        Sketch triangle{SketchPlane::XY, {
            {EntityKind::Line, {0, 0}, {10, 0}, 1, "t1"},
            {EntityKind::Line, {10, 0}, {0, 10}, 1, "t2"},
            {EntityKind::Line, {0, 10}, {0, 0}, 1, "t3"}}};
        const auto triangularContour = closedContours(triangle);
        require(triangularContour.size() == 1, "A closed triangle must be selectable");
        require(contourContains(triangularContour[0], SketchPlane::XY, {1, 1}), "Triangle interior must be selectable");
        require(!contourContains(triangularContour[0], SketchPlane::XY, {9, 9}),
                "Hit testing must reject a point inside the bounding box but outside the actual region");
        // A second additive feature must create one touching/fused body.
        requireNear(volume(makeExtrusion(profile, 35, Direction::Positive, original)), 40 * 30 * 35);
        auto remote = rectangle();
        remote.entities[0].first.x = 100;
        remote.entities[0].second.x = 140;
        rejected([&] { makeExtrusion(remote, 10, Direction::Positive, original); });
        rejected([&] { makeCut(TopoDS_Shape(), circle(), 10, false, Direction::Positive); });
        rejected([&] { makeCut(original, circle(), 0, false, Direction::Positive); });
        rejected([&] { makeCut(original, remote, 10, false, Direction::Positive); });
        rejected([&] { makeCut(original, circle(), 10, true, Direction::Negative); });
        rejected([&] { makeCut(original, profile, 10, true, Direction::Positive); });
        auto split = rectangle();
        split.entities[0].first = {19, -1};
        split.entities[0].second = {21, 31};
        rejected([&] { makeCut(original, split, 10, true, Direction::Positive); });
        std::ostringstream afterOperations;
        BRepTools::Write(original, afterOperations);
        if (originalBrep.str() != afterOperations.str()) {
            const auto before = originalBrep.str(), after = afterOperations.str();
            std::size_t different = 0;
            while (different < before.size() && different < after.size() && before[different] == after[different]) { ++different; }
            std::cerr << "BRep difference at " << different << ": BEFORE " << before.substr(different, 160)
                      << " AFTER " << after.substr(different, 160) << '\n';
        }
        require(originalBrep.str() == afterOperations.str(), "Preview/recompute Boolean operations must not mutate source topology or tolerances");
        requireNear(volume(original), 30000);
        std::cout << "PASS: all planes/directions, line/rectangle/circle profiles, exact volumes, cut/regeneration, and invalid-profile/Boolean rejection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
