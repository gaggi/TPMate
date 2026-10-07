# TPMate

A small native Windows tray application for iRacing Trading Paints. TPMate is written in C++20 and uses the Windows API directly. It has no .NET or Windows Forms dependency.

## Current features

- Tray icon with Open, Clean iRacing Paints Folder, and Exit commands
- Main window shows iRacing connection status, logs, and settings
- DPI-aware Segoe UI layout groups Paints, Behavior, and Downloads & logging; the activity log uses a monospace font
- The window keeps its standard size; car paint layers are indented below Cars in the vertical Paints list
- Only one instance runs per Windows session; starting either executable again opens the existing window
- Select Cars, Helmets, and Suits independently, with Numbers and Spec Maps options for Cars
- Only download present drivers is enabled by default. It filters the historical roster using `CarIdxTrackSurface`, includes cars in pit stalls/off track, and preloads the player's own car even in the garage. Other garage cars are deferred until they appear in the world; this is a world-presence filter, not an exact server connection list.
- Presence changes are sampled by the existing one-second monitor and automatically trigger downloads without re-parsing the session info for newly appearing drivers. Already processed drivers are remembered for the session and their paints remain installed when they disappear. Disable the checkbox to load the complete roster as before.
- If presence telemetry is missing or temporarily invalid, remote downloads wait for valid data rather than downloading the historical roster; this also applies when checking for fresh paints with Ctrl+R. Team API results are filtered to the eligible roster before asset downloads.
- Start with Windows creates a per-user logon task; changing this option prompts for UAC once, while subsequent logons start TPMate in the tray with normal user privileges
- Settings are saved in `%APPDATA%\TPMate\settings.ini`
- Closing or minimizing the window hides it in the tray by default
- The tray tooltip reflects the iRacing connection status
- Ctrl+R re-downloads session paints when iRacing is connected, its simulator window is active, and the reload option is enabled; detection uses Raw Input (no keyboard hook), so it never delays iRacing's keyboard input, and ignores key repeats and injected keys
- Version 0.1.0; Check for updates is available in the window and tray menu and only contacts GitHub when clicked
- Updates use the latest published release of `gaggi/TPMate`; install downloads the matching executable and restarts TPMate, or opens the GitHub release page
- Runs at Low CPU priority and, where Windows supports it, in efficiency mode (EcoQoS) so iRacing keeps the performance cores; hidden log updates do not request scrolling or immediate repaints
- While iRacing is not running, the monitor only probes for the shared memory once per second; the process list is only checked to detect the simulator exiting
- Clean iRacing Paints Folder moves `.tga` and `.mip` paint files under `Documents\iRacing\paint` to the Recycle Bin after confirmation
- Downloads Trading Paints for the current session and for newly joined drivers, with configurable concurrency
- Reloads all car textures after the initial download and only affected cars after later downloads
- Reports installed paints in one reload summary, grouped as Cars, Suits, and Helmets; multiple car paint layers count as one car
- Downloads overwrite existing paint files without creating backups or restoring previous files
- Optional cleanup deletes downloaded paint files when iRacing exits
- Download workers install each paint immediately instead of retaining the entire batch in RAM; at most two paints are unpacked/installed concurrently, and each install appends one line to the cleanup list instead of rewriting it
- HTTP requests share a WinHTTP session and read response bodies in 64 KB blocks
- The download worker sleeps until new session information, settings, or reload requests arrive
- Verbose logging includes the total time spent processing each paint batch

## Build

Requires CMake 3.21+ and Visual Studio 2022 Build Tools with the C++ toolchain. Build both architectures with:

```powershell
.\build.ps1
```

This creates both release builds in the regular `build` directory:

- x64: `build\x64\Release\TPMate.exe`
- x86: `build\x86\Release\TPMate.exe`

## GitHub releases

Use version tags such as `v0.1.0`. Attach standalone executable assets named
`TPMate-windows-x64.exe` and `TPMate-windows-x86.exe` (versioned names ending in
`windows-x64.exe` / `windows-x86.exe` also work). If a newer release lacks a
matching executable, the update dialog offers its GitHub page. Drafts and
prereleases are excluded by GitHub's latest-release endpoint. No published release
yet is shown as no update available; network and API failures are reported.

Before releasing a new version, update the CMake project version and the
Windows VERSIONINFO in `resources/resources.rc` together.

The update tests can be built as the `TPMateUpdateTests` target and run directly;
they verify version comparisons and release metadata for each architecture offline.
