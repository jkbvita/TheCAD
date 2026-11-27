#include "modeling/SketchOffset.h"
#include "modeling/ConstraintSolver.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace thecad::modeling
{
namespace
{
constexpr double tolerance = 1e-6;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Point2 add(Point2 a, Point2 b) { return {a.x + b.x, a.y + b.y}; }
Point2 subtract(Point2 a, Point2 b) { return {a.x - b.x, a.y - b.y}; }
Point2 scale(Point2 p, double value) { return {p.x * value, p.y * value}; }
double dot(Point2 a, Point2 b) { return a.x * b.x + a.y * b.y; }
double cross(Point2 a, Point2 b) { return a.x * b.y - a.y * b.x; }
double length(Point2 p) { return std::hypot(p.x, p.y); }
bool near(Point2 a, Point2 b) { return length(subtract(a, b)) <= tolerance; }

const SketchEntity& entity(const Sketch& sketch, const std::string& id)
{
    const auto found = std::find_if(sketch.entities.begin(), sketch.entities.end(),
                                    [&](const auto& item) { return item.id == id; });
    require(found != sketch.entities.end(), "Offset references missing sketch geometry.");
    return *found;
}

std::string nextId(const Sketch& sketch)
{
    for (std::size_t i = sketch.entities.size() + 1;; ++i)
    {
        auto id = "offset-" + std::to_string(i);
        if (std::none_of(sketch.entities.begin(), sketch.entities.end(),
                         [&](const auto& item) { return item.id == id; }))
            return id;
    }
}

bool touches(const SketchEntity& a, const SketchEntity& b)
{
    return near(a.first, b.first) || near(a.first, b.second) ||
           near(a.second, b.first) || near(a.second, b.second);
}

Sketch::Offset chain(const Sketch& sketch, const std::vector<std::string>& ids, bool expand)
{
    std::vector<const SketchEntity*> lines;
    std::set<std::string> selected;
    for (const auto& id : ids)
    {
        const auto& item = entity(sketch, id);
        require(item.kind == EntityKind::Line && selected.insert(id).second,
                "Select a single circle or a chain of distinct lines.");
        lines.push_back(&item);
    }
    if (expand)
        for (std::size_t i = 0; i < lines.size(); ++i)
            for (const auto& item : sketch.entities)
                if (item.kind == EntityKind::Line && item.construction == lines[i]->construction &&
                    touches(*lines[i], item) && selected.insert(item.id).second)
                    lines.push_back(&item);

    struct Node { Point2 point; std::vector<std::size_t> edges; };
    std::vector<Node> nodes;
    const auto nodeFor = [&](Point2 point) {
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (near(nodes[i].point, point))
                return i;
        nodes.push_back({point, {}});
        return nodes.size() - 1;
    };
    std::vector<std::pair<std::size_t, std::size_t>> ends;
    for (const auto* line : lines)
    {
        const auto first = nodeFor(line->first), second = nodeFor(line->second);
        require(first != second, "An offset source segment is too short.");
        nodes[first].edges.push_back(ends.size());
        nodes[second].edges.push_back(ends.size());
        ends.emplace_back(first, second);
    }
    std::vector<std::size_t> tips;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        require(nodes[i].edges.size() <= 2, "An offset chain cannot branch. Select the intended lines.");
        if (nodes[i].edges.size() == 1)
            tips.push_back(i);
    }
    require(tips.empty() || tips.size() == 2, "Select one connected chain of lines.");
    Sketch::Offset result;
    result.closed = tips.empty();
    std::vector<bool> used(lines.size());
    std::size_t current = result.closed ? ends.front().first : tips.front();
    while (result.segments.size() < lines.size())
    {
        const auto& incident = nodes[current].edges;
        const auto next = std::find_if(incident.begin(), incident.end(), [&](auto index) { return !used[index]; });
        require(next != incident.end(), "Select one connected chain of lines.");
        const auto edge = *next;
        used[edge] = true;
        const bool reversed = ends[edge].first != current;
        result.segments.push_back({lines[edge]->id, {}, reversed});
        current = reversed ? ends[edge].first : ends[edge].second;
    }
    const auto firstPick = std::find_if(result.segments.begin(), result.segments.end(),
                                       [&](const auto& segment) { return segment.source == ids.front(); });
    if (firstPick->reversed)
    {
        std::reverse(result.segments.begin(), result.segments.end());
        for (auto& segment : result.segments)
            segment.reversed = !segment.reversed;
    }
    return result;
}

struct Line
{
    Point2 first, last;
};

Line oriented(const SketchEntity& item, bool reverse)
{
    return reverse ? Line{item.second, item.first} : Line{item.first, item.second};
}

// Includes touching and overlapping collinear non-neighboring segments.
bool intersects(Line a, Line b)
{
    const auto u = subtract(a.last, a.first), v = subtract(b.last, b.first);
    const auto offset = subtract(b.first, a.first);
    const double ul = length(u), vl = length(v);
    const double denominator = cross(u, v);
    if (std::abs(denominator) > ul * vl * 1e-10)
    {
        const double s = cross(offset, v) / denominator, t = cross(offset, u) / denominator;
        return s >= -tolerance / ul && s <= 1 + tolerance / ul &&
               t >= -tolerance / vl && t <= 1 + tolerance / vl;
    }
    if (std::abs(cross(offset, u)) > ul * tolerance)
        return false;
    const double start = dot(offset, u) / ul, end = dot(subtract(b.last, a.first), u) / ul;
    return std::max(start, end) >= -tolerance && std::min(start, end) <= ul + tolerance;
}

std::vector<Line> offsetLines(const Sketch& sketch, const Sketch::Offset& group)
{
    std::vector<Line> seeds, result;
    std::vector<Point2> units;
    for (const auto& segment : group.segments)
    {
        const auto line = oriented(entity(sketch, segment.source), segment.reversed);
        const auto delta = subtract(line.last, line.first);
        const double size = length(delta);
        require(size > tolerance, "An offset source segment is too short.");
        const auto direction = scale(delta, 1 / size);
        const auto shift = scale({-direction.y, direction.x}, group.distance);
        seeds.push_back(line);
        result.push_back({add(line.first, shift), add(line.last, shift)});
        units.push_back(direction);
    }
    const std::size_t count = seeds.size();
    const std::size_t joins = group.closed ? count : count - 1;
    for (std::size_t i = 0; i < joins; ++i)
    {
        const auto next = (i + 1) % count;
        require(near(seeds[i].last, seeds[next].first), "The offset source chain is no longer connected.");
        const double denominator = cross(units[i], units[next]);
        require(std::abs(denominator) > 1e-8,
                "An offset chain needs a distinct corner between each pair of lines.");
        const double distance = cross(subtract(result[next].first, result[i].first), units[next]) /
                                denominator;
        const auto corner = add(result[i].first, scale(units[i], distance));
        result[i].last = corner;
        result[next].first = corner;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        require(dot(subtract(result[i].last, result[i].first), units[i]) > tolerance,
                "The offset collapses or reverses a segment. Reduce the distance or reverse it.");
        for (std::size_t j = i + 2; j < count; ++j)
        {
            if (group.closed && i == 0 && j == count - 1)
                continue;
            require(!intersects(seeds[i], seeds[j]), "The selected chain intersects itself.");
            require(!intersects(result[i], result[j]), "The offset intersects itself. Reduce the distance.");
        }
    }
    return result;
}

double outwardSign(const Sketch& sketch, const Sketch::Offset& group)
{
    if (!group.closed)
        return 1;
    const auto anchor = entity(sketch, group.segments.front().source).first;
    double area = 0;
    for (const auto& segment : group.segments)
    {
        const auto line = oriented(entity(sketch, segment.source), segment.reversed);
        area += cross(subtract(line.first, anchor), subtract(line.last, anchor));
    }
    require(std::abs(area) > tolerance * tolerance, "The offset loop has no enclosed area.");
    return area > 0 ? -1 : 1;
}
} // namespace

