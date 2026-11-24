#include "modeling/assembly/MateEquations.h"
#include <gp_Quaternion.hxx>
#include <math_Matrix.hxx>
#include <math_Vector.hxx>
#include <math_SVD.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <map>
#include <numeric>

namespace thecad::modeling::assembly
{
namespace
{
struct State
{
    std::vector<gp_Trsf> poses;
    std::vector<std::vector<double>> coordinates;
};
struct Variable
{
    int body = -1, coordinate = 0, mate = -1;
};
void step(State& s, const Variable& v, double delta, double scale)
{
    if (v.body < 0)
    {
        s.coordinates[std::size_t(v.mate)][std::size_t(v.coordinate)] += delta;
        return;
    }
    auto& pose = s.poses[std::size_t(v.body)];
    const auto position = pose.TranslationPart();
    if (v.coordinate < 3)
    {
        auto t = position;
        t.SetCoord(v.coordinate + 1, t.Coord(v.coordinate + 1) + delta * scale);
        pose.SetTranslationPart(t);
    }
    else
    {
        const gp_Vec axis = v.coordinate == 3   ? gp_Vec(1, 0, 0)
                            : v.coordinate == 4 ? gp_Vec(0, 1, 0)
                                                : gp_Vec(0, 0, 1);
        pose.SetRotation(gp_Quaternion(axis, delta) * pose.GetRotation());
        pose.SetTranslationPart(position);
    }
}
MateResidual evaluate(const State& s, const std::vector<ResolvedMate>& mates, double scale,
                      bool diagnose = false)
{
    MateResidual out;
    for (std::size_t i = 0; i < mates.size(); ++i)
    {
        auto r = mateResidual(mates[i], s.poses, s.coordinates[i], scale, diagnose);
        out.values.insert(out.values.end(), r.values.begin(), r.values.end());
        out.tolerances.insert(out.tolerances.end(), r.tolerances.begin(), r.tolerances.end());
    }
    return out;
}
double cost(const MateResidual& r)
{
    double sum = 0;
    for (double v : r.values)
    {
        if (!std::isfinite(v))
            throw std::runtime_error("Mate equations produced a non-finite value.");
        sum += v * v;
    }
    return sum;
}
bool satisfied(const MateResidual& r)
{
    for (std::size_t i = 0; i < r.values.size(); ++i)
        if (!std::isfinite(r.values[i]) || std::abs(r.values[i]) > r.tolerances[i])
            return false;
    return true;
}
math_Matrix jacobian(const State& s, const std::vector<ResolvedMate>& mates,
                     const std::vector<Variable>& vars, double scale, bool diagnose = false)
{
    const int rows = int(evaluate(s, mates, scale, diagnose).values.size());
    math_Matrix j(1, rows, 1, int(vars.size()), 0.0);
    constexpr double h = 1e-6;
    for (std::size_t c = 0; c < vars.size(); ++c)
    {
        auto plus = s, minus = s;
        step(plus, vars[c], h, scale);
        step(minus, vars[c], -h, scale);
        const auto a = evaluate(plus, mates, scale, diagnose),
                   b = evaluate(minus, mates, scale, diagnose);
        if (a.values.size() != std::size_t(rows) || b.values.size() != std::size_t(rows))
            throw std::runtime_error("Mate equation dimensions changed during evaluation.");
        for (int r = 0; r < rows; ++r)
            j(r + 1, int(c) + 1) = (a.values[std::size_t(r)] - b.values[std::size_t(r)]) / (2 * h);
    }
    return j;
}
std::vector<std::vector<double>> rowBasis(const math_Matrix& matrix, double tolerance)
{
    std::vector<std::vector<double>> rows, basis;
    for (int r = 1; r <= matrix.RowNumber(); ++r)
    {
        std::vector<double> row;
        for (int c = 1; c <= matrix.ColNumber(); ++c)
            row.push_back(matrix(r, c));
        rows.push_back(std::move(row));
    }
    while (!rows.empty())
    {
        const auto norm = [](const auto& row)
        {
            double sum = 0;
            for (double v : row)
                sum += v * v;
            return sum;
        };
        const auto pivot =
            std::max_element(rows.begin(), rows.end(),
                             [&](const auto& a, const auto& b) { return norm(a) < norm(b); });
        const double length = std::sqrt(norm(*pivot));
        if (length < tolerance)
            break;
        auto direction = *pivot;
        rows.erase(pivot);
        for (auto& v : direction)
            v /= length;
        for (auto& row : rows)
            for (int pass = 0; pass < 2; ++pass)
            {
                double dot = 0;
                for (std::size_t c = 0; c < row.size(); ++c)
                    dot += row[c] * direction[c];
                for (std::size_t c = 0; c < row.size(); ++c)
                    row[c] -= dot * direction[c];
            }
        basis.push_back(std::move(direction));
    }
    return basis;
}
int rank(const math_Matrix& matrix, double tolerance)
{
    return int(rowBasis(matrix, tolerance).size());
}

} // namespace
static MateSolution solveMateBlock(const std::vector<RigidBody>& bodies,
                                   const std::vector<ResolvedMate>& input)
{
    if (bodies.size() > 64 || input.size() > 256)
        throw std::runtime_error("Assembly supports up to 64 components and 256 mates.");
    auto mates = input;
    State state;
    double scale = 1;
    for (const auto& b : bodies)
    {
        validateRigidPlacement(b.placement);
        if (!std::isfinite(b.size) || b.size <= 0)
            throw std::runtime_error("Invalid component size.");
        state.poses.push_back(b.placement);
        scale = std::max({scale, b.size, b.placement.TranslationPart().Modulus()});
    }
    std::vector<bool> participating(bodies.size(), false);
    state.coordinates.resize(mates.size());
    for (std::size_t i = 0; i < mates.size(); ++i)
    {
        auto& m = mates[i];
        if (!std::isfinite(m.value) || std::abs(m.value) > 1e9 || std::abs(m.alignment) != 1)
            throw std::runtime_error("Invalid mate value or alignment.");
        if (!m.definition)
        {
            m.definition = ResolvedMateDefinition{};
            m.definition->references = {{"first", m.first, mateGeometry(m.a)},
                                        {"second", m.second, mateGeometry(m.b)}};
        }
        prepareMate(m, state.poses);
        for (const auto& r : m.definition->references)
            if (r.body >= 0)
                participating[std::size_t(r.body)] = true;
        state.coordinates[i] = m.definition->motion.coordinates;
    }
    std::vector<Variable> variables;
    int inactiveFreedom = 0;
    std::vector<int> active;
    for (std::size_t b = 0; b < bodies.size(); ++b)
        if (!bodies[b].fixed)
        {
            if (participating[b])
            {
                active.push_back(int(b));
                for (int axis = 0; axis < 6; ++axis)
                    variables.push_back({int(b), axis, -1});
            }
            else
                inactiveFreedom += 6;
        }
    const int bodyColumns = int(variables.size());
    for (std::size_t m = 0; m < mates.size(); ++m)
        for (std::size_t q = 0; q < state.coordinates[m].size(); ++q)
            variables.push_back({-1, int(q), int(m)});
    auto current = evaluate(state, mates, scale);
    double damping = 1e-6;
    const int columns = int(variables.size());
    for (int iteration = 0; !satisfied(current) && iteration < 180 && columns > 0; ++iteration)
    {
        const auto j = jacobian(state, mates, variables, scale);
        const int rows = int(current.values.size());
        // Escape a singular equation only when its first derivative vanishes.
        // Probe one coordinate, rather than disturbing every free component.
        bool escaped = false;
        for (int row = 0; row < rows && !escaped; ++row)
        {
            if (std::abs(current.values[std::size_t(row)]) <= current.tolerances[std::size_t(row)])
                continue;
            double gradient = 0;
            for (int c = 1; c <= columns; ++c)
                gradient += j(row + 1, c) * j(row + 1, c);
            if (gradient > 1e-16)
                continue;
            double response = 0;
            int coordinate = -1;
            for (int c = 0; c < columns; ++c)
            {
                auto trial = state;
                step(trial, variables[std::size_t(c)], 1e-3, scale);
                const auto probe = evaluate(trial, mates, scale);
                const double change =
                    std::abs(probe.values[std::size_t(row)] - current.values[std::size_t(row)]);
                if (change > response)
                {
                    response = change;
                    coordinate = c;
                }
            }
            if (coordinate >= 0 && response > 1e-10)
            {
                step(state, variables[std::size_t(coordinate)], 1e-3, scale);
                current = evaluate(state, mates, scale);
                escaped = true;
            }
        }
        if (escaped)
            continue;
        math_Matrix augmented(1, rows + columns, 1, columns, 0.0);
        math_Vector right(1, rows + columns, 0.0), delta(1, columns, 0.0);
        for (int r = 1; r <= rows; ++r)
        {
            right(r) = -current.values[std::size_t(r - 1)];
            for (int c = 1; c <= columns; ++c)
                augmented(r, c) = j(r, c);
        }
        for (int c = 1; c <= columns; ++c)
            augmented(rows + c, c) = std::sqrt(damping);
        math_SVD svd(augmented);
        if (!svd.IsDone())
            throw std::runtime_error("Cannot decompose the assembly mate system.");
        svd.Solve(right, delta, 1e-10);
        double limit = 1;
        for (int c = 0; c < columns; ++c)
        {
            const auto& v = variables[std::size_t(c)];
            limit = std::max(limit, std::abs(delta(c + 1)) /
                                        (v.body >= 0 && v.coordinate < 3 ? 5.0 : 0.5));
        }
        auto trial = state;
        for (int c = 0; c < columns; ++c)
            step(trial, variables[std::size_t(c)], delta(c + 1) / limit, scale);
        auto next = evaluate(trial, mates, scale);
        if (cost(next) < cost(current) || satisfied(next))
        {
            state = std::move(trial);
            current = std::move(next);
            damping = std::max(1e-14, damping * 0.25);
        }
        else
        {
            damping *= 10;
            if (damping > 1e12)
                break;
        }
    }
    if (!satisfied(current))
    {
        std::string message = "Conflicting or unsolved mates:";
        for (const auto& m : mates)
            message += " " + m.id;
        throw std::runtime_error(message + ". The previous assembly is retained.");
    }
    MateSolution solution;
    solution.placements = state.poses;
    solution.degreesOfFreedom = inactiveFreedom;
    for (const auto& b : bodies)
        solution.fullyConstrained.push_back(b.fixed);
    if (bodyColumns > 0)
    {
        const auto j = jacobian(state, mates, variables, scale, true);
        const auto basis = rowBasis(j, 1e-7);
        math_Matrix bodyNull(1, bodyColumns, 1, columns, 0.0);
        for (int row = 1; row <= bodyColumns; ++row)
            for (int c = 1; c <= columns; ++c)
            {
                double v = row == c ? 1 : 0;
                for (const auto& direction : basis)
                    v -= direction[std::size_t(row - 1)] * direction[std::size_t(c - 1)];
                bodyNull(row, c) = v;
            }
        solution.degreesOfFreedom += rank(bodyNull, 1e-6);
        for (std::size_t b = 0; b < active.size(); ++b)
        {
            double freedom = 0;
            for (int axis = 0; axis < 6; ++axis)
                for (int c = 1; c <= columns; ++c)
                    freedom += std::pow(bodyNull(int(b) * 6 + axis + 1, c), 2);
            solution.fullyConstrained[std::size_t(active[b])] = freedom < 1e-10;
        }
    }
    for (std::size_t i = 0; i < mates.size(); ++i)
    {
        validateMateSolution(mates[i], state.coordinates[i]);
        auto motion = mates[i].definition->motion;
        motion.coordinates = state.coordinates[i];
        solution.motion.push_back(std::move(motion));
    }
    for (const auto& p : solution.placements)
        validateRigidPlacement(p);
    return solution;
}
MateSolution solveGeneralMates(const std::vector<RigidBody>& bodies,
                               const std::vector<ResolvedMate>& mates)
{
    std::vector<int> parents(bodies.size());
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](int id)
    {
        while (parents[std::size_t(id)] != id)
            id = parents[std::size_t(id)];
        return id;
    };
    const auto floating = [&](const ResolvedMate& mate)
    {
        std::vector<int> ids;
        const auto add = [&](int id)
        {
            if (id < -1 || id >= int(bodies.size()))
                throw std::runtime_error("Mate reference component is unavailable.");
            if (id >= 0 && !bodies[std::size_t(id)].fixed &&
                std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        };
        if (mate.definition)
            for (const auto& ref : mate.definition->references)
                add(ref.body);
        else
        {
            add(mate.first);
            add(mate.second);
        }
        return ids;
    };
    for (const auto& mate : mates)
    {
        const auto ids = floating(mate);
        for (std::size_t i = 1; i < ids.size(); ++i)
            parents[std::size_t(root(ids[i]))] = root(ids[0]);
    }
    std::map<int, std::vector<std::size_t>> blocks;
    std::vector<bool> participating(bodies.size(), false);
    for (std::size_t i = 0; i < mates.size(); ++i)
    {
        const auto ids = floating(mates[i]);
        for (int id : ids)
            participating[std::size_t(id)] = true;
        blocks[ids.empty() ? -1 : root(ids[0])].push_back(i);
    }
    if (blocks.size() <= 1)
        return solveMateBlock(bodies, mates);
    MateSolution result;
    result.motion.resize(mates.size());
    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
        result.placements.push_back(bodies[i].placement);
        result.fullyConstrained.push_back(bodies[i].fixed);
        if (!bodies[i].fixed && !participating[i])
            result.degreesOfFreedom += 6;
    }
    for (const auto& [key, indices] : blocks)
    {
        auto localBodies = bodies;
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (!participating[i] || root(int(i)) != key)
                localBodies[i].fixed = true;
        std::vector<ResolvedMate> localMates;
        for (auto i : indices)
            localMates.push_back(mates[i]);
        const auto solution = solveMateBlock(localBodies, localMates);
        result.degreesOfFreedom += solution.degreesOfFreedom;
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (participating[i] && root(int(i)) == key)
            {
                result.placements[i] = solution.placements[i];
                result.fullyConstrained[i] = solution.fullyConstrained[i];
            }
        for (std::size_t i = 0; i < indices.size(); ++i)
            result.motion[indices[i]] = solution.motion[i];
    }
    return result;
}

