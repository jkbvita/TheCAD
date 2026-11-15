#include "modeling/assembly/MateSolver.h"
#include <gp_Quaternion.hxx>
#include <math_Matrix.hxx>
#include <math_SVD.hxx>
#include <math_Vector.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thecad::modeling::assembly
{
namespace
{
constexpr double pi = 3.14159265358979323846;
using Poses = std::vector<gp_Trsf>;
DatumGeometry world(DatumGeometry geometry, int body, const Poses& poses)
{
    if (body >= 0)
        geometry.frame.Transform(poses.at(std::size_t(body)));
    return geometry;
}
void step(gp_Trsf& pose, int coordinate, double value, double scale)
{
    auto position = pose.TranslationPart();
    if (coordinate < 3)
    {
        position.SetCoord(coordinate + 1, position.Coord(coordinate + 1) + value * scale);
        pose.SetTranslationPart(position);
    }
    else
    {
        const gp_Vec axis = coordinate == 3   ? gp_Vec(1, 0, 0)
                            : coordinate == 4 ? gp_Vec(0, 1, 0)
                                              : gp_Vec(0, 0, 1);
        const auto rotation = gp_Quaternion(axis, value) * pose.GetRotation();
        pose.SetRotation(rotation);
        pose.SetTranslationPart(position);
    }
}
struct Equations
{
    std::vector<double> values, tolerances;
};
Equations equations(const Poses& poses, const std::vector<ResolvedMate>& mates, double scale)
{
    Equations e;
    const auto scalar = [&](double value, bool length = false)
    {
        e.values.push_back(value);
        e.tolerances.push_back(length ? 1e-5 / scale : 1e-8);
    };
    const auto vector = [&](const gp_Vec& value, bool length = false)
    {
        scalar(value.X(), length);
        scalar(value.Y(), length);
        scalar(value.Z(), length);
    };
    for (const auto& mate : mates)
    {
        const auto a = world(mate.a, mate.first, poses).frame,
                   b = world(mate.b, mate.second, poses).frame;
        const gp_Vec normal(a.Direction()), other(b.Direction()), gap(a.Location(), b.Location());
        if (mate.kind == MateKind::Angle)
        {
            const double angle = mate.value * pi / 180;
            if (angle < 1e-10 || pi - angle < 1e-10)
                vector(other - normal * (angle < pi / 2 ? 1 : -1));
            else
                scalar(std::atan2(normal.Crossed(other).Magnitude(), normal.Dot(other)) - angle);
        }
        else
        {
            vector(other - normal * mate.alignment);
            if (mate.kind == MateKind::Concentric)
                vector(gap.Crossed(normal) / scale, true);
            else
                scalar((gap.Dot(normal) - (mate.kind == MateKind::Distance ? mate.value : 0)) /
                           scale,
                       true);
        }
    }
    return e;
}
bool satisfied(const Equations& e)
{
    for (std::size_t i = 0; i < e.values.size(); ++i)
        if (std::abs(e.values[i]) > e.tolerances[i])
            return false;
    return true;
}
double cost(const Equations& e)
{
    double value = 0;
    for (auto v : e.values)
        value += v * v;
    return value;
}
math_Matrix jacobian(const Poses& poses, const std::vector<ResolvedMate>& mates,
                     const std::vector<int>& free, double scale)
{
    const int rows = int(equations(poses, mates, scale).values.size()),
              columns = int(free.size()) * 6;
    math_Matrix matrix(1, rows, 1, columns, 0.0);
    constexpr double h = 1e-6;
    for (int column = 0; column < columns; ++column)
    {
        auto plus = poses, minus = poses;
        const auto body = std::size_t(free[std::size_t(column / 6)]);
        step(plus[body], column % 6, h, scale);
        step(minus[body], column % 6, -h, scale);
        const auto high = equations(plus, mates, scale), low = equations(minus, mates, scale);
        for (int row = 0; row < rows; ++row)
            matrix(row + 1, column + 1) =
                (high.values[std::size_t(row)] - low.values[std::size_t(row)]) / (2 * h);
    }
    return matrix;
}
void seedSingularOrientations(Poses& poses, const std::vector<RigidBody>& bodies,
                              const std::vector<ResolvedMate>& mates)
{
    for (const auto& mate : mates)
    {
        int target = mate.second, reference = mate.first;
        auto a = mate.a, b = mate.b;
        if (target < 0 || bodies[std::size_t(target)].fixed)
        {
            std::swap(target, reference);
            std::swap(a, b);
        }
        if (target < 0 || bodies[std::size_t(target)].fixed)
            continue;
        const auto first = world(a, reference, poses).frame, second = world(b, target, poses).frame;
        const gp_Vec normal(first.Direction()), current(second.Direction());
        gp_Vec desired = normal * mate.alignment;
        if (mate.kind == MateKind::Angle)
        {
            if (std::abs(normal.Dot(current)) < 0.999999)
                continue;
            const double angle = mate.value * pi / 180;
            desired = normal * std::cos(angle) +
                      (gp_Vec(first.XDirection()) * 0.8 + gp_Vec(first.YDirection()) * 0.6) *
                          std::sin(angle);
        }
        else if (desired.Dot(current) > -0.95)
            continue;
        auto& pose = poses[std::size_t(target)];
        const auto translation = pose.TranslationPart();
        pose.SetRotation(gp_Quaternion(current, desired) * pose.GetRotation());
        pose.SetTranslationPart(translation);
    }
}
} // namespace
const char* mateName(MateKind kind)
{
    switch (kind)
    {
    case MateKind::Coincident:
        return "Coincident";
    case MateKind::Concentric:
        return "Concentric";
    case MateKind::Distance:
        return "Distance";
    case MateKind::Angle:
        return "Angle";
    }
    return "Invalid";
}
bool validMateTypes(MateKind kind, DatumKind first, DatumKind second)
{
    if (kind == MateKind::Concentric)
        return first == DatumKind::Axis && second == DatumKind::Axis;
    if (kind == MateKind::Angle)
        return first == second && (first == DatumKind::Plane || first == DatumKind::Axis);
    return (kind == MateKind::Coincident || kind == MateKind::Distance) &&
           first == DatumKind::Plane && second == DatumKind::Plane;
}
void validateRigidPlacement(const gp_Trsf& placement)
{
    if (std::abs(placement.ScaleFactor() - 1) > 1e-10 || placement.IsNegative())
        throw std::runtime_error("Component placement must be a rigid rotation and translation.");
    for (int row = 1; row <= 3; ++row)
        for (int column = 1; column <= 4; ++column)
            if (!std::isfinite(placement.Value(row, column)) ||
                std::abs(placement.Value(row, column)) > 1e9)
                throw std::runtime_error("Component placement exceeds the finite 1e9 mm limit.");
}
MateSolution solveMates(const std::vector<RigidBody>& bodies,
                        const std::vector<ResolvedMate>& mates)
{
    if (bodies.size() > 64 || mates.size() > 256)
        throw std::runtime_error(
            "This assembly milestone supports up to 64 components and 256 mates.");
    Poses poses;
    std::vector<int> free;
    double scale = 1;
    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
        if (!std::isfinite(bodies[i].size) || bodies[i].size <= 0)
            throw std::runtime_error("Component size must be finite and positive.");
        validateRigidPlacement(bodies[i].placement);
        poses.push_back(bodies[i].placement);
        if (!bodies[i].fixed)
            free.push_back(int(i));
        scale = std::max({scale, bodies[i].size, bodies[i].placement.TranslationPart().Modulus()});
    }
    for (const auto& mate : mates)
    {
        if (mate.first < -1 || mate.second < -1 || mate.first >= int(bodies.size()) ||
            mate.second >= int(bodies.size()) || mate.first == mate.second ||
            !validMateTypes(mate.kind, mate.a.kind, mate.b.kind) || !std::isfinite(mate.value) ||
            std::abs(mate.value) > 1e9 ||
            (mate.kind == MateKind::Angle && (mate.value < 0 || mate.value > 180)) ||
            std::abs(mate.alignment) != 1)
            throw std::runtime_error("Invalid mate geometry, references or value.");
    }
    const int columns = int(free.size()) * 6;
    if (!mates.empty())
    {
        seedSingularOrientations(poses, bodies, mates);
        auto current = equations(poses, mates, scale);
        double damping = 1e-6;
        for (int iteration = 0; !satisfied(current) && iteration < 100 && columns > 0; ++iteration)
        {
            const auto j = jacobian(poses, mates, free, scale);
            const int rows = int(current.values.size());
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
            math_SVD solver(augmented);
            if (!solver.IsDone())
                throw std::runtime_error("The assembly mate system could not be decomposed.");
            solver.Solve(right, delta, 1e-10);
            auto trial = poses;
            double limit = 1;
            for (int c = 1; c <= columns; ++c)
                limit = std::max(limit, std::abs(delta(c)) /
                                            (c % 6 == 4 || c % 6 == 5 || c % 6 == 0 ? 0.5 : 10.0));
            for (int c = 0; c < columns; ++c)
                step(trial[std::size_t(free[std::size_t(c / 6)])], c % 6, delta(c + 1) / limit,
                     scale);
            auto candidate = equations(trial, mates, scale);
            if (cost(candidate) < cost(current) || satisfied(candidate))
            {
                poses = std::move(trial);
                current = std::move(candidate);
                damping = std::max(1e-14, damping * 0.2);
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
            std::string message = "Conflicting or unsolved assembly mates:";
            for (const auto& mate : mates)
                message += " " + mate.id;
            throw std::runtime_error(message + ". The previous assembly is retained.");
        }
    }
    MateSolution result;
    result.placements = poses;
    result.degreesOfFreedom = columns;
    for (const auto& body : bodies)
        result.fullyConstrained.push_back(body.fixed);
    if (columns > 0 && !mates.empty())
    {
        const auto j = jacobian(poses, mates, free, scale);
        double magnitude = 0;
        for (int r = j.LowerRow(); r <= j.UpperRow(); ++r)
            for (int c = 1; c <= columns; ++c)
                magnitude += std::abs(j(r, c));
        if (magnitude > 1e-12)
        {
            math_SVD decomposition(j);
            if (!decomposition.IsDone())
                throw std::runtime_error("Cannot diagnose assembly freedom.");
            math_Matrix inverse(1, columns, 1, j.RowNumber(), 0.0);
            decomposition.PseudoInverse(inverse, 1e-7);
            double rank = 0;
            for (int c = 1; c <= columns; ++c)
            {
                double diagonal = 0;
                for (int r = 1; r <= j.RowNumber(); ++r)
                    diagonal += inverse(c, r) * j(r, c);
                rank += diagonal;
            }
            result.degreesOfFreedom = std::clamp(columns - int(std::lround(rank)), 0, columns);
            for (std::size_t i = 0; i < free.size(); ++i)
            {
                bool fixed = true;
                for (int axis = 0; axis < 6; ++axis)
                {
                    const int c = int(i) * 6 + axis + 1;
                    double diagonal = 0;
                    for (int r = 1; r <= j.RowNumber(); ++r)
                        diagonal += inverse(c, r) * j(r, c);
                    if (1 - diagonal > 1e-6)
                        fixed = false;
                }
                result.fullyConstrained[std::size_t(free[i])] = fixed;
            }
        }
    }
    for (const auto& pose : poses)
        validateRigidPlacement(pose);
    return result;
}
} // namespace thecad::modeling::assembly
