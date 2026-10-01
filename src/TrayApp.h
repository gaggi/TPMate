#pragma once

#include "AppLog.h"
#include "IRacingMonitor.h"
#include "PaintDownloader.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class TrayApp final
{
public:
    TrayApp();
    int Run(HINSTANCE instance);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
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
    void UpdateConnection(bool connected);

    HWND window_ = nullptr;
    HWND statusText_ = nullptr;
    HWND logEdit_ = nullptr;
    HWND minimizeCheck_ = nullptr;
    HWND deleteCheck_ = nullptr;
    HWND reloadCheck_ = nullptr;
    HWND loadCarsCheck_ = nullptr;
    HWND loadHelmetsCheck_ = nullptr;
    HWND loadSuitsCheck_ = nullptr;
    HWND loadNumbersCheck_ = nullptr;
    HWND loadSpecMapsCheck_ = nullptr;
    HWND logLevelLabel_ = nullptr;
    HWND logLevelCombo_ = nullptr;
    HWND concurrencyLabel_ = nullptr;
    HWND concurrencyCombo_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HMODULE richEditModule_ = nullptr;
    UINT iracingBroadcastMessage_ = 0;
    bool connected_ = false;
    bool minimizeToTray_ = true;
    bool deleteAfterSession_ = true;
    bool autoRefreshOnReload_ = true;
    bool loadCars_ = true;
    bool loadHelmets_ = true;
    bool loadSuits_ = true;
    bool loadNumbers_ = true;
    bool loadSpecMaps_ = true;
    unsigned int maxConcurrentDownloads_ = 5;
    std::atomic<LogLevel> minimumLogLevel_{LogLevel::Info};
    NOTIFYICONDATAW iconData_{};
    std::mutex logMutex_;
    std::vector<std::wstring> pendingLogs_;
    IRacingMonitor monitor_;
    PaintDownloader downloader_;
};
