# SolidWorks assembly mates: behavior and TheCAD gap analysis

Research date: 2026-11-15. Target: desktop SolidWorks 2026 English workflows.
Sources are official SolidWorks Help. No local SolidWorks GUI interaction,
proprietary source inspection or copying of icons/resources was performed.
Some detailed pages were retrievable only in earlier official editions; those
links retain their actual edition. Such details need a 2026 UI/document check
before an exact-parity claim. This report is research, not an implementation.

Implementation update: the tables below preserve the **pre-implementation baseline at 10cb319**.
See [current implementation and limitations](assembly-mates-implementation.md).

## What "all mates" includes

The ordinary Mate PropertyManager has eight Standard types, seven Advanced
types when Limit Distance and Limit Angle are counted separately, and seven
Mechanical types: 22 types. Magnetic mating and the generated Belt, In Place
and Lock To Sketch relationships are additional workflows, not extra buttons
to insert into those three tabs. Locked variants are options, not additional
independent types. [Standard tab](https://help.solidworks.com/2026/English/SolidWorks/sldworks/HIDD_NEW_ADD_MATE.htm),
[Advanced tab](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_mate_pm_advanced.htm?id=7.7.3.1),
[Mechanical tab](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_mate_pm_mechanical.htm?id=7.7.3.2),
[tree relationship/variant list](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_mate_icons_featuremanager_design_tree.htm?id=7.7.4).

## Standard types

"Partial" below describes the latest local TheCAD code at `10cb319`, not the
full SolidWorks geometry eligibility matrix.

| Type | Expected relationship | TheCAD now |
| --- | --- | --- |
| Coincident | Shared location, line or plane according to the entity pair | Partial: plane/plane |
| Parallel | Directional parallelism without closing a positional gap | Missing |
| Perpendicular | Orthogonal eligible directions/entities | Missing |
| Tangent | Contact between eligible curved and other geometry | Missing |
| Concentric | Shared center/centerline as appropriate | Partial: axis/axis |
| Lock | Preserve relative position and orientation of two components | Missing; global Fix is different |
| Distance | Specified separation appropriate to the entities | Partial: plane/plane |
| Angle | Specified angular relationship | Partial: two planes or two axes |

Concentric can lock relative rotation. Coincident between origins/coordinate
systems can align their axes. Misaligned concentric mates have linked deviation
options, rather than silently altering the selected geometry. These options
also need coverage; simply adding the eight names is insufficient.
[Standard tab](https://help.solidworks.com/2026/English/SolidWorks/sldworks/HIDD_NEW_ADD_MATE.htm).

Eligibility is entity-specific. For example, points can be coincident with
points, planes or lines; circular edges can define concentric relationships;
spherical faces need center/radius information. The full official table is the
acceptance source, including cone, cylinder, curve, extrusion and non-analytic
surface distinctions. Do not reduce every curved face to an axis.
[Standard Mates by Entity](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_Standard_Mates_by_Entity.htm?id=7.7.5).

Tangency has its own eligibility restrictions; a non-analytic surface and an
undrafted extruded face are not interchangeable abstractions.
[Tangent Mate](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_Tangent_Mate_SWassy.htm).

## Advanced types — missing at research baseline

| Type | Selection/behavior requirements |
| --- | --- |
| Profile Center | Supported profiles; center alignment, orientation/offset and appropriate rotation locking |
| Symmetric | Two similar entities and a separate symmetry plane |
| Width | Two reference groups defining a groove/tab, with several positioning modes |
| Path | Component point plus an ordered contiguous path; optional orientation control |
| Linear/Linear Coupler | Two translation directions, ratio/reversal and optional reference components |
| Limit Distance | Starting separation and lower/upper bounds, with movement inside the interval |
| Limit Angle | Starting angle and lower/upper bounds, with movement inside the interval |

The family and dynamic selection fields are documented in the
[Advanced tab](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_mate_pm_advanced.htm?id=7.7.3.1).

Profile Center recognizes specified profiles, including full circles and regular
polygon profiles. Rectangles have documented allowances for some corner
treatments, but arbitrary perimeter changes are not supported. A bounding-box
center is not an equivalent implementation.
[Profile Center](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_profile_center_mate.htm).

Symmetric relates the chosen entities, not the whole bodies. Similar supported
entities include points, lines, planes and equal-radius spheres/cylinders.
[Symmetry Mate, official 2025 edition](https://help.solidworks.com/2025/English/SolidWorks/sldworks/t_Symmetry_Mate_SWassy.htm?id=583c935aa6d14e5c98a504dfc37b8b6a).

Width has Centered, Free, Dimension and Percent modes. Its references can include
parallel/nonparallel faces and, in the retrieved 2025 documentation, axes or
cylindrical faces in either selection group. Preserve permitted translation and
rotation rather than locking the complete tab pose.
[Width Mates, official 2025 edition](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_width_mates_swassy.htm).

Path supports free travel, distance and percentage positions, measuring from
either end. Pitch/yaw may remain free or follow a component axis along the path.
Roll can remain free or use an up vector. The orientation controls cannot use
the same component axis for both roles.
[Path Mate, official 2023 edition](https://help.solidworks.com/2023/english/SolidWorks/sldworks/t_Path_Mate_SWassy.htm).

Linear coupling relates relative translations; either motion can be measured
against ground or another component. It is not a fixed distance between bodies.
[Linear/Linear Coupler, official 2025 edition](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_mates_linear-linear_coupler.htm?id=7.6.6.1.1).

Limits are intervals, not equalities at the initial value. Dragging inside a
valid interval must remain possible and outward movement at a bound must stop.
[Limit Mates, official 2020 edition](https://help.solidworks.com/2020/english/solidworks/sldworks/t_Limit_Mates_SWassy.htm).

## Mechanical types — missing at research baseline

| Type | Expected behavior and controls |
| --- | --- |
| Cam | Follower stays on the connected cam-face chain; eligible follower face/vertex |
| Slot | Bolt/axis/cylinder or slot travels within a straight/arced slot; Free, Center, Distance, Percent and applicable rotation locking |
| Hinge | Concentric and coincident reference groups leave one rotation; optional angular limits |
| Gear | Signed ratio between shaft rotations; editable/default ratio and Reverse |
| Rack and Pinion | Linear travel coupled to rotation using pitch diameter or travel per revolution |
| Screw | Concentric relationship plus translation/rotation pitch coupling and Reverse |
| Universal Joint | Input/output shaft rotation coupling for a universal joint |

Family membership and its selection-specific PropertyManager are documented in
the [Mechanical tab](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_mate_pm_mechanical.htm?id=7.7.3.2).

Cam selection follows a closed, tangent extruded profile; selecting a cam face
collects its connected profile faces. The follower can be cylindrical, planar
or a vertex. A tangent constraint on only the first selected face will fail as
the cam passes a face transition.
[Cam-Follower, official 2024 edition](https://help.solidworks.com/2024/english/solidworks/sldworks/t_Cam_Follower_Mates_SWassy.htm).

Slot-to-slot permits Free or Center, whereas axis/cylinder-to-slot additionally
has distance/percentage placement. Flip changes the measured endpoint.
[Slot Mates, official 2024 edition](https://help.solidworks.com/2024/english/solidworks/sldworks/t_assemblies_mates_creating_slot_mates.htm).

Hinge needs separate concentric and coincident selection sets; angle limits add
another face pair. It is one mate definition with the intended remaining DOF.
[Hinge Mates](https://help.solidworks.com/2026/english/SolidWorks/Sldworks/t_hinge_mates.htm).

Gear couples rotations without requiring gear-tooth geometry or preventing
collisions. Its ratio may be derived from the selected geometry or overridden.
[Gear Mates](https://help.solidworks.com/2026/English/SolidWorks/Sldworks/t_Gear_Mates_SWassy.htm).

Rack/Pinion supports pitch diameter or linear travel per revolution; neither
component needs actual teeth. [Rack and Pinion, official 2025 edition](https://help.solidworks.com/2025/english/SolidWorks/Sldworks/t_Rack_and_Pinion_Mates_SWassy.htm).
Screw adds concentricity as well as pitch, with distance/revolution or the inverse
unit option. [Screw Mate](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_Screw_Mate_SWassy.htm).
Universal Joint drives output rotation from input rotation; the retrieved short
topic does not specify enough detail to claim its complete selector/phase UI was
verified. [Universal Joint, official 2025 edition](https://help.solidworks.com/2025/English/SolidWorks/sldworks/t_Universal_Joint_Mate_SWassy.htm).

## Related mating workflows — missing at research baseline

| Workflow | Dependency/behavior |
| --- | --- |
| Magnetic | Published connection points/directions and ground planes; proximity snapping, connector cycling, lock/unlock |
| Belt | Generated by Belt/Chain; pulley rotation coupling, belt path/length and engagement |
| Lock To Sketch | Generated with a belt part linked to its assembly belt sketch |
| In Place | Automatically associated with in-context component insertion/creation |

Magnetic mating depends on published assets; replacing it with nearest-face
snapping would be a different feature.
[Magnetic Mates](https://help.solidworks.com/2026/English/SolidWorks/sldworks/c_magnetic_mates_intro.htm).
Belt/Chain creates pulley relationships and a belt sketch. Optionally generated
belt parts carry assembly references and a LockToSketch relationship; engagement
controls coupled movement. [Belt/Chain, official 2025 edition](https://help.solidworks.com/2025/English/SolidWorks/sldworks/t_Creating_a_Belt_Chain_Assembly_Feature.htm).
In Place is an automatic in-context relationship, not another manual Standard
mate. [Official IMateInPlace description](https://help.solidworks.com/2025/English/api/sldworksapi/SolidWorks.Interop.sldworks~SolidWorks.Interop.sldworks.IMateInPlace.html?id=312dc84125ef4e6baa2a74e7f74a72a9).

## Interaction parity requirements

The three PropertyManager tabs expose only compatible mate choices for the
selected entities. Selection controls change with the mate type. The workflow
includes a live preview, editable alignment/values, confirmation, editing from
the mate tree and an option to keep the manager open for more mates.
[Adding Mates](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_Adding_Mates_SWassy.htm).

Ctrl-selection can show Quick Mates without opening the full manager. It offers
compatible choices and a default, with extra controls for parameterized mates.
Reference geometry can be selected in the tree or viewport.
[Quick Mates](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_mates_quick_mates_toolbar.htm?id=7.7.2.2).

Suggestions can fall back when the primary default overdefines the assembly;
parallel planes, for example, can use Parallel when Coincident conflicts.
[Default Mate Types](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_assemblies_default_mate_types.htm?id=7.7.2.12).

Mates solve as a system; add order is not a positioning recipe. Suppression and
tree editing are part of the workflow. [Mates overview](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_Mates_Overview_Assemblies.htm?id=7.7).
Mate folders expose solved/error/overdefined/suppressed states and component
associations. [Mate Folders](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_Mate_Folders_SWassy.htm?id=7.7.2.0).

SolidWorks can retain an unsolved mate or force a new mate while another becomes
broken. TheCAD currently rejects such transactions. Matching this is an explicit
document/error-state decision: do not silently disable or delete existing mates.
[Adding Mates](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_Adding_Mates_SWassy.htm).

Motion/Simulation mate properties, including bushing stiffness/damping and load
parameters, belong to an additional analysis workflow. Kinematic mate coverage
does not by itself provide a force, collision or deformable-body simulator.
[Mate Analysis tab](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_DVE_MATEANALYSIS.htm?id=7.7.3.6).

## Architecture gaps at the research baseline

Inspection of the latest local implementation found:

- `MateKind` has four enum values. A resolved mate has exactly two body indices,
  two `DatumGeometry` records, one scalar and one alignment sign.
- `DatumGeometry` distinguishes only point, axis and plane, with one cylinder
  radius. It cannot describe sphere/cone curvature, bounded curves, contact
  branches, slot geometry, a profile, a coordinate frame or a chain of faces.
- `AssemblyMate` and schema 1 persist two references. Width/Hinge/Symmetric,
  optional coupler reference bodies and ordered path/cam selections do not fit.
- Solver residuals are equalities over rigid poses. There is no inequality
  active set, curve parameter unknown, continuous multi-turn phase or persisted
  unsolved/suppressed mate status.
- The editor has two selectors and a flat type combo. The viewport picks faces;
  points/edges and selection sets must be added. Reference and presentation
  caching from the accepted performance work must be preserved.

The following document proposes the required extension, rather than pretending
the existing four-type engine already supports the full catalog:
[complete mate architecture proposal](../decisions/0056-complete-assembly-mates-proposal.md).
