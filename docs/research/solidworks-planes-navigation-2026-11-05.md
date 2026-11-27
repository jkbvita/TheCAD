# SolidWorks plane/navigation reference — 2026-11-05

The prepared disposable research model remained open in SOLIDWORKS Premium 2025
SP2.0. A fresh activation showed the plate's top face selected in cyan. Further
attempts to open the plane controls were interrupted by active local user input;
automation was stopped rather than competing for the desktop. No new midplane,
tangent-plane or mouse-navigation interaction is claimed as directly observed.
Earlier actual offset-plane/axis/point observations are recorded in
[the preceding report](solidworks-reference-managers-2026-11-05.md).

The supported computer-control API exposes left-button drags, but no modified
middle-button drag, so it cannot faithfully exercise the requested navigation
chords in SolidWorks. Those controls were verified against official documentation
and are exercised through native Qt mouse events in TheCAD's automated tests.
No proprietary implementation, icons or graphics were extracted.

Official references:

- [Middle Mouse Button](https://help.solidworks.com/2021/english/SolidWorks/sldworks/r_Middle_Mouse_Button.htm)
  and the indexed [Middle Mouse Button Functions](https://help.solidworks.com/2010/english/SolidWorks/sldworks/LegacyHelp/Sldworks/Display/Middle_Mouse_Button_Functions.htm)
  document middle drag rotation, Ctrl-middle panning, Shift-middle zooming and
  cursor-directed wheel zoom. The newer URL returned only its web-help shell
  through the reader; the indexed official article supplied the actual controls.
- [Rotate View](https://help.solidworks.com/2023/English/solidworks/sldworks/t_rotate_view.htm)
  and its [indexed official article](https://help.solidworks.com/2010/english/SolidWorks/sldworks/LegacyHelp/Sldworks/Display/Rotate_View.htm)
  describe geometry-selected rotation. TheCAD uses the exact picked position
  on the selected vertex, edge or face as the pivot.
- [Plane PropertyManager](https://help.solidworks.com/2014/english/solidworks/sldworks/hidd_dve_create_plane.htm)
  documents reference-dependent constraints: offset/coincident, mid-plane,
  perpendicular through a point, parallel through a point, angle about a line
  and tangent constructions. This milestone supports analytic cylindrical
  tangency; general curved/conical tangent plane solvers remain unsupported.

The owner's explicit #06B4FF color and orange-outline hover are the appearance
specification, independently of installed SolidWorks settings. The 2026-11-06
correction makes selected solid faces fully blue and unselected bodies neutral
light gray (#D0D0D0). Reference planes retain slight selected transparency. Persistent naming was added following the owner's explicit scope approval.
