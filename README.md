# Bellwin

Bellwin rings a bell at an unpredictable time between the configured minimum
and maximum intervals. Quiet hours suppress the sound, while the tray icon
keeps the program available after the settings window is closed.

All settings controls support keyboard focus with `Tab` and `Shift+Tab`.
Focused sliders use the left/right arrows, the switch and install button use
`Space` or `Enter`, and the selected hour/minute pair can be typed directly or
changed with the up/down arrows. Sliders and individual time pairs also react
to the mouse wheel while hovered. Sliders additionally react to horizontal
touchpad scrolling. An upward or rightward gesture increases the hovered
value; a downward or leftward gesture decreases it.

Mouse clicks keep keyboard focus without drawing a focus outline. Using the
keyboard reveals the outline, while time inputs use the selected digit pair as
their focus indication. Left/right arrows cycle between the hours and minutes
pairs.

Left-clicking the tray icon opens its context menu; double-clicking opens
Settings. The menu can ring the bell immediately, show how long ago the
previous bell rang, pause ringing for 30 minutes, 1 hour, or 2 hours, toggle an
indefinite pause, or exit the application. While paused, the tray icon is
desaturated and rotated 90° counter-clockwise,
and the menu shows when a timed pause ends. While a timed pause is active, the
**Pause for** submenu includes **Unpause**. Pauses survive application restarts.
After a timed or indefinite pause ends, Bellwin starts a fresh random interval
before the next bell.

Random intervals count only time outside quiet hours. Bellwin arms a timer for
the next meaningful event instead of polling every second. Volume changes
preview the bell without changing its schedule.

## Build

The project follows the single-command native build used by
`signature_helper`:

```powershell
.\nob.exe
```

The output is `Bellwin.exe`, a 32-bit Windows GUI executable targeting Windows
7 (`6.1`) and newer. The application icon and MP3 bell sound are embedded into
the executable. Theme support uses only Windows system APIs and does not add a
runtime or sidecar DLL, so the distributed application remains one executable.

The settings window layout is computed with the vendored
[Clay](https://github.com/nicbarker/clay) single-header library
(`thirdparty/clay.h`, pinned at v0.14) and rendered through a small GDI
backend, so the executable still has no runtime dependencies. The semantic
widget vocabulary and shared formatting rules live in `widgets.h`; the widget
table, focus model, hit testing, and input dispatch live in `ui.c`; the
declarative screen layout and single Clay implementation live in `layout.c`,
GDI rendering lives in
`render_gdi.c`, and the native accessibility provider lives in `uia.c`.
These seams are covered by headless tests.

Run the scheduling, widget, layout, command-line parsing, IPC-message
validation, and theme-resolution tests with:

```powershell
.\nob.exe --test
```

## Accessibility and UI Automation

The settings window exposes its semantic controls through the native Windows
UI Automation provider included with Windows 7 and newer. This makes the UI
readable by Narrator and scriptable through clients such as Accessibility
Insights, FlaUI, and pywinauto without adding a runtime dependency.

The three sliders support `RangeValue` plus a read-only, human-formatted
`Value`. Each quiet-hours field is a group containing separate hour and minute
spinners with `RangeValue`. **Launch at login** supports `Toggle`, while the
visible **Install** or **Update** button supports `Invoke`. Automation IDs are
stable across runs, and focus, value, and toggle changes raise the matching UI
Automation events.

## Command line automation

The executable is also a command-line client for the running Bellwin instance.
If Bellwin is not running, an action starts it in the background first. Every
command returns status `0` on success, writes machine-readable `key=value`
output to standard output, and writes errors to standard error.

```powershell
Bellwin.exe --ring
Bellwin.exe --pause 30        # also 60 or 120
Bellwin.exe --unpause
Bellwin.exe --show
Bellwin.exe --get volume
Bellwin.exe --set volume=40
Bellwin.exe --status
```

Supported setting keys are `volume`, `minimum-interval`, `maximum-interval`,
`quiet-start`, `quiet-end`, and `autostart`. Volumes use `0` through `100`;
intervals use 30-minute steps from `30` through `480`; quiet-hour values use
`HH:MM`; and autostart accepts `on` or `off`. `--status` also reports `pause`,
`pause-until`, and `last-ring`; timestamps are Unix seconds.

Commands use a versioned, fixed-size `WM_COPYDATA` protocol between the CLI
process and the single GUI instance. Argument parsing uses the vendored
[flag.h](https://github.com/tsoding/flag.h) single-header library pinned to
commit `7d3699298551080678d7763adcdd22e78873f4c4`; its MIT license is stored in
`thirdparty/flag.LICENSE`. No sidecar DLL or helper executable is required.

## Windows themes

Bellwin follows the Windows high-contrast palette on Windows 7 and newer.
Outside high contrast, it uses the system DWM colorization color as its accent
when available and falls back to the built-in Bellwin accent if DWM cannot
provide one.

On Windows 10 and newer, the settings window follows the user's app light/dark
preference. On Windows 11 build 22000 and newer, the standard title bar follows
dark mode too. Older systems retain their normal system-drawn title bar. Theme,
system-color, and accent changes are applied while Bellwin is running. The tray
menu remains a native Windows menu so the operating system controls its theme
and accessibility behavior.

## Installation

The **Install** button copies the executable to
`%LOCALAPPDATA%\Bellwin\Bellwin.exe` and creates a shortcut in the Start menu.
If the installed file has a different version, the button is shown as
**Update**. **Launch at login** adds or removes the per-user startup entry;
startup launches Bellwin in the tray without opening the settings window.
