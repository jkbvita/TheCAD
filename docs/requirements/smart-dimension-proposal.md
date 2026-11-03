# Smart Dimension investigation and proposal

Date: 2026-11-03
Status: Proposal only; no solver selected or integrated

## Existing model and limitation

Sketches hold stable entity/point IDs and Coincident/Horizontal/Vertical records.
The current editSketchEntity equality groups propagate independent X/Y equalities
and pin the local origin. This works for supported dragging and joins. It cannot
solve coupled nonlinear line lengths or simultaneous dimensional constraints.
Adding special-case endpoint relocation for each new dimension would make
results order-dependent and undermine conflict detection.

## Proposed model behavior for approval

Keep dimensional constraints in the sketch model, separate from annotation
placement and Qt graphics. Each gets a stable ID, references, dimension kind,
value in mm and driving/reference status. Initial kinds: line length, horizontal
and vertical point distances, circle radius and diameter. Radius and diameter
refer to one radius parameter; conflicting independent driving values must fail.
The owner must approve signed/unsigned distance conventions and which points stay
anchored when an underconstrained sketch changes.

A line-length equation uses squared endpoint distance minus squared target
length. Horizontal/vertical distances constrain local coordinate differences;
circle diameter constrains twice the radius. Coincident/H/V and the fixed origin
must participate in the same solve. Underconstrained results retain a nearby
solution using the prior coordinates and an approved drag/anchor policy.

Dimension creation/edit and dragging should solve a candidate sketch, check
residuals and kernel validity, regenerate dependent features, and commit only on
success. On conflict, identify involved stable constraint IDs and retain the
previous geometry. Expose underconstrained, fully constrained, redundant,
inconsistent and nonconverged states distinctly. Store real editable driving
values; measuring graphics alone are not dimensions. Save/reopen must regenerate
with the same constraints. A versioned migration will be needed for dimensional
records; none is added by this milestone.

## Options evaluated

| Option | Advantages | Cost / risk |
| --- | --- | --- |
| Extend equality propagation | Small dependency surface for H/V and axis distances | General lengths require nonlinear solving, rank analysis, solution selection and diagnostics; this becomes a new solver project |
| SolveSpace library | Published C interface; geometric/dimensional constraints, degrees of freedom and failed-constraint reporting | GPL licensing must fit the owner's application license decision; integration and numerical behavior need a prototype |
| PlaneGCS component | Existing 2D constraint APIs, solving algorithms and conflict/redundancy diagnostics | Extraction/build adaptation and dependency audit required; upstream includes FreeCAD export definitions and Eigen; component reuse needs explicit owner approval |

My engineering recommendation is to evaluate an existing solver behind a small
Qt-independent adapter, starting with a disposable SolveSpace integration spike
if the owner approves its license implications. Compare it with an isolated
PlaneGCS spike before selecting either. This recommendation is an inference from
the APIs, not a performance benchmark. Reusing a solver component would not mean
turning TheCAD into a FreeCAD application fork. No library was downloaded or
integrated for this investigation.

For approval: solver evaluation scope, acceptable license/dependencies, signed
distance semantics, anchoring/drag behavior, and how redundant constraints are
presented. After approval, test a constrained rectangle changing width/height,
diagonal line length, circle radius/diameter, inconsistent dimensions, singular
geometry, drag/recompute, attached-face sketches and file round trips.

## Primary sources reviewed

- [SolveSpace library interface and license](https://solvespace.com/library.pl).
- [Current SolveSpace C interface](https://github.com/solvespace/solvespace/blob/master/include/slvs.h): distance/diameter constraints, solve results, degrees of freedom and failed constraints.
- [PlaneGCS interface](https://github.com/FreeCAD/FreeCAD/blob/main/src/Mod/Sketcher/App/planegcs/GCS.h): dimensional APIs, diagnostics and integration dependencies; the file identifies LGPL-2.1-or-later.

A full dependency/license audit remains part of a future approved integration.
