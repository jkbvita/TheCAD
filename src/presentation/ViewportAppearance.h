#pragma once
#include <V3d_View.hxx>
namespace thecad::presentation
{
// Shared workspace styling. Applying a theme never changes camera or geometry.
void applyViewportAppearance(const Handle(V3d_View) & view, bool dark);
} // namespace thecad::presentation
