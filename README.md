# Bellwin

Bellwin rings a bell at an unpredictable time between the configured minimum
and maximum intervals. Quiet hours suppress the sound, while the tray icon
keeps the program available after the settings window is closed.

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
