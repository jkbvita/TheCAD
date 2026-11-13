# 0050. Reveal feature depth when starting a solid command

The owner requested that starting Extrude, Cut and other solid features change
the view so the operation is visible. This is an explicit workflow transition,
not permission to move the camera during ordinary selection or preview updates.

On entering a solid feature editor, turn flat views into an oblique view once.
For a known sketch profile, use its actual frame, including face-attached or
custom planes: looking nearly normal to it hides depth, while looking nearly
edge-on hides its area. Use equal contributions from the sketch's X, Y and
normal directions with the sketch Y direction as up. Without a known profile,
turn flat world-axis views into an isometric view. Keep a useful existing
oblique view unchanged.

Apply this policy to Extrude/Cut, Revolve/Revolved Cut, Sweep/Swept Cut,
Fillet/Chamfer, Mirror and the solid patterns, including editing existing
features. Preserve target, scale and projection; only orientation and clipping
are updated. Do not run Fit All. No repeated camera transitions occur when
typing parameters, picking contours/references, toggling preview or confirming.
Canceling back into an active sketch retains its existing normal-to-sketch
workflow. Reference-geometry editors and ordinary selection are unchanged.

The policy lives in the viewport navigation controller and is called at feature
entry, outside rendering and selection. No document, solver or kernel changes
or additional dependencies are needed.
