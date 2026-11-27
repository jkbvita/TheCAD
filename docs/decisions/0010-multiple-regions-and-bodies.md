# 0010 — Multiple selected regions and multiple bodies

Status: owner approved, 2026-11-04. Evidence: request to support many contours in
one sketch and explicit reply “Allow multiple bodies within a part”. This
supersedes the single connected solid restriction in the initial prototype.

Extrude and Extruded Cut explicitly select one or more actual planar regions.
Clicking regions toggles selection; the PropertyManager list supports multiple
selection. No region is selected implicitly. Each feature stores every selected
region's exact stable boundary-ID set; missing or changed regions fail clearly.

Independent closed loops and strict nesting are supported. A loop's selectable
region excludes its immediate child loops as holes. Child interiors are separately
selectable. Selecting both an annular region and its inner region fills the hole.
Actual OCCT faces/classifiers determine containment, not screen rectangles.
Crossing or touching boundaries and general shared-edge arrangements still fail;
there is no automatic trimming or invented contour choice.

The part's precise result may contain several valid positive-volume solids.
Additive regions are fused together and with the existing result; disconnected
results are retained. Cuts act on the current part result, affecting intersected
bodies; removing one body or splitting one is allowed while a valid nonempty
solid result remains. There are no persistent body identities, body scope controls
or body-specific materials in this milestone. Those need later design decisions.
