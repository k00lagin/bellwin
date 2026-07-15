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
their focus indication. Left/right arrows select the hours/minutes pair.

Left-clicking the tray icon opens Settings. Its context menu can ring the bell
immediately, pause ringing for 30 minutes, 1 hour, or 2 hours, toggle an
indefinite pause, or exit the application. After a timed or indefinite pause
ends, Bellwin starts a fresh random interval before the next bell.

## Build

The project follows the single-command native build used by
`signature_helper`:

```powershell
.\nob.exe
```

The output is `Bellwin.exe`, a 32-bit Windows GUI executable targeting Windows
7 (`6.1`) and newer. The application icon and MP3 bell sound are embedded into
the executable.

Run the core scheduling tests with:

```powershell
.\nob.exe --test
```

## Installation

The **Install** button copies the executable to
`%LOCALAPPDATA%\Bellwin\Bellwin.exe` and creates shortcuts on the Desktop and in
the Start menu. If the installed file has a different version, the button is
shown as **Update**. **Launch at login** adds or removes the per-user startup
entry; startup launches Bellwin in the tray without opening the settings
window.