MateSolution dragMates(const std::vector<RigidBody>& bodies, const std::vector<ResolvedMate>& input,
                       const ComponentDragTarget& target)
{
    if (target.body < 0 || target.body >= int(bodies.size()) ||
        bodies[std::size_t(target.body)].fixed)
        throw std::runtime_error("Choose a floating component to drag.");
    auto result = solveGeneralMates(bodies, input);
    auto mates = input;
    std::vector<bool> connected(bodies.size(), false);
    connected[std::size_t(target.body)] = true;
    for (auto& m : mates)
        if (!m.definition)
        {
            m.definition = ResolvedMateDefinition{};
            m.definition->references = {{"first", m.first, mateGeometry(m.a)},
                                        {"second", m.second, mateGeometry(m.b)}};
        }
    const bool constrained = std::any_of(
        mates.begin(), mates.end(),
        [&](const auto& m)
        {
            return std::any_of(m.definition->references.begin(), m.definition->references.end(),
                               [&](const auto& r) { return r.body == target.body; });
        });
    if (!constrained)
    {
        auto delta =
            gp_Vec(target.localPoint.Transformed(result.placements[std::size_t(target.body)]),
                   target.target);
        const gp_Vec n(target.viewNormal);
        delta -= n * delta.Dot(n);
        auto& placement = result.placements[std::size_t(target.body)];
        placement.SetTranslationPart(placement.TranslationPart() + delta.XYZ());
        return result;
    }
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& m : mates)
        {
            bool uses = false;
            for (const auto& r : m.definition->references)
                uses |= r.body >= 0 && connected[std::size_t(r.body)];
            if (uses)
                for (const auto& r : m.definition->references)
                    if (r.body >= 0 && !bodies[std::size_t(r.body)].fixed &&
                        !connected[std::size_t(r.body)])
                    {
                        connected[std::size_t(r.body)] = true;
                        changed = true;
                    }
        }
    }
    double scale = 1;
    for (const auto& b : bodies)
        scale = std::max(scale, b.size);
    const gp_Vec normal(target.viewNormal);
    const auto error = [&](const std::vector<gp_Trsf>& poses)
    {
        auto v =
            gp_Vec(target.localPoint.Transformed(poses[std::size_t(target.body)]), target.target);
        return v - normal * v.Dot(normal);
    };
    for (int iteration = 0; iteration < 6; ++iteration)
    {
        const auto wanted = error(result.placements);
        if (wanted.Magnitude() <= target.tolerance)
            break;
        State state;
        state.poses = result.placements;
        state.coordinates.resize(mates.size());
        std::vector<Variable> variables;
        std::vector<double> weights;
        for (std::size_t b = 0; b < bodies.size(); ++b)
            if (connected[b] && !bodies[b].fixed)
                for (int axis = 0; axis < 6; ++axis)
                {
                    variables.push_back({int(b), axis, -1});
                    weights.push_back(axis < 3 ? 1 : 0.03);
                }
        for (std::size_t m = 0; m < mates.size(); ++m)
        {
            mates[m].definition->motion = result.motion[m];
            prepareMate(mates[m], state.poses);
            state.coordinates[m] = mates[m].definition->motion.coordinates;
            bool uses = false;
            for (const auto& r : mates[m].definition->references)
                uses |= r.body >= 0 && connected[std::size_t(r.body)];
            if (uses)
                for (std::size_t q = 0; q < state.coordinates[m].size(); ++q)
                {
                    variables.push_back({-1, int(q), int(m)});
                    weights.push_back(1);
                }
        }
        auto hard = jacobian(state, mates, variables, scale, true);
        for (int r = 1; r <= hard.RowNumber(); ++r)
            for (int c = 1; c <= hard.ColNumber(); ++c)
                hard(r, c) *= weights[std::size_t(c - 1)];
        const auto basis = rowBasis(hard, 1e-7);
        const int columns = int(variables.size());
        math_Matrix point(1, 3, 1, columns, 0.0);
        for (int c = 0; c < columns; ++c)
        {
            if (variables[std::size_t(c)].body != target.body)
                continue;
            auto plus = state, minus = state;
            step(plus, variables[std::size_t(c)], 1e-6, scale);
            step(minus, variables[std::size_t(c)], -1e-6, scale);
            const auto derivative =
                (error(minus.poses) - error(plus.poses)) * (weights[std::size_t(c)] / 2e-6);
            for (int r = 1; r <= 3; ++r)
                point(r, c + 1) = derivative.Coord(r);
        }
        for (int r = 1; r <= 3; ++r)
            for (const auto& direction : basis)
            {
                double dot = 0;
                for (int c = 0; c < columns; ++c)
                    dot += point(r, c + 1) * direction[std::size_t(c)];
                for (int c = 0; c < columns; ++c)
                    point(r, c + 1) -= dot * direction[std::size_t(c)];
            }
        math_Matrix gram(1, 3, 1, 3, 0.0);
        for (int r = 1; r <= 3; ++r)
            for (int c = 1; c <= 3; ++c)
            {
                for (int k = 1; k <= columns; ++k)
                    gram(r, c) += point(r, k) * point(c, k);
                if (r == c)
                    gram(r, c) += 1e-8;
            }
        math_Vector rhs(1, 3), answer(1, 3, 0.0);
        for (int r = 1; r <= 3; ++r)
            rhs(r) = wanted.Coord(r);
        math_SVD svd(gram);
        if (!svd.IsDone())
            break;
        svd.Solve(rhs, answer, 1e-12);
        std::vector<double> delta(std::size_t(columns), 0);
        double limit = 1;
        for (int c = 0; c < columns; ++c)
        {
            for (int r = 1; r <= 3; ++r)
                delta[std::size_t(c)] += point(r, c + 1) * answer(r);
            delta[std::size_t(c)] *= weights[std::size_t(c)];
            limit = std::max(limit, std::abs(delta[std::size_t(c)]) /
                                        (variables[std::size_t(c)].body >= 0 &&
                                                 variables[std::size_t(c)].coordinate >= 3
                                             ? 0.4
                                             : 0.5));
        }
        auto trial = state;
        for (int c = 0; c < columns; ++c)
            step(trial, variables[std::size_t(c)], delta[std::size_t(c)] / limit, scale);
        auto trialBodies = bodies;
        for (std::size_t b = 0; b < bodies.size(); ++b)
            trialBodies[b].placement = trial.poses[b];
        auto trialMates = mates;
        for (std::size_t m = 0; m < mates.size(); ++m)
            trialMates[m].definition->motion.coordinates = trial.coordinates[m];
        try
        {
            auto next = solveGeneralMates(trialBodies, trialMates);
            if (error(next.placements).SquareMagnitude() >= wanted.SquareMagnitude() - 1e-12)
                break;
            result = std::move(next);
        }
        catch (const std::exception&)
        {
            break;
        } // A blocked drag retains its last valid placement.
    }
    return result;
}
} // namespace thecad::modeling::assembly
