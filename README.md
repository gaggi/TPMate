# TPMate

A small native Windows tray application for iRacing Trading Paints. TPMate is written in C++20 and uses the Windows API directly. It has no .NET or Windows Forms dependency.

## Current features

- Tray icon with Open, Clean iRacing Paints Folder, and Exit commands
- Main window shows iRacing connection status, logs, and settings
- Only one instance runs per Windows session; starting either executable again opens the existing window
- Select Cars, Helmets, and Suits independently, with Numbers and Spec Maps options for Cars
- Settings are saved in `%APPDATA%\TPMate\settings.ini`
- Closing or minimizing the window hides it in the tray by default
- The tray tooltip reflects the iRacing connection status
- Clean iRacing Paints Folder moves `.tga` and `.mip` paint files under `Documents\iRacing\paint` to the Recycle Bin after confirmation

- Downloads Trading Paints for the current session and for newly joined drivers, with configurable concurrency
- Reloads all car textures after the initial download and only affected cars after later downloads
- Reports installed paints in one reload summary, grouped as Cars, Suits, and Helmets; multiple car paint layers count as one car
- Downloads overwrite existing paint files without creating backups or restoring previous files
- Optional cleanup deletes downloaded paint files when iRacing exits
- Download workers install each paint immediately instead of retaining the entire batch in RAM; at most two paints are unpacked/installed concurrently
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
