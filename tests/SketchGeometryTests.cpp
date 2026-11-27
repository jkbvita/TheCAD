#include "modeling/Sketch.h"
#include "modeling/SketchSnapping.h"
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


Sketch constrainedRectangle()
{
    Sketch sketch{SketchPlane::XY, {
        {EntityKind::Line, {0,0}, {40,0}, 1, "a"},
        {EntityKind::Line, {40,0}, {40,30}, 1, "b"},
        {EntityKind::Line, {40,30}, {0,30}, 1, "c"},
        {EntityKind::Line, {0,30}, {0,0}, 1, "d"}}};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& entity = sketch.entities[i];
        sketch.relations.push_back({"join-" + std::to_string(i), RelationKind::Coincident,
            {entity.id, PointRole::Second}, {sketch.entities[(i + 1) % 4].id, PointRole::First}});
        sketch.relations.push_back({"axis-" + std::to_string(i), i % 2 ? RelationKind::Vertical : RelationKind::Horizontal,
            {entity.id, PointRole::First}, {entity.id, PointRole::Second}});
    }
    sketch.relations.push_back({"origin", RelationKind::Coincident, {"a", PointRole::First}, {}});
    return sketch;
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
        require(inferLineDirection({0,0},{50,0.1},2,300,300).relation==RelationKind::Horizontal,"Near-horizontal screen direction must infer horizontal");
        require(inferLineDirection({0,0},{0.1,-50},300,2,300).relation==RelationKind::Vertical,"Negative vertical direction must infer vertical");
        require(!inferLineDirection({0,0},{20,20},100,100,141).relation,"A diagonal must remain free");
        require(!inferLineDirection({0,0},{1,0.2},4,19,20).relation,"Short diagonal lines must not be forced onto an axis");
        Sketch separate{SketchPlane::XY, {
            {EntityKind::Line,{-40,-10},{-20,0},1,"drag-a"},
            {EntityKind::Line,{10,20},{35,35},1,"drag-b"}}};
        const auto joined=moveSketchPoint(separate,{"drag-a",PointRole::Second},{10.1,20.1},SketchPointRef{"drag-b",PointRole::First});
        require(joined.entities[0].second.x==10 && joined.entities[0].second.y==20,"Joining must use exact target coordinates");
        require(joined.relations.size()==1 && joined.relations[0].kind==RelationKind::Coincident,"Joining creates a persistent coincident relation");
        require(areSketchPointsCoincident(joined,{"drag-a",PointRole::Second},{"drag-b",PointRole::First}),"Stable point refs must stay connected");
        const auto moved=moveSketchPoint(joined,{"drag-a",PointRole::Second},{-5,25});
        require(moved.entities[1].first.x==-5 && moved.entities[1].first.y==25,"Moving a connected endpoint propagates its connection");
        const auto pinned=moveSketchPoint(separate,{"drag-a",PointRole::Second},{0.1,0.1},SketchPointRef{});
        require(pinned.entities[0].second.x==0 && pinned.entities[0].second.y==0,"Origin joining must be exact");
        rejected([&]{moveSketchPoint(pinned,{"drag-a",PointRole::Second},{1,2});});
        rejected([&]{moveSketchPoint(pinned,{}, {1,2});});
        rejected([&]{moveSketchPoint(separate,{"missing",PointRole::Second},{1,2});});
        const auto resizedCorner=moveSketchPoint(constrainedRectangle(),{"a",PointRole::Second},{45,10});
        require(resizedCorner.entities[0].second.x==45 && resizedCorner.entities[0].second.y==0,"Adjacent corner dragging projects onto its free coordinate");
        require(separate.entities[0].second.x==-20 && separate.relations.empty(),"Failed and successful point moves never mutate the source");
        // Exact persistent point equalities survive edits without a general solver.
        auto constrained = constrainedRectangle();
        validateSketchRelations(constrained);
        require(sketchPoints(constrained).size() == 9, "The origin and all line endpoints must have independent stable references");
        auto edited = constrained.entities[0]; edited.second.x = 55;
        const auto wider = editSketchEntity(constrained, edited);
        require(wider.entities[1].first.x == 55 && wider.entities[1].second.x == 55
            && wider.entities[2].first.x == 55, "Horizontal, vertical, and coincident groups must propagate exact coordinates");
        requireNear(volume(makeExtrusion(wider, 10, Direction::Positive)), 55 * 30 * 10);
        require(constrained.entities[1].first.x == 40, "Transactional edits must not mutate the source sketch");
        edited = constrained.entities[0]; edited.first.x = 1;
        rejected([&] { editSketchEntity(constrained, edited); });
        edited = constrained.entities[0]; edited.second = {0,0};
        rejected([&] { editSketchEntity(constrained, edited); });
        auto invalidRelations = constrained; invalidRelations.entities[1].first.x = 40.000000001;
        rejected([&] { sketchShape(invalidRelations); });
        invalidRelations = constrained; invalidRelations.relations[0].second.entityId = "missing";
        rejected([&] { validateSketchRelations(invalidRelations); });
        invalidRelations = constrained; invalidRelations.relations[0].second.role = PointRole::Center;
        rejected([&] { validateSketchRelations(invalidRelations); });
        invalidRelations = constrained; auto duplicateRelation = invalidRelations.relations[0]; duplicateRelation.id = "duplicate";
        invalidRelations.relations.push_back(duplicateRelation);
        rejected([&] { validateSketchRelations(invalidRelations); });
        invalidRelations = constrained; invalidRelations.relations[0].id = "origin";
        rejected([&] { validateSketchRelations(invalidRelations); });
        const auto removed = removeSketchEntity(constrained, "b");
        require(removed.entities.size() == 3 && removed.relations.size() == 6, "Deleting an entity must explicitly remove its relations");
        validateSketchRelations(removed);
        Sketch centeredCircle{SketchPlane::XY, {{EntityKind::Circle, {0,0}, {}, 5, "circle"}}};
        centeredCircle.relations.push_back({"center-at-origin", RelationKind::Coincident, {"circle", PointRole::Center}, {}});
        auto circleEdit = centeredCircle.entities[0]; circleEdit.radius = 8;
        require(editSketchEntity(centeredCircle, circleEdit).entities[0].radius == 8, "A fixed circle center must still allow radius editing");
        circleEdit.first.x = 1;
        rejected([&] { editSketchEntity(centeredCircle, circleEdit); });
        auto circleOnEndpoint = constrained;
        circleOnEndpoint.entities.push_back({EntityKind::Circle, {40,30}, {}, 4, "circle"});
        circleOnEndpoint.relations.push_back({"center-on-endpoint", RelationKind::Coincident,
            {"circle", PointRole::Center}, {"b", PointRole::Second}});
        edited = circleOnEndpoint.entities[0]; edited.second.x = 50;
        require(editSketchEntity(circleOnEndpoint, edited).entities.back().first.x == 50,
            "Circle-center connections must propagate with the connected line endpoint");
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
        requireNear(volume(makeExtrusion(mixed,10,Direction::Positive)),(1200-25*pi)*10);
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
        require(closedContours(nested).size()==2,"Nested boundaries create an annulus and an inner selectable region");
        auto touching = rectangle();
        secondRectangle.first = {40, 0};
        secondRectangle.second = {60, 30};
        touching.entities.push_back(secondRectangle);
        const auto adjacent = closedContours(touching);
        require(adjacent.size() == 2, "Rectangles sharing a complete side must remain selectable.");
        requireNear(volume(makeExtrusions(touching, {adjacent[0].entityIds, adjacent[1].entityIds},
                                          10, Direction::Positive)), 18000);
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
