#include "document/PartDocument.h"
#include "modeling/ConstraintSolver.h"
#include "modeling/SketchModifiers.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace thecad::modeling;
using namespace thecad::document;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void near(Point2 point, Point2 expected, const char* message)
{
    require(std::hypot(point.x - expected.x, point.y - expected.y) < 1e-6, message);
}

const SketchEntity& item(const Sketch& sketch, const std::string& id)
{
    const auto found = std::find_if(sketch.entities.begin(), sketch.entities.end(),
                                   [&](const auto& e) { return e.id == id; });
    require(found != sketch.entities.end(), "Sketch entity missing.");
    return *found;
}

void copies()
{
    Sketch source;
    source.entities = {{EntityKind::Line, {0, -10}, {0, 10}, 1, "axis", true},
                       {EntityKind::Circle, {6, 2}, {}, 2, "circle"}};
    source.relations = {{"axis-start", RelationKind::FixedPoint, {"axis", PointRole::First}, {}, {0, -10}},
                        {"axis-end", RelationKind::FixedPoint, {"axis", PointRole::Second}, {}, {0, 10}}};
    SketchPattern mirror;
    mirror.kind = SketchModifier::Mirror;
    mirror.mirrorLine = "axis";
    auto mirrored = replicateSketchEntities(source, {"circle"}, mirror);
    const auto image = mirrored.replications.front().target;
    near(item(mirrored, image).first, {-6, 2}, "Mirror center is incorrect.");
    auto seed = item(mirrored, "circle");
    seed.first = {8, 5};
    seed.radius = 4;
    mirrored = editSketchEntity(mirrored, seed);
    near(item(mirrored, image).first, {-8, 5}, "Mirror must follow its seed.");
    require(std::abs(item(mirrored, image).radius - 4) < 1e-6, "Mirror must retain the radius.");

    SketchPattern linear;
    linear.angle = 90;
    linear.spacing = 10;
    linear.count = 3;
    linear.reverse = true;
    auto patterned = replicateSketchEntities(source, {"circle"}, linear);
    require(patterned.replications.size() == 2, "Count must include the seed.");
    near(patterned.entities.back().first, {6, -18}, "Linear spacing or direction is incorrect.");
    seed = item(patterned, "circle");
    seed.first = {11, 4};
    patterned = editSketchEntity(patterned, seed);
    near(patterned.entities.back().first, {11, -16}, "Linear instances must follow the seed.");

    SketchPattern circular;
    circular.kind = SketchModifier::CircularPattern;
    circular.center = {1, 2};
    circular.angle = 120;
    circular.count = 3;
    auto arc = replicateSketchEntities(source, {"circle"}, circular);
    near(arc.entities[2].first, {3.5, 2 + 5 * std::sqrt(3.) / 2}, "Circular instance is incorrect.");
    seed = item(arc, "circle");
    seed.first = {9, 2};
    arc = editSketchEntity(arc, seed);
    near(arc.entities[2].first, {5, 2 + 4 * std::sqrt(3.)}, "Circular copies must follow their seed.");
    near(arc.entities[3].first, {-3, 2 + 4 * std::sqrt(3.)}, "Circular total angle is incorrect.");
    auto full = circular;
    full.angle = 360;
    full.count = 4;
    const auto around = replicateSketchEntities(source, {"circle"}, full);
    require(around.replications.size() == 3, "A full revolution must not repeat the seed.");
    near(around.entities.back().first, {1, -3}, "Full circle spacing is incorrect.");

    QTemporaryDir temp;
    auto part = PartDocument::createEmpty();
    part.addSketch(arc);
    const auto file = temp.filePath("patterns.thepart");
    part.save(file);
    auto loaded = PartDocument::load(file);
    require(loaded.features()[0].sketch.replications.size() == 2,
            "Reopened patterns must retain associations.");
    auto released = removeSketchEntity(loaded.features()[0].sketch, "circle");
    require(released.replications.empty() && released.entities.size() == 3,
            "Deleting a seed must release its existing copies.");
}

