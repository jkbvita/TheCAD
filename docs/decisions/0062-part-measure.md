# 0062. Measure in parts and active sketches

## Reference inspection

The local application identified itself as SOLIDWORKS Premium 2025 SP2.0. Inspection used a disposable copy of the project's fillet-test part. Opening Evaluate → Measure with an empty selection displayed a floating native tool window. It could collapse to a roughly 545 × 68 pixel strip on this display. Expanding it revealed a roughly 545 × 533 pixel window. The feature tree and viewport kept their positions.

A clicked planar face turned bright green. A thin leader connected the face to a white, black-bordered label showing area **883.86 mm²** and perimeter **200.87 mm**. Clicking a second face without Ctrl added a row. The parallel-face report showed **7.28 mm** distance, XYZ differences **0, 0, 7.28 mm**, and **1952.06 mm²** total area. The distance label used a dark caption cell, a light value cell and a small circular leader connection. The Z span was blue. Closing Measure restored ordinary blue selection. Reopening Measure took the preselected pair. These numbers describe the reference application and model.

| Control | Function and availability | Observed appearance or documented behavior |
|---|---|---|
| Evaluate → Measure | Opens inspection in a part | Command stayed pressed. Initially collapsed on this installation |
| Title, close, pin, expand | Move the modeless window, close it, keep it open on Esc, reveal results | Document name in native title. Pin and chevron at right of toolbar |
| Arc/Circle | Choose a measurement convention for circular geometry | Leftmost split button. Help documents center, minimum, maximum and independent first/second arc conditions |
| Units/Precision | Document or custom formatting | Separate compact dialog. Document selected on test model. Length was mm, angle degrees, both two decimals. Controls included scientific notation, fractions, dual units and accuracy level |
| XYZ | Show axis components in the viewport | Pressed on this installation. Documentation describes component graphics |
| Point-to-point | Pick arbitrary locations on geometry | Toolbar toggle. Help describes mutual exclusion with arc conditions |
| Relative coordinates and projection | Select reference frame or project onto screen/face/plane | Compact selection menus. Additional coordinate systems are conditional |
| Selection box | List selected geometry | Pale blue field immediately below toolbar, dark blue current row, small left-aligned text. One face per line, six selections documented |
| Result text | Quantities appropriate to current picks | Dense left-aligned system sans serif. Quick Copy and two font-size buttons at upper right. Geometry relationship, dimensions and source file/configuration below |
| History and sensor | Review session readings or create a persistent alert | Toolbar buttons. History lifetime and sensor workflow taken from Help |
| Callouts | Read values while navigating | Thin leaders, small attachment circles, compact opaque value boxes. Measured faces green, ordinary hover orange, ordinary selected faces blue |

The geometry combinations, projection choices, copying, history semantics and pin/Esc rules also come from the official documentation. 2026 was checked through Help. The local visual inspection covered the 2025 build. TheCAD's dark palette, logical-pixel spacing and toolbar glyphs are original adaptations. Color measurements above are visual observations, not sampled theme constants.

