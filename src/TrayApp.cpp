#include "TrayApp.h"
#include "StartupRegistration.h"
#include "Resource.h"

#include <shellapi.h>
#include <shlobj.h>
#include <richedit.h>
#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>

namespace
{
    constexpr DWORD kMainWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    constexpr UINT kTrayMessage = WM_APP + 1;
    constexpr UINT kConnectionChanged = WM_APP + 2;
    constexpr UINT kAppendLogs = WM_APP + 3;
    constexpr UINT kSessionExited = WM_APP + 4;
    constexpr UINT kShowExistingWindow = WM_APP + 5;
    constexpr UINT kUpdateCheckFinished = WM_APP + 6;
    constexpr UINT kUpdateInstallFinished = WM_APP + 7;
    constexpr UINT kTextureReloadKey = WM_APP + 8;
    constexpr int kReloadTexturesMessage = 7;
    constexpr UINT kMenuOpen = 1001;
    constexpr UINT kMenuClean = 1003;
    constexpr UINT kMenuUpdate = 1005;
    constexpr UINT kMenuExit = 1004;
    constexpr int kSettingsMinimize = 2001;
    constexpr int kSettingsDelete = 2002;
    constexpr int kSettingsReload = 2003;
    constexpr int kLogLevelCombo = 2004;
    constexpr int kConcurrencyCombo = 2005;
    constexpr int kLoadCars = 2010;
    constexpr int kLoadHelmets = 2011;
    constexpr int kLoadSuits = 2012;
    constexpr int kLoadNumbers = 2013;
    constexpr int kLoadSpecMaps = 2014;
    constexpr int kStartWithWindows = 2015;

    std::wstring ConfigDirectory()
    {
        wchar_t appData[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
            return L".";
        std::wstring path = appData;
        path += L"\\TPMate";
        CreateDirectoryW(path.c_str(), nullptr);
        return path;
    }

    std::wstring SettingsPath() { return ConfigDirectory() + L"\\settings.ini"; }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (count <= 0) return std::wstring(value.begin(), value.end());
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
        return result;
    }

    const wchar_t* LevelName(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Verbose: return L"VERBOSE";
        case LogLevel::Info: return L"INFO";
        case LogLevel::Warning: return L"WARNING";
        case LogLevel::Error: return L"ERROR";
        }
        return L"INFO";
    }

    int LogLevelValue(LogLevel level) { return static_cast<int>(level); }

    void CleanIRacingPaints(HWND owner, PaintDownloader& downloader)
    {
        const auto result = MessageBoxW(owner,
            L"Move all .tga and .mip files in the iRacing paints folder to the Recycle Bin?",
            L"Clean iRacing Paints Folder", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (result != IDYES) return;

        downloader.DeleteDownloadedPaints();
        wchar_t documents[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents)))
        {
            MessageBoxW(owner, L"The iRacing paints folder could not be found.", L"TPMate", MB_OK | MB_ICONERROR);
            return;
        }
        const std::wstring root = std::wstring(documents) + L"\\iRacing\\paint";
        std::error_code error;
        if (!std::filesystem::exists(root, error) || error)
        {
            MessageBoxW(owner, L"The iRacing paints folder could not be found.", L"TPMate", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::vector<std::wstring> files;
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error))
        {
            if (error) { error.clear(); continue; }
            if (!it->is_regular_file(error) || error) { error.clear(); continue; }
            auto extension = it->path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            if (extension == L".tga" || extension == L".mip") files.push_back(it->path().wstring());
        }
        if (files.empty())
        {
            MessageBoxW(owner, L"No iRacing paint files were found.", L"TPMate", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::wstring fileList;
        for (const auto& file : files) { fileList += file; fileList.push_back(L'\0'); }
        fileList.push_back(L'\0');
        SHFILEOPSTRUCTW operation{};
        operation.hwnd = owner; operation.wFunc = FO_DELETE; operation.pFrom = fileList.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        if (SHFileOperationW(&operation) != 0 || operation.fAnyOperationsAborted)
            MessageBoxW(owner, L"Some paint files could not be moved to the Recycle Bin.", L"TPMate", MB_OK | MB_ICONERROR);
        else
            MessageBoxW(owner, L"The iRacing paints folder has been cleaned.", L"TPMate", MB_OK | MB_ICONINFORMATION);
    }
}

TrayApp::TrayApp()
    : monitor_([this](bool connected) { if (window_) PostMessageW(window_, kConnectionChanged, connected, 0); },
        [this](std::string yaml) { downloader_.OnSessionInfo(std::move(yaml)); },
        [this]() { if (window_) PostMessageW(window_, kSessionExited, 0, 0); },
        [this](LogLevel level, const std::string& message) { PostLog(level, message); }),
      downloader_([this](LogLevel level, const std::string& message) { PostLog(level, message); })
{}

