#pragma once

#include "AppLog.h"
#include "AppSettings.h"
#include "IRacingMonitor.h"
#include "PaintDownloader.h"
#include "Pages.h"
#include "UpdateChecker.h"
#include "ReloadShortcut.h"
#include "ui/NavBar.h"
#include "ui/RowList.h"
#include "ui/ScrollHost.h"
#include "ui/StatusPanel.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class TrayApp final
{
public:
    TrayApp();
    int Run(HINSTANCE instance);

private:
    enum class Page { Session, Paints, Activity, Settings };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void UpdateReloadShortcutListener();
    void OnRawKeyboardInput(HRAWINPUT input);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateFonts();
    void CreateControls();
    void ShowPage(Page page);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowMenu();
    void ShowStatusWindow();
    void SaveSettings();
    void CaptureWindowPlacement();
    void RestoreWindowPlacement();
    void ApplySettings();
    PageContext MakePageContext();
    void PostLog(LogLevel level, const std::string& message);
    void AppendQueuedLogs();
    void LayoutControls(int width, int height);
    void UpdateConnection(bool connected);
    void UpdateStatusDisplay();
    void RefreshActivityRows();
    void ChooseLogLevel();
    void RequestPaintRefresh();
    void StartUpdateCheck();
    void FinishUpdateCheck();
    void StartUpdateInstall();
    std::wstring ApplyStartWithWindows(bool enabled);
    void RefreshPage();
    void OpenPaintFolder();
    void CleanPaintFolder();
    void FinishUpdateInstall();

    HWND window_ = nullptr;
    NavBar navBar_;
    StatusPanel banner_;
    HWND pageTitle_ = nullptr;
    HWND pageHint_ = nullptr;
    Page page_ = Page::Session;
    ScrollHost pageHost_;
    RowList activityRows_;
    HWND logEdit_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HMODULE richEditModule_ = nullptr;
    UINT dpi_ = 96;
    HFONT uiFont_ = nullptr;
    HFONT headingFont_ = nullptr;
    HFONT logFont_ = nullptr;
    bool reloadShortcutListening_ = false;
    ReloadShortcut reloadShortcut_;
    UINT iracingBroadcastMessage_ = 0;
    bool connected_ = false;
    AppSettings settings_;
    bool startWithWindows_ = false;
    std::atomic<LogLevel> minimumLogLevel_{LogLevel::Info};
    NOTIFYICONDATAW iconData_{};
    std::mutex logMutex_;
    std::vector<std::wstring> pendingLogs_;
    std::mutex progressMutex_;
    PaintProgress progress_;
    std::atomic_bool progressPosted_{false};
    IRacingMonitor monitor_;
    PaintDownloader downloader_;
    std::thread updateThread_;
    UpdateState update_;
    UpdateCheckResult updateResult_;
    std::filesystem::path downloadedUpdate_;
    std::wstring updateError_;
    std::wstring openFolderMessage_;
    std::wstring cleanFolderMessage_;
    // While the clean-up question is open, page switches wait: the page that asked must
    // outlive the message box, which keeps tray commands running.
    bool cleanQuestionOpen_ = false;
    std::optional<Page> pendingPage_;
};
