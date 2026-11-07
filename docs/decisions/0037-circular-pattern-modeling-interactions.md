# 0037. Circular Pattern and modeling interactions

The owner approved Circular Pattern and deferred Hole Wizard. Add the solid
feature to the Pattern / Mirror group using the existing pattern panel and
material-based feature replay. The seed counts as one instance. Equal spacing
uses the total span and omits the duplicate endpoint on a full turn. With equal
spacing cleared, the angle is the increment between instances. Support a single
direction with an optional reverse switch. The reference can be a named axis,
straight edge or sketch line, circular edge, or an axial face. Persist the
reference and parameters in schema 22. Bidirectional patterns and per-instance
skips are outside this first implementation.

On a blank startup part, a direct viewport click on an origin plane creates
the first sketch. The shortcut ends after the first document edit or
replacement. Tree selection and later plane selection retain their normal
behavior. Origin and datum plane selection uses a thicker blue border with the
same translucent surface. Solid face selection remains filled blue.

Profile features can start while Smart Dimension is picking or placing an
annotation. Starting the feature closes Smart Dimension and discards an
unplaced annotation. An open dimension value editor must still be completed
or canceled. Removed material is yellow, including when editing an existing
feature, and the preview compares with the solid immediately before that feature.

Reference: [SolidWorks Circular Pattern PropertyManager](https://help.solidworks.com/2022/english/SolidWorks/sldworks/HIDD_CPATTERN.htm).
