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

Run the core scheduling and theme-resolution tests with:

```powershell
.\nob.exe --test
```

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
