# 0059. Shared part and assembly viewport appearance

Requested by the owner on 2026-11-17. The assembly viewport should match the part
workspace. Both now use `presentation::applyViewportAppearance` for the existing
part gradient (including its sRGB conversion) and lower-left axis indicator.
Removed independent assembly colors and the startup overrides of axis-label color.
Lighting, antialiasing, opaque light-gray solid shading, edge rendering and selection
already use the shared GraphicsScene/SolidPresentation implementations.

This styling helper changes neither camera state nor document geometry. Appearance
updates follow the same application theme in both modes. No new dependencies,
modeling behaviors or persistence formats. Regression tests check actual OCCT view
colors, indicator scale/label color, opaque assembly solids and unchanged camera
state for Dark/Light, including the existing HiDPI runs.

Release build and all five focused CTest suites passed (7.63 seconds): part and
assembly viewport workflows, both HiDPI variants, and dependency boundaries.
No camera or modeling behavior changes.