void stationaryMirrorAxis()
{
    for (int variant = 0; variant < 5; ++variant)
    {
        Sketch source;
        source.entities = {{EntityKind::Line, {0, -20}, {0, 30}, 1, "axis", true},
                           {EntityKind::Line, {10, 5}, {22, 18}, 1, "seed"}};
        if (variant == 1)
            source.relations.push_back({"vertical", RelationKind::Vertical,
                {"axis", PointRole::First}, {"axis", PointRole::Second}});
        if (variant == 2)
        {
            source.relations.push_back({"anchor", RelationKind::FixedPoint,
                {"axis", PointRole::First}, {}, {0, -20}});
            SketchDimension length;
            length.id = "axis-length"; length.kind = DimensionKind::LineLength;
            length.first = {"axis", PointRole::First}; length.second = {"axis", PointRole::Second};
            length.value = 50; source.dimensions.push_back(length);
        }
        if (variant == 3) source.entities[0].second = {20, 30};
        if (variant == 4) { source.entities[0].first={-30,-20}; source.entities[0].second={40,-20}; }
        SketchPattern parameters;
        parameters.kind = SketchModifier::Mirror; parameters.mirrorLine = "axis";
        const auto original = replicateSketchEntities(source, {"seed"}, parameters);
        const auto image = original.replications.front().target;
        const auto check = [&](const Sketch& moved) {
            near(item(moved, "axis").first, item(original, "axis").first, "Mirror axis start moved during a drag.");
            near(item(moved, "axis").second, item(original, "axis").second, "Mirror axis end moved during a drag.");
            require(moved.relations.size() == original.relations.size(), "Drag anchors must not persist as relations.");
            require(solvePlanarSketch(moved).success, "A mirrored drag must retain valid solver geometry.");
            for (const auto role : {PointRole::First, PointRole::Second})
            {
                const auto a = sketchPointValue(moved, {"seed", role});
                const auto b = sketchPointValue(moved, {image, role});
                const auto axis = item(moved, "axis");
                const double dx = axis.second.x-axis.first.x, dy = axis.second.y-axis.first.y;
                require(std::abs(((a.x+b.x)/2-axis.first.x)*dy-((a.y+b.y)/2-axis.first.y)*dx)<1e-5,
                    "Mirror midpoint must remain on the axis.");
                require(std::abs((a.x-b.x)*dx+(a.y-b.y)*dy)<1e-5,
                    "Mirrored points must remain symmetric.");
            }
        };
        for (const auto& id : {std::string("seed"), image})
        {
            const auto start = item(original, id).first;
            auto moved = moveSketchPoint(original, {id, PointRole::First}, {start.x+3, start.y+4});
            near(item(moved, id).first, {start.x+3, start.y+4}, "A mirrored endpoint must reach the cursor.");
            check(moved);
            const auto line = item(original, id);
            const Point2 target{(line.first.x+line.second.x)/2+4, (line.first.y+line.second.y)/2+3};
            moved = moveSketchLine(original, id, .5, target);
            check(moved);
            near({(item(moved,id).first.x+item(moved,id).second.x)/2,
                  (item(moved,id).first.y+item(moved,id).second.y)/2}, target, "Mirrored line drag must reach the cursor.");
            moved = moveSketchPoint(moved, {id, PointRole::Second}, {item(moved,id).second.x+1, item(moved,id).second.y+2});
            check(moved);
        }
        if (variant == 0)
        {
            const auto moved = moveSketchLine(original, "axis", .5, {5, 7});
            require(std::abs(item(moved,"axis").first.x)>1, "The mirror axis must remain directly editable.");
            auto replacement = item(original,"seed"); replacement.first={15,8}; replacement.second={27,21};
            check(editSketchEntity(original,replacement));
            SketchPattern linear;linear.count=2;linear.spacing=30;
            const auto chain=replicateSketchEntities(original,{image},linear);
            check(moveSketchPoint(chain,{chain.entities.back().id,PointRole::First},{12,9}));
            QTemporaryDir temp;
            auto part = PartDocument::createEmpty(); part.addSketch(original);
            const auto path = temp.filePath("free-mirror.thepart"); part.save(path);
            const auto reopened = PartDocument::load(path).features().front().sketch;
            check(moveSketchLine(reopened,"seed",.5,{20,15}));
        }
    }
    for(const auto kind:{EntityKind::Circle,EntityKind::Point}) {
        Sketch source;
        source.entities={{EntityKind::Line,{0,-20},{0,30},1,"axis",true},{kind,{10,8},{},3,"seed"}};
        SketchPattern mirror;mirror.kind=SketchModifier::Mirror;mirror.mirrorLine="axis";
        const auto original=replicateSketchEntities(source,{"seed"},mirror);
        for(const auto& id:{std::string("seed"),original.replications.front().target}) {
            const auto before=item(original,id).first;
            const auto moved=moveSketchPoint(original,{id,kind==EntityKind::Circle?PointRole::Center:PointRole::First},
                {before.x+4,before.y+3},{},kind==EntityKind::Circle);
            near(item(moved,"axis").first,{0,-20},"Moving a mirrored circle/point moved the axis.");
            near(item(moved,"axis").second,{0,30},"Moving a mirrored circle/point rotated the axis.");
            near(item(moved,id).first,{before.x+4,before.y+3},"The mirrored circle/point did not reach its target.");
            if(kind==EntityKind::Circle)require(std::abs(item(moved,id).radius-3)<1e-7,"Circle dragging changed its radius.");
        }
    }
}