[2025 Measure](https://help.solidworks.com/2025/english/SolidWorks/sldworks/HIDD_MEASURE.htm) · [2026 Measure](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_MEASURE.htm?id=3.10) · [Using Measure](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_using_the_measure_tool.htm) · [Circular distance conditions](https://help.solidworks.com/2013/english/WhatsNew/t_wntut_measuring_distance_two_circles.htm) · [Quick Copy](https://help.solidworks.com/2022/english/WhatsNew/c_wn2022_fundamentals_quick_copy_setting.htm?id=3.2) · [Projected area](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_calculating_proj_surf_area.htm?id=23.6.36.3)

## Implementation and parity checklist

| Behavior | TheCAD status and scope |
|---|---|
| Evaluate activation, preselection, update and cleanup | Implemented. The checked action opens the left Measure dock and starts a temporary inspection selection in parts and active sketches |
| Pick and remove | Implemented. Click to add or toggle up to six items. Blank space clears. List supports Remove or Delete. Select Other and selection filters reuse the viewport's tolerance and occlusion rules |
| Vertices and sketch points | Implemented. Exact coordinates relative to the part origin or the chosen frame |
| Lines, circles, arcs, splines, construction curves | Implemented. BRep lengths. Lines report direction. Circles and arcs report center, radius, diameter, circumference and sweep. An arc's Length row is its actual trimmed length |
| Legacy compound sketch rectangles | Individual sides of a rectangle stored as a single old-style sketch entity cannot be measured directly in sketch mode. Rectangles stored as four sketch lines and edges on generated solids are supported |
| Faces, cylinders and solids | Implemented. Surface area, trimmed face boundary perimeter including inner wires, cylinder radius/diameter/axis, planar normal. Periodic seams excluded. Body inspection includes surface and projected area |
| Point, curve, face, cylinder and datum pairs | Implemented. Minimum distance with witnesses, applicable line/axis/plane angles, center and perpendicular distances. Reference plane and axis display extents never determine distance |
| Multiple curves or faces | Implemented. Sum lengths or areas. Unsupported mixed aggregates show an explicit message |
| Circular modes | Implemented for applicable geometry. Center distance uses circle centers or cylinder axes. Trimmed circular edges support minimum, maximum and mixed arc conditions. Full or trimmed cylindrical faces support radial placement when valid witnesses exist in a common normal section. The panel calls this **maximum radial distance** |
| General maximum of arbitrary freeform surfaces | Unsupported. Minimum remains available. No farthest-distance value is inferred from a mesh or bounding box |
| Point-to-point | Implemented. Identical face can receive both picks. Face/body picks intersect the trimmed BRep with the viewing ray. Edge points lie on the geometric edge. Third point starts the next pair |
| XYZ and relative coordinates | Implemented. Signed differences and separate X/Y/Z graphics. Part origin, active sketch and custom plane frames. A standalone coordinate-system feature and rotation between two such features require a future document feature |
| Projection | Implemented. None, screen, selected planar face or datum. Reference can be replaced while retaining picks. In-plane and normal components use geometric witnesses. Screen projection refreshes after view changes |
| Projected surface area | Implemented. Planar area uses the normal dot product. Other supported faces and bodies use OCCT geometric hidden-line projection, planar partitioning and intersection classification to preserve holes and overlaps. Computed on a worker using a detached shape |
| Units | Implemented. Document mm, custom metric/imperial units, degrees/radians, independent decimal precision, scientific notation, dual length/area units and fractional inches. Document format currently supplies mm only |
| Copy, font, history | Implemented. Copy a result value with or without units, copy all rows or a history entry, adjust result text size. History is shared in memory across part windows during the process |
| Appearance and navigation | Owner-requested layout. Measure opens in the left dock below the FeatureManager. Every measurement mode has a visible name. Display controls callout visibility, result visibility and whether Esc keeps the panel open. The result table and viewport labels default to 16 logical pixels and resize together with A− and A+. Temporary green selection, colored spans, leaders, end markers and arrows remain attached to the geometry during navigation |
| Precision slider | Deferred. Computations use the modeling kernel's tolerance and adaptive integration. Display decimals affect formatting |
| Sensors | Disabled. Requires persistent document-owned sensors, stable reference resolution at regeneration, thresholds and notifications |
| Assemblies, drawings, mass properties | Outside this milestone. The solver-independent model API can be used by future inspectors |
| Reference details beyond this workflow | Document/configuration metadata beyond the part filename, callout-embedded arc menus, arbitrary editing-command coexistence and global status-bar Quick Measure remain separate work |

## Numerical and interaction verification

The modeling tests use an absolute comparison tolerance of 1e-7 in the stated unit, with exact input geometry. This is a regression criterion for these small fixtures, not a universal accuracy guarantee.

| Fixture | Expected result |
|---|---|
| Straight edge from (0,0,0) to (3,4,0) | 5 mm |
| Circle of radius 3 | Radius 3 mm, diameter 6 mm, length 6π mm |
| Quarter-circle of radius 4 | Length 2π mm, sweep 90°, center at origin |
| Points separated by (3,4,12) | 13 mm. XY projection 5 mm, normal component 12 mm |
| Point (2,3,0) and bounded X edge | Minimum 3 mm |
| Offset parallel trimmed faces and infinite datums | 12 mm trimmed separation. 3 mm datum separation regardless of displayed location |
| Intersecting reversed straight edges | Acute angle 45° |
| Circles of radii 3 and 2, centers 12 apart | Clearance 7 mm and center spacing 12 mm |
| Circles of radii 4 and 2, centers 10 apart | Maximum 16 mm and near/center 6 mm |
| Parallel finite cylinders of radii 4 and 2, axes 10 apart | Surface clearance 4 mm, axes 10 mm, radial maximum 16 mm |
| Rotated, translated local frame | (4, −3, 12) instead of global point coordinates (13, 9, 13) |
| Three edges of lengths 4, 3 and 12 | 19 mm |
| 10 × 8 × 5 block and a through-hole of radius 1 | Block area 340 mm², projected areas 80 and 80 − π mm² |
| Degenerate edge, null selection, unsupported mixed aggregate | No numeric result |

The reference-manager Qt tests exercise real viewport and tree clicks, tool activation, selection removal, precision and unit conversions, projection controls, same-face point picking, model and sketch immutability, camera invariants, theme changes and overlay cleanup. The modeling tests cover coordinates in a rotated and translated frame. Text and color comparison against SolidWorks on the owner's GPU remains a manual check.

The optional STEP comparison runs the same measurement engine on the project's `test_models/fillet-test.STEP`. It identifies the parallel faces by their measured boundaries, independently of face numbering. Run `thecad_measurement_tests.exe <path-to-fillet-test.STEP>` to repeat it.

| Reading | SolidWorks 2025, native part | TheCAD, STEP export |
|---|---:|---:|
| First face area (mm²) | 883.86 | 883.856740 |
| First face perimeter (mm) | 200.87 | 200.865658 |
| Second face area (mm²) | 1068.20 | 1068.208666 |
| Second face perimeter (mm) | 137.98 | 137.983081 |
| Parallel-face separation (mm) | 7.28 | 7.279405 |
| Combined area (mm²) | 1952.06 | 1952.065406 |

The separation and both perimeters agree at two decimals. The second area and combined area differ by one unit in the last displayed decimal. The STEP comparison allows 0.01 in the stated unit. The analytic fixtures retain their separate 1e-7 threshold.

The Windows Release build passed all 62 CTest entries, including the modeling measurement fixtures and the reference-manager interaction suite at normal and 150% display scaling. In an earlier run the 150% touchpad test failed during viewport initialization. It passed both on its own and in the full final run. The packaged executable passed all four absolute/relative part and assembly launch cases with the development DLL path removed. The optional fifth case for an externally supplied saved TheCAD part was skipped.

## Manual acceptance

1. Open the fillet test part or a plate with a hole. Select Evaluate → Measure, click a face and then a second face. Check green highlights, area, perimeter, separation, total area and XYZ. Click a selected face again and then blank space.
2. In a sketch, measure a line, circular arc, point and construction line. Drag near an endpoint. Close Measure and verify that the sketch and Undo history are intact.
3. Compare two circular edges using center, minimum, maximum and mixed conditions. Measure two points on the same face. Switch primary, angular and dual units and copy a value into a text field.
4. Pick a projection plane in the tree, replace it with a planar face, then try Screen while orbiting. Project a drilled body's area normal to the hole.
5. Check that the left dock leaves the tree available for picking. On a short display, expand Measurement controls to reach the named commands. Move a label, orbit and try A+ and A−. Check the panel values and the viewport labels at both sizes and in both themes. In Display, toggle callouts and Keep open on Esc. Close the dock with its title-bar button and reopen from Evaluate.
6. Reopen the tool and inspect History. Confirm that selecting and clearing targets did not change camera orientation or mark the part modified.