void validateSketchOffsets(const Sketch& sketch)
{
    if (sketch.offsets.empty())
        return;
    std::map<std::string, std::vector<std::string>> parents;
    for (const auto& copy : sketch.replications)
    {
        auto& refs = parents[copy.target];
        refs.push_back(copy.source);
        if (!copy.mirrorLine.empty())
            refs.push_back(copy.mirrorLine);
    }
    for (const auto& link : sketch.projections)
        parents.emplace(link.entityId, std::vector<std::string>{});
    for (std::size_t i = 0; i < sketch.offsets.size(); ++i)
    {
        const auto& group = sketch.offsets[i];
        require(!group.segments.empty() && group.segments.size() <= 1000 &&
                    (!group.closed || group.segments.size() >= 3) &&
                    std::isfinite(group.distance) && std::abs(group.distance) > tolerance &&
                    std::abs(group.distance) <= 1e9, "Invalid sketch offset group.");
        std::set<std::string> sources;
        // The group node represents the shared dependencies of all mitered segments.
        const auto groupKey = "@offset-" + std::to_string(i);
        for (const auto& segment : group.segments)
        {
            const auto& source = entity(sketch, segment.source);
            const auto& target = entity(sketch, segment.target);
            require(source.kind == target.kind && (source.kind == EntityKind::Line ||
                        (source.kind == EntityKind::Circle && group.segments.size() == 1 &&
                         !group.closed && !segment.reversed)) && sources.insert(source.id).second &&
                        parents.emplace(target.id, std::vector<std::string>{groupKey}).second,
                    "Invalid or duplicate sketch offset references.");
        }
        parents[groupKey].assign(sources.begin(), sources.end());
    }
    std::map<std::string, int> state;
    const std::function<void(const std::string&)> visit = [&](const auto& id) {
        if (state[id] == 2)
            return;
        require(state[id] != 1, "Sketch offset references cannot be cyclic.");
        state[id] = 1;
        if (const auto found = parents.find(id); found != parents.end())
            for (const auto& parent : found->second)
                visit(parent);
        state[id] = 2;
    };
    for (const auto& [id, sources] : parents)
        visit(id);
}

