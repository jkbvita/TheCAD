# 0061. Touchpad camera controls

Owner requested automatic controls when no mouse is connected on 2026-11-18,
and explicitly chose modifier-only movement rather than click-and-drag.
The existing mouse navigation remains available. Both Part and Assembly use the
same touchpad input translator and existing camera math; modeling and solvers
are unchanged.

## Controls and preferences

- Hold Shift and move the pointer to orbit; hold Ctrl and move to pan. No button
  hold is needed. Ctrl takes precedence when both modifiers are held.
- Two-finger scroll pans in touchpad mode; Shift + scroll orbits. Pinch zooms
  when exposed as native zoom or Ctrl+wheel by the driver. Ctrl + two-finger
  scroll is also a zoom fallback. Positive pinch magnifies; ordinary mouse-wheel
  direction remains as previously requested by the owner.
- Zoom-in uses the pointer position; zoom-out remains centered.
- Modifier navigation is limited to the active viewport window, without buttons
  pressed. Selection clicks and active sketch/component drags retain priority.
  A new gesture first captures its position, avoiding a camera jump. Key events,
  leaving the view, focus/grab loss, window deactivation and input-mode changes
  reset its transient state. Navigation changes no document or undo entry.
- Edit > Preferences provides Automatic / Mouse / Touchpad, shared across Part
  and Assembly, and saved under `navigation/inputMode`. Automatic enables
  modifier navigation after detecting no connected mouse. Mouse disables the
  touchpad mappings; Touchpad forces them for drivers detection cannot classify.

## Device detection

A background Qt Core thread-pool task enumerates present, started Windows mouse
class devices through SetupAPI/Configuration Manager. Driver descriptions,
hardware/compatible IDs and ancestor devices identify common touchpad mouse
compatibility interfaces, including HID Digitizer/Touchpad usage 0D/05 and
integrated Synaptics/ELAN devices. Local remote-desktop compatibility mouse
entries are ignored. Refresh runs every two seconds, with no polling on camera
movement and no concurrent duplicate scans. Unknown/error results retain Mouse
behavior in Automatic; the manual override always works.

Detection is intentionally conservative. A generic legacy touchpad may identify
as an ordinary PS/2 mouse; a wireless receiver can remain present while its mouse
is switched off. Remote sessions expose server-side devices. These cases need
the Touchpad override. Actual pinch event delivery also depends on Windows and
the driver; raw finger capture and OS-wide gesture changes are not introduced.

## Reference and implementation

[Microsoft touchpad guidelines](https://learn.microsoft.com/en-us/windows/apps/develop/input/touchpad-interactions)
describe two-finger slide for pan and pinch/stretch for zoom. SolidWorks' touch
documentation describes touch-enabled screens; it is not evidence of a universal
desktop touchpad orbit shortcut. The owner-approved Shift/Ctrl behavior supplies
orbit and pan for this application.

[Qt wheel events](https://doc.qt.io/qt-6/qwheelevent.html) can represent both mouse
wheels and touchpads; [native gesture events](https://doc.qt.io/qt-6/qnativegestureevent.html)
provide platform-dependent pan and pinch values. The translator accepts both,
including horizontal and high-resolution pixel deltas, with accumulated physical
pixel rounding for high-DPI pan.

New files live in `ui/input/`, with shared preferences in `ui/PreferencesDialog`.
An explicit-mode overload in `viewport::CameraNavigation` reuses camera math;
the original middle-button modifier mapping is unchanged. Windows system import
libraries SetupAPI and CfgMgr32 use the existing Windows SDK. No new third-party
dependencies, proprietary resources or document-format changes.

## Verification and manual checks

Automated tests cover device classification, injected absent/present/unknown
device scans and hotplug changes, saved modes, shared preferences, Shift orbit,
Ctrl pan, plain-hover stability, gesture resets, click priority, pan/pinch events,
mouse-wheel compatibility and unchanged sketch/component data. Both viewports
are exercised at normal and high DPI; tests isolate settings from owner data.
The existing GUI suites explicitly select Mouse controls so their results are
independent of the test computer's peripherals.

Manual hardware checks:

1. Unplug the mouse, choose Automatic, wait up to two seconds, then hold Shift
   while moving on the touchpad. Release Shift: ordinary pointing resumes.
2. Hold Ctrl and move: pan without rotating or changing scale. Confirm sketch
   and mate selection clicks still work.
3. Try two-finger pan and pinch zoom in Part and Assembly. If the driver does
   not expose pinch, use Ctrl + two-finger scroll.
4. Reconnect the mouse: Automatic restores ordinary mouse controls within two
   seconds. Verify the established middle-button and wheel controls.
5. If detection misidentifies the touchpad, select Touchpad in Preferences,
   restart the app and confirm that choice persists.

Physical unplug/replug and this owner's touchpad gestures require owner testing;
automated tests inject equivalent input and device changes.

Release verification: all 62 CTest suites passed (332.27 seconds). The touchpad
suites each passed 15 Qt test cases, including normal/high-DPI Part and Assembly
input, gesture reset, fractional pan, saved preferences and device-change cases.
The standalone package passed all four absolute/relative part/assembly launch
cases, with 6 Qt cases passed and the optional owner-model case skipped. The
packaged executable SHA-256 matches the tested Release executable.
