#pragma once

#include "modeling/SketchModifiers.h"

namespace thecad::modeling
{
inline bool isSketchLineEdit(SketchModifier kind)
{
    return kind == SketchModifier::Trim || kind == SketchModifier::Extend || kind == SketchModifier::Split;
}

// Produces exact sketch geometry without running the solver during pointer hover.
// The document transaction solves and validates downstream features on acceptance.
Sketch editSketchLine(const Sketch& sketch, SketchModifier kind, const std::string& id, Point2 pick);
}
