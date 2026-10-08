#include "AppSettings.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

namespace
{
    constexpr wchar_t kSection[] = L"Settings";

    int ReadInt(const std::wstring& path, const wchar_t* key, int fallback)
    {
        return static_cast<int>(GetPrivateProfileIntW(kSection, key, fallback, path.c_str()));
    }

    bool ReadBool(const std::wstring& path, const wchar_t* key, bool fallback)
    {
        return ReadInt(path, key, fallback ? 1 : 0) != 0;
    }

    void WriteInt(const std::wstring& path, const wchar_t* key, int value)
    {
        WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), path.c_str());
    }

    void WriteBool(const std::wstring& path, const wchar_t* key, bool value)
    {
        WritePrivateProfileStringW(kSection, key, value ? L"1" : L"0", path.c_str());
    }
}

std::wstring AppSettings::Path()
{
    wchar_t appData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
        return L".\\settings.ini";
    std::wstring directory = std::wstring(appData) + L"\\TPMate";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\settings.ini";
}

AppSettings AppSettings::Load()
{
    const auto path = Path();
    AppSettings settings;
    settings.minimizeToTray = ReadBool(path, L"MinimizeToTray", true);
    settings.deleteAfterSession = ReadBool(path, L"DeleteAfterSession", true);
    settings.refreshOnTextureReload = ReadBool(path, L"RefreshOnTextureReload", true);
    settings.onlyPresentDrivers = ReadBool(path, L"OnlyPresentDrivers", true);
    settings.loadCars = ReadBool(path, L"LoadCars", true);
    settings.loadHelmets = ReadBool(path, L"LoadHelmets", true);
    settings.loadSuits = ReadBool(path, L"LoadSuits", true);
    settings.loadNumbers = ReadBool(path, L"LoadNumbers", true);
    settings.loadSpecMaps = ReadBool(path, L"LoadSpecMaps", true);
    settings.maxConcurrentDownloads = static_cast<unsigned int>((std::clamp)(ReadInt(path, L"MaxConcurrentDownloads", 5), 1, 10));
    const int level = ReadInt(path, L"LogLevel", static_cast<int>(LogLevel::Info));
    settings.logLevel = level < static_cast<int>(LogLevel::Verbose) || level > static_cast<int>(LogLevel::Error) ?
        LogLevel::Info : static_cast<LogLevel>(level);
    settings.windowLeft = ReadInt(path, L"WindowLeft", 0);
    settings.windowTop = ReadInt(path, L"WindowTop", 0);
    settings.windowWidth = ReadInt(path, L"WindowWidth", 0);
    settings.windowHeight = ReadInt(path, L"WindowHeight", 0);
    settings.windowMaximized = ReadBool(path, L"WindowMaximized", false);
    return settings;
}

void AppSettings::Save() const
{
    const auto path = Path();
    WriteBool(path, L"MinimizeToTray", minimizeToTray);
    WriteBool(path, L"DeleteAfterSession", deleteAfterSession);
    WriteBool(path, L"RefreshOnTextureReload", refreshOnTextureReload);
    WriteBool(path, L"OnlyPresentDrivers", onlyPresentDrivers);
    WriteBool(path, L"LoadCars", loadCars);
    WriteBool(path, L"LoadHelmets", loadHelmets);
    WriteBool(path, L"LoadSuits", loadSuits);
    WriteBool(path, L"LoadNumbers", loadNumbers);
    WriteBool(path, L"LoadSpecMaps", loadSpecMaps);
    WriteInt(path, L"MaxConcurrentDownloads", static_cast<int>(maxConcurrentDownloads));
    WriteInt(path, L"LogLevel", static_cast<int>(logLevel));
    if (windowWidth > 0 && windowHeight > 0)
    {
        WriteInt(path, L"WindowLeft", windowLeft);
        WriteInt(path, L"WindowTop", windowTop);
        WriteInt(path, L"WindowWidth", windowWidth);
        WriteInt(path, L"WindowHeight", windowHeight);
        WriteBool(path, L"WindowMaximized", windowMaximized);
    }
}
