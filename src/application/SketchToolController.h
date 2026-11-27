#pragma once
#include "modeling/Sketch.h"
#include <optional>
namespace thecad::application
{
// Tool activation is application state, separate from QWidget mouse dispatch.
class SketchToolController final
{
public:
    enum class Mode
    {
        Select,
        Draw,
        Dimension,
        Relation
    };
    void select()
    {
        m_mode = Mode::Select;
        m_drawing.reset();
        m_construction = false;
    }
    void draw(modeling::EntityKind kind, bool construction)
    {
        select();
        m_mode = Mode::Draw;
        m_drawing = kind;
        m_construction = construction;
    }
    void dimension(bool enabled)
    {
        select();
        m_mode = enabled ? Mode::Dimension : Mode::Select;
    }
    void relations(bool enabled)
    {
        select();
        m_mode = enabled ? Mode::Relation : Mode::Select;
    }
    void clearDrawing()
    {
        m_drawing.reset();
        if (m_mode == Mode::Draw)
            m_mode = Mode::Select;
    }
    void resetConstruction() { m_construction = false; }
    Mode mode() const { return m_mode; }
    const std::optional<modeling::EntityKind>& drawing() const { return m_drawing; }
    bool construction() const { return m_construction; }
    bool dimensionPicking() const { return m_mode == Mode::Dimension; }
    bool constraintPicking() const { return m_mode == Mode::Dimension || m_mode == Mode::Relation; }

private:
    Mode m_mode = Mode::Select;
    std::optional<modeling::EntityKind> m_drawing;
    bool m_construction = false;
};
} // namespace thecad::application