void projectedBoundary()
{
    auto part = PartDocument::createBox({40, 25, 10});
    const auto options = part.referenceOptions();
    const auto top = std::find_if(options.begin(), options.end(), [](const auto& ref) {
        return ref.reference.source == ReferenceSource::BodyFace &&
               ref.geometry.kind == DatumKind::Plane &&
               std::abs(ref.geometry.frame.Location().Z() - 10) < 1e-6 &&
               std::abs(ref.geometry.frame.Direction().Z()) > .99;
    });
    require(top != options.end(), "Expected a named top face.");
    const auto sketchId = part.addSketch(part.sketchOnFace(top->reference.index));
    auto feature = part.features().back();
    const auto references = part.sketchBoundaryReferences(feature.sketch);
    require(references.size() == 4, "Expected all four supporting edges.");
    const auto longEdge = std::find_if(references.begin(), references.end(), [](const auto& option) {
        const auto& line = option.geometry;
        return std::hypot(line.second.x - line.first.x, line.second.y - line.first.y) > 35;
    });
    require(longEdge != references.end(), "Expected a reference along the box width.");
    const auto id = useSketchProjection(feature.sketch, *longEdge, true);
    const auto midpoint = sketchPointValue(feature.sketch, {id, PointRole::Midpoint});
    const auto originalMidpoint = sketchPoint(feature.sketch, midpoint);
    feature.sketch.entities.push_back({EntityKind::Point, midpoint, {}, 1, "snap"});
    feature.sketch.relations.push_back({"mid", RelationKind::Midpoint, {"snap", PointRole::First},
                                        {id, PointRole::First}});
    part.replaceFeature(feature);
    require(closedContours(part.features().back().sketch).empty(),
            "Construction references must stay out of the profile.");
    auto resized = part.features().front();
    resized.box.width = 60;
    part.replaceFeature(resized);
    feature = part.features().back();
    const auto moved = sketchPointValue(feature.sketch, {id, PointRole::Midpoint});
    near(item(feature.sketch, "snap").first, moved, "A snapped point must follow its projected edge.");
    require(sketchPoint(feature.sketch, moved).Distance(originalMidpoint) > 1,
            "The referenced midpoint should move after resizing the source.");

    feature.sketch = part.convertSketchGeometry(sketchId, feature.sketch, {}, false, false);
    require(feature.sketch.projections.size() == 4 && !item(feature.sketch, id).construction,
            "Convert Entities must reuse and promote the existing linked reference.");
    part.replaceFeature(feature);
    const auto contours = closedContours(part.features().back().sketch);
    require(contours.size() == 1, "Converted edges must make a usable closed profile.");
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(contours.front().face, properties);
    require(std::abs(properties.Mass() - 1500) < 1e-6, "Converted outline area is incorrect.");
    require(analyzeSketchDefinition(part.features().back().sketch).degreesOfFreedom == 0,
            "Converted geometry and linked midpoint must be fully defined.");

    QTemporaryDir temp;
    const auto file = temp.filePath("converted.thepart");
    part.save(file);
    const auto reopened = PartDocument::load(file);
    require(reopened.features().back().sketch.projections.size() == 4,
            "Converted edges must survive reopening.");
}

void innerLoopAndDimension()
{
    auto part = PartDocument::createEmpty();
    Sketch profile;
    profile.entities = {{EntityKind::Rectangle, {-20, -20}, {20, 20}, 1, "outline"}};
    const auto profileId = part.addSketch(profile);
    part.addExtrude(profileId, {"outline"}, 10, Direction::Positive);
    Sketch hole;
    hole.entities = {{EntityKind::Circle, {}, {}, 5, "hole"}};
    const auto holeId = part.addSketch(hole);
    part.addCut(holeId, {"hole"}, 10, true, Direction::Positive);
    const auto options = part.referenceOptions();
    const auto top = std::find_if(options.begin(), options.end(), [](const auto& ref) {
        return ref.reference.source == ReferenceSource::BodyFace &&
               ref.geometry.kind == DatumKind::Plane &&
               std::abs(ref.geometry.frame.Location().Z() - 10) < 1e-6;
    });
    require(top != options.end(), "Expected a named top face with an inner loop.");
    const auto id = part.addSketch(part.sketchOnFace(top->reference.index));
    const auto sketch = part.features().back().sketch;
    require(part.convertSketchGeometry(id, sketch, {}, false, false).projections.size() == 4,
            "Default face conversion should select only the outer loop.");
    auto converted = part.convertSketchGeometry(id, sketch, {}, true, true);
    require(converted.projections.size() == 5, "Include inner loops should convert the circular hole.");
    const auto circle = std::find_if(converted.entities.begin(), converted.entities.end(), [](const auto& item) {
        return item.kind == EntityKind::Circle;
    });
    require(circle != converted.entities.end() && circle->radius == 5,
            "The circular edge should retain its radius.");
    SketchDimension dimension;
    dimension.id = "model-distance";
    dimension.kind = DimensionKind::HorizontalDistance;
    dimension.first = {circle->id, PointRole::Center};
    dimension.second = {"point", PointRole::First};
    dimension.value = 12;
    converted.entities.push_back({EntityKind::Point, {circle->first.x + 8, circle->first.y}, {}, 1, "point"});
    const auto solved = setSketchDimension(converted, dimension);
    require(std::abs(dimensionMeasurement(solved, dimension) - 12) < 1e-6,
            "A driving dimension must be able to use a fixed model reference.");
}
}

int main()
{
    try
    {
        copies();
        stationaryMirrorAxis();
        projectedBoundary();
        innerLoopAndDimension();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