int TrayApp::Run(HINSTANCE instance)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dpi_ = GetDpiForSystem();
    backgroundBrush_ = CreateSolidBrush(RGB(246, 247, 249));
    instance_ = instance;
    startWithWindows_ = StartupRegistration::IsEnabled();
    const auto settingsPath = SettingsPath();
    minimizeToTray_ = GetPrivateProfileIntW(L"Settings", L"MinimizeToTray", 1, settingsPath.c_str()) != 0;
    deleteAfterSession_ = GetPrivateProfileIntW(L"Settings", L"DeleteAfterSession", 1, settingsPath.c_str()) != 0;
    autoRefreshOnReload_ = GetPrivateProfileIntW(L"Settings", L"RefreshOnTextureReload", 1, settingsPath.c_str()) != 0;
    loadCars_ = GetPrivateProfileIntW(L"Settings", L"LoadCars", 1, settingsPath.c_str()) != 0;
    loadHelmets_ = GetPrivateProfileIntW(L"Settings", L"LoadHelmets", 1, settingsPath.c_str()) != 0;
    loadSuits_ = GetPrivateProfileIntW(L"Settings", L"LoadSuits", 1, settingsPath.c_str()) != 0;
    loadNumbers_ = GetPrivateProfileIntW(L"Settings", L"LoadNumbers", 1, settingsPath.c_str()) != 0;
    loadSpecMaps_ = GetPrivateProfileIntW(L"Settings", L"LoadSpecMaps", 1, settingsPath.c_str()) != 0;
    const int savedConcurrency = GetPrivateProfileIntW(L"Settings", L"MaxConcurrentDownloads", 5, settingsPath.c_str());
    maxConcurrentDownloads_ = static_cast<unsigned int>((std::clamp)(savedConcurrency, 1, 10));
    int savedLevel = GetPrivateProfileIntW(L"Settings", L"LogLevel", LogLevelValue(LogLevel::Info), settingsPath.c_str());
    if (savedLevel < LogLevelValue(LogLevel::Verbose) || savedLevel > LogLevelValue(LogLevel::Error))
        savedLevel = LogLevelValue(LogLevel::Info);
    minimumLogLevel_ = static_cast<LogLevel>(savedLevel);
    if (SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS))
        PostLog(LogLevel::Info, "TPMate CPU priority: Low.");
    else
        PostLog(LogLevel::Warning, "Could not set TPMate CPU priority to Low (Windows error " +
            std::to_string(GetLastError()) + ").");
    richEditModule_ = LoadLibraryW(L"Msftedit.dll");
    if (!richEditModule_)
        return 1;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = WindowProc; wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hbrBackground = backgroundBrush_; wc.lpszClassName = L"TPMateNativeWindow";
    if (!RegisterClassExW(&wc)) return 1;

    const std::wstring title = L"TPMate " + UpdateChecker::CurrentVersion();
    RECT initialRect{0, 0, MulDiv(880, dpi_, 96), MulDiv(640, dpi_, 96)};
    AdjustWindowRectExForDpi(&initialRect, kMainWindowStyle, FALSE, 0, dpi_);
    window_ = CreateWindowExW(0, wc.lpszClassName, title.c_str(), kMainWindowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, initialRect.right - initialRect.left, initialRect.bottom - initialRect.top,
        nullptr, nullptr, instance_, this);
    if (!window_) return 1;
    statusText_ = CreateWindowExW(0, L"STATIC", L"Waiting for iRacing...", WS_CHILD | WS_VISIBLE | SS_LEFT,
        12, 10, 770, 28, window_, nullptr, instance_, nullptr);
    updateButton_ = CreateWindowExW(0, L"BUTTON", L"Check for updates", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        630, 8, 154, 28, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kMenuUpdate)), instance_, nullptr);
    paintsGroup_ = CreateWindowExW(0, L"BUTTON", L"Paints", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 100, 100, window_, nullptr, instance_, nullptr);
    behaviorGroup_ = CreateWindowExW(0, L"BUTTON", L"Behavior", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 100, 100, window_, nullptr, instance_, nullptr);
    downloadsGroup_ = CreateWindowExW(0, L"BUTTON", L"Downloads && logging", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 100, 100, window_, nullptr, instance_, nullptr);
    activityLabel_ = CreateWindowExW(0, L"STATIC", L"Activity", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 24, window_, nullptr, instance_, nullptr);
    logEdit_ = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 12, 44, 770, 510, window_, nullptr, instance_, nullptr);
    minimizeCheck_ = CreateWindowExW(0, L"BUTTON", L"Close / minimize to tray", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        12, 562, 360, 24, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsMinimize)), instance_, nullptr);
    deleteCheck_ = CreateWindowExW(0, L"BUTTON", L"Clean downloaded paints on iRacing exit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        390, 562, 390, 24, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsDelete)), instance_, nullptr);
    reloadCheck_ = CreateWindowExW(0, L"BUTTON", L"Re-download paints on Ctrl+R", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        12, 590, 490, 24, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSettingsReload)), instance_, nullptr);
    logLevelLabel_ = CreateWindowExW(0, L"STATIC", L"Log level", WS_CHILD | WS_VISIBLE | SS_LEFT,
        536, 590, 82, 24, window_, nullptr, instance_, nullptr);
    logLevelCombo_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        435, 586, 100, 180, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kLogLevelCombo)), instance_, nullptr);
    concurrencyLabel_ = CreateWindowExW(0, L"STATIC", L"Parallel downloads", WS_CHILD | WS_VISIBLE | SS_LEFT,
        543, 590, 126, 24, window_, nullptr, instance_, nullptr);
    concurrencyCombo_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        677, 586, 106, 180, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kConcurrencyCombo)), instance_, nullptr);
    struct PaintCheck { const wchar_t* label; int id; HWND* handle; bool checked; };
    const PaintCheck paintChecks[] = {
        {L"Cars", kLoadCars, &loadCarsCheck_, loadCars_},
        {L"Numbers", kLoadNumbers, &loadNumbersCheck_, loadNumbers_},
        {L"Spec Maps", kLoadSpecMaps, &loadSpecMapsCheck_, loadSpecMaps_},
        {L"Helmets", kLoadHelmets, &loadHelmetsCheck_, loadHelmets_},
        {L"Suits", kLoadSuits, &loadSuitsCheck_, loadSuits_}
    };
    for (const auto& option : paintChecks)
    {
        *option.handle = CreateWindowExW(0, L"BUTTON", option.label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 100, 24,
            window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(option.id)), instance_, nullptr);
        SendMessageW(*option.handle, BM_SETCHECK, option.checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    EnableWindow(loadNumbersCheck_, loadCars_);
    EnableWindow(loadSpecMapsCheck_, loadCars_);
    startupCheck_ = CreateWindowExW(0, L"BUTTON", L"Start with Windows",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 620, 0, 164, 24,
        window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kStartWithWindows)), instance_, nullptr);
    SendMessageW(startupCheck_, BM_SETCHECK, startWithWindows_ ? BST_CHECKED : BST_UNCHECKED, 0);
    const std::pair<const wchar_t*, LogLevel> levels[] = {
        {L"Error", LogLevel::Error}, {L"Warning", LogLevel::Warning},
        {L"Info", LogLevel::Info}, {L"Verbose", LogLevel::Verbose}
    };
    int selectedLevelIndex = 2;
    for (int i = 0; i < static_cast<int>(std::size(levels)); ++i)
    {
        const LRESULT index = SendMessageW(logLevelCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(levels[i].first));
        SendMessageW(logLevelCombo_, CB_SETITEMDATA, index, LogLevelValue(levels[i].second));
        if (levels[i].second == minimumLogLevel_.load()) selectedLevelIndex = i;
    }
    for (unsigned int count = 1; count <= 10; ++count)
    {
        const auto label = std::to_wstring(count);
        const LRESULT index = SendMessageW(concurrencyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(concurrencyCombo_, CB_SETITEMDATA, index, count);
        if (count == maxConcurrentDownloads_)
            SendMessageW(concurrencyCombo_, CB_SETCURSEL, index, 0);
    }
    SendMessageW(minimizeCheck_, BM_SETCHECK, minimizeToTray_ ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(deleteCheck_, BM_SETCHECK, deleteAfterSession_ ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(reloadCheck_, BM_SETCHECK, autoRefreshOnReload_ ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(logLevelCombo_, CB_SETCURSEL, selectedLevelIndex, 0);
    SendMessageW(logEdit_, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
    SendMessageW(logEdit_, EM_EXLIMITTEXT, 0, 1000000);
    UpdateFonts();
    RECT clientRect{};
    GetClientRect(window_, &clientRect);
    LayoutControls(clientRect.right, clientRect.bottom);

    AddTrayIcon();
    iracingBroadcastMessage_ = RegisterWindowMessageW(L"IRSDK_BROADCASTMSG");
    downloader_.SetAutoRefreshOnReload(autoRefreshOnReload_);
    downloader_.SetMaxConcurrentDownloads(maxConcurrentDownloads_);
    downloader_.SetReloadExcludeWindow(window_);
    downloader_.SetPaintOptions(loadCars_, loadHelmets_, loadSuits_, loadNumbers_, loadSpecMaps_);
    downloader_.Start(deleteAfterSession_);
    monitor_.Start();
    PostMessageW(window_, kAppendLogs, 0, 0);
    ShowWindow(window_, SW_HIDE);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (reloadKeyboardHook_) UnhookWindowsHookEx(reloadKeyboardHook_);
    reloadKeyboardHook_ = nullptr;
    keyboardHookOwner_ = nullptr;
    if (updateThread_.joinable()) updateThread_.join();
    monitor_.Stop();
    downloader_.Stop();
    RemoveTrayIcon();
    if (richEditModule_) FreeLibrary(richEditModule_);
    DeleteObject(uiFont_); DeleteObject(headingFont_); DeleteObject(logFont_); DeleteObject(backgroundBrush_);
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam); self = static_cast<TrayApp*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->window_ = window;
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (iracingBroadcastMessage_ != 0 && message == iracingBroadcastMessage_)
    {
        if (LOWORD(wParam) == kReloadTexturesMessage && autoRefreshOnReload_)
        {
            PostLog(LogLevel::Info, "Detected an iRacing SDK texture reload request.");
            downloader_.OnIRacingTextureReload();
        }
        return 0;
    }
    switch (message)
    {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_KILLFOCUS && reinterpret_cast<HWND>(lParam) == logEdit_)
        {
            SendMessageW(logEdit_, EM_SETSEL, static_cast<WPARAM>(-1), 0);
            return 0;
        }
        switch (LOWORD(wParam))
        {
        case kStartWithWindows:
        {
            const bool requested = SendMessageW(startupCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (StartupRegistration::Apply(window_, requested))
            {
                startWithWindows_ = requested;
                PostLog(LogLevel::Info, requested ? "Windows startup enabled." : "Windows startup disabled.");
            }
            else
            {
                const bool cancelled = GetLastError() == ERROR_CANCELLED;
                SendMessageW(startupCheck_, BM_SETCHECK, startWithWindows_ ? BST_CHECKED : BST_UNCHECKED, 0);
                if (!cancelled)
                    MessageBoxW(window_, L"Windows startup could not be changed. The previous setting was kept.",
                        L"TPMate", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        case kLoadCars:
        case kLoadHelmets:
        case kLoadSuits:
        case kLoadNumbers:
        case kLoadSpecMaps:
            ApplyPaintOptions();
            return 0;
        case kMenuOpen: ShowStatusWindow(); return 0;
        case kMenuUpdate: StartUpdateCheck(); return 0;
        case kMenuClean: CleanIRacingPaints(window_, downloader_); return 0;
        case kMenuExit: DestroyWindow(window_); return 0;
        case kSettingsMinimize: minimizeToTray_ = SendMessageW(minimizeCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED; SaveSettings(); return 0;
        case kSettingsDelete:
            deleteAfterSession_ = SendMessageW(deleteCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            // Applied to simulator exits without restarting the worker.
            downloader_.SetDeleteAfterExit(deleteAfterSession_);
            return 0;
        case kSettingsReload:
            autoRefreshOnReload_ = SendMessageW(reloadCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            downloader_.SetAutoRefreshOnReload(autoRefreshOnReload_);
            UpdateReloadKeyboardHook();
            return 0;
        }
        if (LOWORD(wParam) == kLogLevelCombo && HIWORD(wParam) == CBN_SELCHANGE)
        {
            const LRESULT selected = SendMessageW(logLevelCombo_, CB_GETCURSEL, 0, 0);
            const LRESULT value = selected == CB_ERR ? LogLevelValue(LogLevel::Info) :
                SendMessageW(logLevelCombo_, CB_GETITEMDATA, selected, 0);
            minimumLogLevel_ = static_cast<LogLevel>(value);
            SaveSettings();
            return 0;
        }
        if (LOWORD(wParam) == kConcurrencyCombo && HIWORD(wParam) == CBN_SELCHANGE)
        {
            const LRESULT selected = SendMessageW(concurrencyCombo_, CB_GETCURSEL, 0, 0);
            const LRESULT value = selected == CB_ERR ? 5 : SendMessageW(concurrencyCombo_, CB_GETITEMDATA, selected, 0);
            maxConcurrentDownloads_ = static_cast<unsigned int>((std::clamp)(static_cast<int>(value), 1, 10));
            SaveSettings();
            downloader_.SetMaxConcurrentDownloads(maxConcurrentDownloads_);
            return 0;
        }
        break;
    case kTrayMessage:
    {
        const UINT event = LOWORD(lParam);
        if (lParam == WM_RBUTTONUP || event == WM_RBUTTONUP || event == WM_CONTEXTMENU) ShowMenu();
        else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK || event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) ShowStatusWindow();
        return 0;
    }
    case kShowExistingWindow: ShowStatusWindow(); return 0;
    case kConnectionChanged: UpdateConnection(wParam != 0); return 0;
    case kAppendLogs: AppendQueuedLogs(); return 0;
    case kUpdateCheckFinished: FinishUpdateCheck(); return 0;
    case kUpdateInstallFinished: FinishUpdateInstall(); return 0;
    case kTextureReloadKey:
        if (connected_ && autoRefreshOnReload_)
        {
            PostLog(LogLevel::Info, "Detected Ctrl+R in iRacing; requesting fresh session paints.");
            downloader_.OnIRacingTextureReload();
        }
        return 0;
    case kSessionExited:
        downloader_.OnSimulatorExit();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && minimizeToTray_) ShowWindow(window_, SW_HIDE);
        else if (wParam != SIZE_MINIMIZED) LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO:
    {
        RECT minimum{0, 0, MulDiv(880, dpi_, 96), MulDiv(640, dpi_, 96)};
        AdjustWindowRectExForDpi(&minimum, kMainWindowStyle, FALSE, 0, dpi_);
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        info->ptMaxTrackSize = info->ptMinTrackSize;
        return 0;
    }
    case WM_DPICHANGED:
    {
        dpi_ = HIWORD(wParam);
        UpdateFonts();
        const auto* rect = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(40, 47, 58));
        return reinterpret_cast<LRESULT>(backgroundBrush_);
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_CLOSE:
        if (minimizeToTray_) { ShowWindow(window_, SW_HIDE); return 0; }
        DestroyWindow(window_); return 0;
    case WM_DESTROY:
        if (reloadKeyboardHook_) UnhookWindowsHookEx(reloadKeyboardHook_);
        reloadKeyboardHook_ = nullptr;
        keyboardHookOwner_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void TrayApp::AddTrayIcon()
{
    iconData_ = {}; iconData_.cbSize = sizeof(iconData_); iconData_.hWnd = window_; iconData_.uID = 1;
    iconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; iconData_.uCallbackMessage = kTrayMessage;
    iconData_.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wcscpy_s(iconData_.szTip, L"TPMate - Waiting for iRacing");
    Shell_NotifyIconW(NIM_ADD, &iconData_); iconData_.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &iconData_);
}
void TrayApp::RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &iconData_); }

void TrayApp::ShowMenu()
{
    HMENU menu = CreatePopupMenu(); if (!menu) return;
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open");
    AppendMenuW(menu, MF_STRING, kMenuClean, L"Clean iRacing Paints Folder...");
    AppendMenuW(menu, MF_STRING | (updateBusy_ ? MF_GRAYED : 0), kMenuUpdate, L"Check for updates...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, 0, window_, nullptr));
    PostMessageW(window_, WM_NULL, 0, 0); DestroyMenu(menu);
    if (command) PostMessageW(window_, WM_COMMAND, command, 0);
}

void TrayApp::ShowStatusWindow()
{
    ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_);
    SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
    SendMessageW(logEdit_, WM_VSCROLL, SB_BOTTOM, 0);
}

void TrayApp::StartUpdateCheck()
{
    if (updateBusy_) return;
    if (updateThread_.joinable()) updateThread_.join();
    updateBusy_ = true;
    EnableWindow(updateButton_, FALSE);
    SetWindowTextW(updateButton_, L"Checking...");
    PostLog(LogLevel::Info, "Running manual GitHub release check for TPMate updates.");
    const HWND owner = window_;
    updateThread_ = std::thread([this, owner]()
    {
        try { updateResult_ = UpdateChecker::CheckForUpdate(); }
        catch (...) { updateResult_ = {}; updateResult_.message = L"The update check failed unexpectedly."; }
        PostMessageW(owner, kUpdateCheckFinished, 0, 0);
    });
}

void TrayApp::FinishUpdateCheck()
{
    if (updateThread_.joinable()) updateThread_.join();
    updateBusy_ = false;
    EnableWindow(updateButton_, TRUE);
    SetWindowTextW(updateButton_, L"Check for updates");
    const auto& result = updateResult_;
    if (result.state == UpdateCheckState::Failed)
    {
        const std::wstring message = L"Current version: " + UpdateChecker::CurrentVersion() + L"\n\n" + result.message;
        MessageBoxW(window_, message.c_str(), L"TPMate Update", MB_OK | MB_ICONWARNING);
        return;
    }
    const bool available = result.state == UpdateCheckState::UpdateAvailable;
    const std::wstring details = L"Current version: " + UpdateChecker::CurrentVersion() +
        L"\nGitHub version: " + (result.release.versionDisplay.empty() ? L"No published release" : result.release.versionDisplay) +
        (available ? L"\n\nA newer version is available." : L"\n\n" + result.message);
    TASKDIALOG_BUTTON buttons[2]{};
    unsigned count = 0;
    const bool canInstall = available && !result.release.assetDownloadUrl.empty();
    if (canInstall) buttons[count++] = {101, L"Install update\nDownload and restart TPMate"};
    if (!result.release.releasePageUrl.empty()) buttons[count++] = {102, L"Open GitHub release"};
    TASKDIALOGCONFIG dialog{sizeof(dialog)};
    dialog.hwndParent = window_;
    dialog.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    dialog.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    dialog.pszWindowTitle = L"TPMate Update";
    dialog.pszMainInstruction = available ? L"Update available" : L"No update available";
    dialog.pszContent = details.c_str();
    dialog.cButtons = count;
    dialog.pButtons = buttons;
    dialog.nDefaultButton = IDCLOSE;
    int selected = IDCLOSE;
    if (FAILED(TaskDialogIndirect(&dialog, &selected, nullptr, nullptr)))
    {
        MessageBoxW(window_, details.c_str(), L"TPMate Update", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (selected == 101) StartUpdateInstall(result.release);
    else if (selected == 102 && !UpdateChecker::OpenReleasePage(result.release.releasePageUrl))
        MessageBoxW(window_, L"Could not open the GitHub release page.", L"TPMate Update", MB_OK | MB_ICONWARNING);
}

void TrayApp::StartUpdateInstall(UpdateReleaseInfo release)
{
    if (updateBusy_) return;
    updateBusy_ = true;
    EnableWindow(updateButton_, FALSE);
    SetWindowTextW(updateButton_, L"Downloading...");
    PostLog(LogLevel::Info, "Downloading TPMate update. The application will restart after installation.");
    const HWND owner = window_;
    updateThread_ = std::thread([this, owner, release = std::move(release)]()
    {
        updateError_.clear();
        try
        {
            if (!UpdateChecker::DownloadReleaseAsset(release, downloadedUpdate_, updateError_) && updateError_.empty())
                updateError_ = L"Could not download the update.";
        }
        catch (...) { updateError_ = L"Could not prepare the downloaded update."; }
        PostMessageW(owner, kUpdateInstallFinished, 0, 0);
    });
}

void TrayApp::FinishUpdateInstall()
{
    if (updateThread_.joinable()) updateThread_.join();
    updateBusy_ = false;
    EnableWindow(updateButton_, TRUE);
    SetWindowTextW(updateButton_, L"Check for updates");
    if (updateError_.empty() && UpdateChecker::LaunchSelfUpdater(downloadedUpdate_, GetCurrentProcessId(), updateError_))
    {
        SaveSettings();
        DestroyWindow(window_);
        return;
    }
    MessageBoxW(window_, updateError_.c_str(), L"TPMate Update", MB_OK | MB_ICONERROR);
}

void TrayApp::SaveSettings()
{
    const auto path = SettingsPath();
    WritePrivateProfileStringW(L"Settings", L"MinimizeToTray", minimizeToTray_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"DeleteAfterSession", deleteAfterSession_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"RefreshOnTextureReload", autoRefreshOnReload_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"LoadCars", loadCars_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"LoadHelmets", loadHelmets_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"LoadSuits", loadSuits_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"LoadNumbers", loadNumbers_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"LoadSpecMaps", loadSpecMaps_ ? L"1" : L"0", path.c_str());
    const auto concurrency = std::to_wstring(maxConcurrentDownloads_);
    WritePrivateProfileStringW(L"Settings", L"MaxConcurrentDownloads", concurrency.c_str(), path.c_str());
    const auto logLevel = std::to_wstring(LogLevelValue(minimumLogLevel_.load()));
    WritePrivateProfileStringW(L"Settings", L"LogLevel", logLevel.c_str(), path.c_str());
}

void TrayApp::ApplyPaintOptions()
{
    loadCars_ = SendMessageW(loadCarsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    loadHelmets_ = SendMessageW(loadHelmetsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    loadSuits_ = SendMessageW(loadSuitsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    loadNumbers_ = SendMessageW(loadNumbersCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    loadSpecMaps_ = SendMessageW(loadSpecMapsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(loadNumbersCheck_, loadCars_);
    EnableWindow(loadSpecMapsCheck_, loadCars_);
    SaveSettings();
    downloader_.SetPaintOptions(loadCars_, loadHelmets_, loadSuits_, loadNumbers_, loadSpecMaps_);
}

void TrayApp::PostLog(LogLevel level, const std::string& message)
{
    if (LogLevelValue(level) < LogLevelValue(minimumLogLevel_.load()))
        return;
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t timestamp[32]{};
    swprintf_s(timestamp, L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    std::wstring line = L"[" + std::wstring(timestamp) + L"] [" + LevelName(level) + L"] " + Utf8ToWide(message) + L"\r\n";
    {
        std::lock_guard lock(logMutex_);
        const bool queueWasEmpty = pendingLogs_.empty();
        pendingLogs_.push_back(std::move(line));
        if (queueWasEmpty && window_) PostMessageW(window_, kAppendLogs, 0, 0);
    }
}

void TrayApp::AppendQueuedLogs()
{
    std::vector<std::wstring> logs;
    { std::lock_guard lock(logMutex_); logs.swap(pendingLogs_); }
    if (!logEdit_) return;
    if (logs.empty()) return;
    std::wstring combined;
    size_t totalLength = 0;
    for (const auto& line : logs) totalLength += line.size();
    combined.reserve(totalLength);
    for (const auto& line : logs)
    {
        combined += line;
    }
    if (GetWindowTextLengthW(logEdit_) > 900000)
    {
        CHARRANGE trim{0, 150000};
        SendMessageW(logEdit_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&trim));
        SendMessageW(logEdit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    }
    const LONG end = GetWindowTextLengthW(logEdit_);
    CHARRANGE append{end, end};
    SendMessageW(logEdit_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&append));
    SendMessageW(logEdit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(combined.c_str()));
    const LONG newEnd = GetWindowTextLengthW(logEdit_);
    SendMessageW(logEdit_, EM_SETSEL, newEnd, newEnd);
    if (IsWindowVisible(window_))
    {
        SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
        SendMessageW(logEdit_, WM_VSCROLL, SB_BOTTOM, 0);
        // Let Windows combine paints instead of repainting synchronously for every log batch.
        InvalidateRect(logEdit_, nullptr, FALSE);
    }
}

void TrayApp::LayoutControls(int width, int height)
{
    const auto px = [this](int value) { return MulDiv(value, dpi_, 96); };
    const int logicalWidth = MulDiv(width, 96, dpi_);
    const int logicalHeight = MulDiv(height, 96, dpi_);
    const int content = logicalWidth - 40;
    const int settingsY = logicalHeight - 196;
    const int paintsWidth = (content - 24) * 28 / 100;
    const int behaviorWidth = (content - 24) * 42 / 100;
    const int behaviorX = 20 + paintsWidth + 12;
    const int downloadsX = behaviorX + behaviorWidth + 12;
    const int downloadsWidth = logicalWidth - 20 - downloadsX;
    const auto place = [&](HWND control, int x, int y, int w, int h)
    { if (control) MoveWindow(control, px(x), px(y), px(w), px(h), TRUE); };

    place(statusText_, 20, 26, content - 180, 24);
    place(updateButton_, logicalWidth - 180, 18, 160, 32);
    place(activityLabel_, 20, 66, content, 22);
    place(logEdit_, 20, 92, content, settingsY - 108);
    if (logEdit_)
    {
        RECT textRect{};
        GetClientRect(logEdit_, &textRect);
        textRect.left += px(10); textRect.top += px(8);
        textRect.right -= px(10); textRect.bottom -= px(8);
        SendMessageW(logEdit_, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&textRect));
    }
    place(paintsGroup_, 20, settingsY, paintsWidth, 176);
    place(behaviorGroup_, behaviorX, settingsY, behaviorWidth, 176);
    place(downloadsGroup_, downloadsX, settingsY, downloadsWidth, 176);
    const int paintX = 36;
    place(loadCarsCheck_, paintX, settingsY + 30, paintsWidth - 32, 24);
    place(loadNumbersCheck_, paintX + 16, settingsY + 56, paintsWidth - 48, 24);
    place(loadSpecMapsCheck_, paintX + 16, settingsY + 82, paintsWidth - 48, 24);
    place(loadHelmetsCheck_, paintX, settingsY + 112, paintsWidth - 32, 24);
    place(loadSuitsCheck_, paintX, settingsY + 140, paintsWidth - 32, 24);
    place(minimizeCheck_, behaviorX + 16, settingsY + 30, behaviorWidth - 32, 24);
    place(startupCheck_, behaviorX + 16, settingsY + 60, behaviorWidth - 32, 24);
    place(deleteCheck_, behaviorX + 16, settingsY + 90, behaviorWidth - 32, 24);
    place(reloadCheck_, behaviorX + 16, settingsY + 120, behaviorWidth - 32, 24);
    place(logLevelLabel_, downloadsX + 16, settingsY + 28, downloadsWidth - 32, 20);
    place(logLevelCombo_, downloadsX + 16, settingsY + 50, downloadsWidth - 32, 180);
    place(concurrencyLabel_, downloadsX + 16, settingsY + 96, downloadsWidth - 32, 20);
    place(concurrencyCombo_, downloadsX + 16, settingsY + 118, downloadsWidth - 32, 240);
}

void TrayApp::UpdateFonts()
{
    const HFONT previousUi = uiFont_, previousHeading = headingFont_, previousLog = logFont_;
    const auto create = [this](const wchar_t* name, int points, int weight)
    { return CreateFontW(-MulDiv(points, dpi_, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, name); };
    uiFont_ = create(L"Segoe UI", 9, FW_NORMAL);
    headingFont_ = create(L"Segoe UI", 10, FW_SEMIBOLD);
    logFont_ = create(L"Consolas", 9, FW_NORMAL);
    for (HWND control : {updateButton_, minimizeCheck_, deleteCheck_, reloadCheck_, loadCarsCheck_, loadHelmetsCheck_,
        loadSuitsCheck_, loadNumbersCheck_, loadSpecMapsCheck_, startupCheck_, logLevelLabel_, logLevelCombo_,
        concurrencyLabel_, concurrencyCombo_})
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    for (HWND control : {statusText_, activityLabel_, paintsGroup_, behaviorGroup_, downloadsGroup_})
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), TRUE);
    if (logEdit_) SendMessageW(logEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(logFont_), TRUE);
    for (HWND control : {logLevelCombo_, concurrencyCombo_})
    {
        if (!control) continue;
        SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), MulDiv(22, dpi_, 96));
        SendMessageW(control, CB_SETITEMHEIGHT, 0, MulDiv(22, dpi_, 96));
    }
    DeleteObject(previousUi); DeleteObject(previousHeading); DeleteObject(previousLog);
}

void TrayApp::UpdateConnection(bool connected)
{
    connected_ = connected;
    UpdateReloadKeyboardHook();
    SetWindowTextW(statusText_, connected_ ? L"Connected to iRacing." : L"Waiting for iRacing...");
    wcscpy_s(iconData_.szTip, connected_ ? L"TPMate - Connected to iRacing" : L"TPMate - Waiting for iRacing");
    iconData_.uFlags = NIF_TIP | NIF_SHOWTIP; Shell_NotifyIconW(NIM_MODIFY, &iconData_);
}

void TrayApp::UpdateReloadKeyboardHook()
{
    const bool wanted = connected_ && autoRefreshOnReload_;
    if (wanted && !reloadKeyboardHook_)
    {
        reloadShortcut_.Reset();
        keyboardHookOwner_ = this;
        reloadKeyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance_, 0);
        if (!reloadKeyboardHook_)
        {
            keyboardHookOwner_ = nullptr;
            PostLog(LogLevel::Warning, "Could not enable Ctrl+R detection (Windows error " + std::to_string(GetLastError()) + ").");
        }
    }
    else if (!wanted && reloadKeyboardHook_)
    {
        UnhookWindowsHookEx(reloadKeyboardHook_);
        reloadKeyboardHook_ = nullptr;
        keyboardHookOwner_ = nullptr;
        reloadShortcut_.Reset();
    }
}

LRESULT CALLBACK TrayApp::KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    auto* self = keyboardHookOwner_;
    if (code == HC_ACTION && self)
    {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (key->vkCode == 'R' && self->reloadShortcut_.OnKey(key->vkCode, wParam,
            (key->flags & LLKHF_INJECTED) != 0,
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
            (GetAsyncKeyState(VK_MENU) & 0x8000) != 0,
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0))
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &processId);
            HANDLE process = processId ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId) : nullptr;
            if (process)
            {
                wchar_t path[32768]{};
                DWORD length = static_cast<DWORD>(std::size(path));
                if (QueryFullProcessImageNameW(process, 0, path, &length))
                {
                    const wchar_t* name = wcsrchr(path, L'\\');
                    name = name ? name + 1 : path;
                    if (ReloadShortcut::IsSimulator(name))
                        PostMessageW(self->window_, kTextureReloadKey, 0, 0);
                }
                CloseHandle(process);
            }
        }
    }
    // Observe the shortcut; never consume it or change iRacing's key handling.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
