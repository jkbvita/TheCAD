# 0044. Mirror-junction edge treatments and preview responsiveness

Keep the native Open CASCADE fillet and chamfer as the first path. Where it
fails on a convex straight edge between two planar faces, construct the exact
circular fillet or equal-distance bevel in a normal section and sweep it
along the selected edge. Subtract the sweep from a private copy and accept
the result only if it remains one valid solid and each selected edge
produces a named treatment face. The fallback can follow a split edge on
the same supporting planes. Its termination is bounded by the selected edge
and it does not construct a three-way corner blend for intersecting edges.
Tangent seams and unsupported curved blends can still be rejected.

An unchanged document prefix reuses its existing solid and topology history.
Fillet and Chamfer debounce input and run one preview job per panel using Qt
Core's thread pool and prepare the viewport meshes there. Selection
feedback stays on the UI thread. A result is
accepted only if it matches the current inputs. Confirmation reuses that
candidate, and a pending Enter confirms only the input it was requested for.
The preview keeps the existing added and removed material colors. No new
runtime dependencies or document fields are required.
