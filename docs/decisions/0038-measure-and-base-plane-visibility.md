# 0038. Measure and base-plane visibility

The owner approved a read-only Measure tool for finished parts. Use the
existing viewport selection and Open CASCADE geometry. The first pick can
report vertex coordinates, edge length, or radius and diameter for circular
edges and cylindrical faces. A second pick reports minimum distance between
the trimmed shapes, plus center spacing for two circular edges or the smaller
angle for two straight edges or planar faces. Use millimeters and degrees with
three decimal places in the UI.

Evaluate > Measure opens a readout in the existing FeatureManager and places
a callout at the measured geometry. A third distinct pick starts a new
measurement. Clicking a selected item removes it and clicking empty space
clears both picks. Escape or the toolbar button closes the tool. Measure
does not change the part, camera, history, or file format. This first version
works with solid faces, edges and vertices in part mode.

Reference Plane, Axis and Point commands preserve the current visibility of
the base planes when opened, confirmed or canceled. Hidden base planes are
excluded from viewport picking. The Reference Geometry PropertyManager shows
the FeatureManager tree above its controls. The origin-plane rows remain
available as references and no longer force those planes visible.

[SolidWorks Measure Tool](https://help.solidworks.com/2024/English/SolidWorks/sldworks/HIDD_MEASURE.htm)