void validateSketchOffsetGeometry(const Sketch& sketch)
{
    for (const auto& group : sketch.offsets)
    {
        const auto& source = entity(sketch, group.segments.front().source);
        if (source.kind == EntityKind::Circle)
        {
            const auto& target = entity(sketch, group.segments.front().target);
            require(source.radius + group.distance > tolerance && near(source.first, target.first) &&
                        std::abs(target.radius - source.radius - group.distance) <= tolerance,
                    "The source circle cannot produce a valid offset at this distance.");
        }
        else
        {
            const auto lines = offsetLines(sketch, group);
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                const auto& target = entity(sketch, group.segments[i].target);
                require(near(target.first, lines[i].first) && near(target.second, lines[i].last),
                        "The solved offset no longer matches the selected side and corners.");
            }
        }
    }
}

Sketch offsetSketchEntities(const Sketch& sketch, const std::vector<std::string>& ids,
                            const SketchOffsetParameters& parameters)
{
    require(!ids.empty(), "Select a circle or one connected chain of lines.");
    require(std::isfinite(parameters.distance) && parameters.distance > tolerance &&
                parameters.distance <= 1e9, "Enter a positive offset distance.");
    const auto& seed = entity(sketch, ids.front());
    const bool circle = seed.kind == EntityKind::Circle;
    Sketch::Offset definition;
    if (circle)
    {
        require(ids.size() == 1, "Select a single circle or a chain of lines.");
        definition.segments = {{seed.id, {}, false}};
    }
    else
        definition = chain(sketch, ids, parameters.selectChain);
    const auto copies = parameters.bothSides ? std::size_t(2) : std::size_t(1);
    require(sketch.entities.size() + definition.segments.size() * copies <= 1000,
            "The offset exceeds the 1000-entity sketch limit.");

    auto result = sketch;
    definition.distance = parameters.distance * (parameters.reverse ? -1 : 1) *
                          outwardSign(sketch, definition);
    for (std::size_t side = 0; side < copies; ++side)
    {
        auto group = definition;
        if (side != 0)
            group.distance = -group.distance;
        const auto lines = circle ? std::vector<Line>{} : offsetLines(sketch, group);
        for (std::size_t i = 0; i < group.segments.size(); ++i)
        {
            auto& segment = group.segments[i];
            auto copy = entity(sketch, segment.source);
            segment.target = copy.id = nextId(result);
            copy.construction = parameters.construction || copy.construction;
            if (circle)
            {
                copy.radius += group.distance;
                require(copy.radius > tolerance, "The offset radius must remain positive.");
            }
            else
            {
                copy.first = lines[i].first;
                copy.second = lines[i].last;
            }
            require(std::none_of(result.entities.begin(), result.entities.end(), [&](const auto& item) {
                if (item.kind != copy.kind || item.construction != copy.construction)
                    return false;
                if (circle)
                    return near(item.first, copy.first) && std::abs(item.radius - copy.radius) <= tolerance;
                return (near(item.first, copy.first) && near(item.second, copy.second)) ||
                       (near(item.first, copy.second) && near(item.second, copy.first));
            }), "The offset coincides with an existing sketch entity.");
            result.entities.push_back(copy);
        }
        result.offsets.push_back(std::move(group));
    }
    return requireSolvedSketch(solvePlanarSketch(result));
}
} // namespace thecad::modeling
