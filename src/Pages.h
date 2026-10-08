#pragma once

#include "AppSettings.h"
#include "PaintDownloader.h"
#include "UpdateChecker.h"

#include <functional>
#include <string>
#include <windows.h>

// Where the update check and install stand; shown in the Settings page's version row.
struct UpdateState
{
    enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Failed };
    Phase phase{Phase::Idle};
    std::wstring message;
    UpdateReleaseInfo release;
};

// The main window sends WM_TIMER with this id to the shown page when state the page
// displays (update check, connection, session) has changed.
inline constexpr UINT_PTR kPageRefreshTimer = 1;

// What the main window shares with the pages it embeds.
struct PageContext
{
    HINSTANCE instance{};
    HFONT headingFont{};
    HFONT textFont{};
    AppSettings* settings{};
    // Saves settings.ini and hands the new values to the downloader right away.
    std::function<void()> settingsChanged;

    // Session page
    const bool* connected{};
    std::function<PaintProgress()> progress;
    // Results of the last paint-folder actions (an error, what was cleaned); empty at first.
    const std::wstring* openFolderMessage{};
    const std::wstring* cleanFolderMessage{};
    std::function<void()> openPaintFolder;
    // Asks first, because it moves every paint file to the Recycle Bin.
    std::function<void()> cleanPaintFolder;

    // Settings page
    const bool* startWithWindows{};
    // Registers or removes the logon task (Windows asks for approval); returns an
    // explanation when nothing changed, or an empty string.
    std::function<std::wstring(bool)> setStartWithWindows;
    const UpdateState* update{};
    std::function<void()> checkForUpdates;
    std::function<void()> installUpdate;
};

// Each function creates a page as a child of `parent` (the page scroll host). The
// page owns itself and is gone when its window is destroyed.
HWND CreateSessionPage(const PageContext& context, HWND parent);
HWND CreatePaintsPage(const PageContext& context, HWND parent);
HWND CreateSettingsPage(const PageContext& context, HWND parent);
