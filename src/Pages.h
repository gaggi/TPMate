#pragma once

#include "AppSettings.h"

#include <functional>
#include <windows.h>

// What the main window shares with the pages it embeds.
struct PageContext
{
    HINSTANCE instance{};
    HFONT headingFont{};
    HFONT textFont{};
    AppSettings* settings{};
    // Saves settings.ini and hands the new values to the downloader right away.
    std::function<void()> settingsChanged;
};

// Each function creates a page as a child of `parent` (the page scroll host). The
// page owns itself and is gone when its window is destroyed.
HWND CreatePaintsPage(const PageContext& context, HWND parent);
