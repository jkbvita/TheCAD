# 0043. Drafted edges, standard views, and Measure

Open CASCADE's draft history can report faces from before wire correction.
Recover the final mapped faces through the draft modifier. Keep the legacy
edge and vertex aliases alongside names derived from the recovered faces.
That allows existing downstream treatments to reopen and gives new
treatments persistent edge references through extrusion and mirror. The
kernel still validates each treatment. Some junctions and tangent seams
cannot accept a radius or chamfer.

The View menu exposes the SolidWorks standard shortcuts. Ctrl+1 through
Ctrl+6 select Front, Back, Left, Right, Top, and Bottom. Ctrl+7 selects
Isometric. These seven presets keep the current camera target, zoom, and
projection. Ctrl+8 retains Normal To, and Home retains Reset View.

The existing Measure command has a docked panel with the current picks,
copyable results, configurable units and precision, XYZ differences, and
a choice of minimum or center distance for circular edges. Its readout
and viewport callout include face area. It continues to accept one or two
solid edges, faces, or vertices.

SolidWorks uses several different Evaluate interfaces. Measure uses a
modeless dialog with an expandable results area and viewport callouts.
Mass Properties and Section Properties report calculated physical and
section values in dialogs. Check Entity lists geometry issues and can
highlight a result. Performance Evaluation lists rebuild cost by feature.
Draft Analysis uses a PropertyManager and colors faces by their draft
relative to the pull direction. These other Evaluate tools and Measure's
arbitrary point picking, projection, history, and sensors are outside
this implementation.

[SolidWorks view shortcuts](https://files.solidworks.com/Supportfiles/Whats_new/2025/English/whatsnew.pdf)
[SolidWorks Measure Tool](https://help.solidworks.com/2024/English/SolidWorks/sldworks/HIDD_MEASURE.htm)
[SolidWorks Mass Properties](https://help.solidworks.com/2021/english/SolidWorks/sldworks/hidd_massproperty_text_dlg.htm?format=P&value=)
[SolidWorks Section Properties](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_section_properties.htm?format=P&value=)
[SolidWorks Check Entity](https://help.solidworks.com/2020/english/SolidWorks/Sldworks/t_check_entity_assembly.htm)
[SolidWorks Performance Evaluation](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_FEATURE_REPORT.htm)
[SolidWorks Draft Analysis](https://help.solidworks.com/2024/english/solidworks/sldworks/r_draft_analysis.htm)
