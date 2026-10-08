#pragma once

#include "AppLog.h"

#include <string>

// Everything TPMate keeps in %APPDATA%\TPMate\settings.ini ([Settings] section).
// "Start with Windows" is not stored here; it is the logon task itself.
struct AppSettings
{
    bool minimizeToTray = true;
    bool deleteAfterSession = true;
    bool refreshOnTextureReload = true;
    bool onlyPresentDrivers = true;
    bool loadCars = true;
    bool loadHelmets = true;
    bool loadSuits = true;
    bool loadNumbers = true;
    bool loadSpecMaps = true;
    unsigned int maxConcurrentDownloads = 5;
    LogLevel logLevel = LogLevel::Info;

    // Normal (restored) window rectangle in screen pixels; width 0 means not saved yet.
    int windowLeft = 0;
    int windowTop = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    bool windowMaximized = false;

    static std::wstring Path();
    static AppSettings Load();
    void Save() const;
};
