# STL/STEP export manual checks

Restart TheCAD from the updated desktop shortcut. Use File → Save As and select
STEP or STL Mesh in the Windows dialog's file-type dropdown.

1. Check the dialog is the standard Windows picker, including folder navigation,
   filename and file-type controls. Open should use the Windows picker too.
2. Export a box/cut model to .step and .stl. Switch file types and check the final
   extension, including a filename with spaces/accents and the .stp alias.
3. Open STEP in another CAD viewer and STL in a mesh viewer/slicer. Check body
   count, holes, placement and dimensions. Import STL using millimeters.
4. Export an unsaved/modified part: its original title/path and unsaved marker
   should remain. Save the editable model separately as .thepart and reopen it.
5. Try an existing export filename. Cancel replacement and verify the old file;
   then replace it deliberately. Cancel Save As without creating a file.
6. Try exporting an empty part or a sketch without a solid: expect a clear error.
   Save As should be unavailable during an unfinished feature draft.

Automated verification: full 40-target suite plus focused export/document checks;
STEP geometric read-back, STL binary mesh validation, Unicode paths, source mesh
and document preservation, native dialog flags/type normalization, and safe failed
writes. Packaged startup and isolated export/read-back also passed.

Export is for final solid geometry. .thepart retains parametric history and sketch
relations. Export options and exchange-file import are outside this milestone.
