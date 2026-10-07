#pragma once

#include "AppLog.h"
#include "IRacingMonitor.h"
#include "PaintDownloader.h"
#include "UpdateChecker.h"
#include "ReloadShortcut.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class TrayApp final
{
public:
    TrayApp();
    int Run(HINSTANCE instance);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    void UpdateReloadKeyboardHook();
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowMenu();
    void ShowStatusWindow();
    void SaveSettings();
    void ApplyPaintOptions();
    void PostLog(LogLevel level, const std::string& message);
    void AppendQueuedLogs();
    void LayoutControls(int width, int height);
    void UpdateFonts();
    void UpdateConnection(bool connected);
    void StartUpdateCheck();
    void FinishUpdateCheck();
    void StartUpdateInstall(UpdateReleaseInfo release);
    void FinishUpdateInstall();

    HWND window_ = nullptr;
    HWND statusText_ = nullptr;
    HWND updateButton_ = nullptr;
    HWND logEdit_ = nullptr;
    HWND activityLabel_ = nullptr;
    HWND paintsGroup_ = nullptr;
    HWND behaviorGroup_ = nullptr;
    HWND downloadsGroup_ = nullptr;
    HWND minimizeCheck_ = nullptr;
    HWND deleteCheck_ = nullptr;
    HWND reloadCheck_ = nullptr;
    HWND presentDriversCheck_ = nullptr;
    HWND loadCarsCheck_ = nullptr;
    HWND loadHelmetsCheck_ = nullptr;
    HWND loadSuitsCheck_ = nullptr;
    HWND loadNumbersCheck_ = nullptr;
    HWND loadSpecMapsCheck_ = nullptr;
    HWND startupCheck_ = nullptr;
    HWND logLevelLabel_ = nullptr;
    HWND logLevelCombo_ = nullptr;
    HWND concurrencyLabel_ = nullptr;
    HWND concurrencyCombo_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HMODULE richEditModule_ = nullptr;
    UINT dpi_ = 96;
    HFONT uiFont_ = nullptr;
    HFONT headingFont_ = nullptr;
    HFONT logFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HHOOK reloadKeyboardHook_ = nullptr;
    ReloadShortcut reloadShortcut_;
    inline static TrayApp* keyboardHookOwner_ = nullptr;
    UINT iracingBroadcastMessage_ = 0;
    bool connected_ = false;
    bool minimizeToTray_ = true;
    bool deleteAfterSession_ = true;
    bool autoRefreshOnReload_ = true;
    bool onlyPresentDrivers_ = true;
    bool loadCars_ = true;
    bool loadHelmets_ = true;
    bool loadSuits_ = true;
    bool loadNumbers_ = true;
    bool loadSpecMaps_ = true;
    bool startWithWindows_ = false;
    unsigned int maxConcurrentDownloads_ = 5;
    std::atomic<LogLevel> minimumLogLevel_{LogLevel::Info};
    NOTIFYICONDATAW iconData_{};
    std::mutex logMutex_;
    std::vector<std::wstring> pendingLogs_;
    IRacingMonitor monitor_;
    PaintDownloader downloader_;
    std::thread updateThread_;
    bool updateBusy_ = false;
    UpdateCheckResult updateResult_;
    std::filesystem::path downloadedUpdate_;
    std::wstring updateError_;
};
