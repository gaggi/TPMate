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
    constexpr DWORD kMainWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    constexpr UINT kTrayMessage = WM_APP + 1;
    constexpr UINT kConnectionChanged = WM_APP + 2;
    constexpr UINT kAppendLogs = WM_APP + 3;
    constexpr UINT kSessionExited = WM_APP + 4;
    constexpr UINT kShowExistingWindow = WM_APP + 5;
    constexpr UINT kUpdateCheckFinished = WM_APP + 6;
    constexpr UINT kUpdateInstallFinished = WM_APP + 7;
    constexpr UINT kProgressChanged = WM_APP + 8;
    constexpr int kReloadTexturesMessage = 7;
    constexpr UINT kMenuOpen = 1001;
    constexpr UINT kMenuClean = 1003;
    constexpr UINT kMenuUpdate = 1005;
    constexpr UINT kMenuExit = 1004;
    constexpr UINT kMenuRefresh = 1006;
    constexpr UINT kMenuOpenFolder = 1007;
    constexpr int kNavPaints = 3001;
    constexpr int kNavActivity = 3002;
    constexpr int kNavSettings = 3003;
    constexpr int kBannerButton = 3010;
    constexpr int kActivityRows = 3020;

    // LaunchMate layout, in DIPs.
    constexpr int kNavWidth = 184;
    constexpr int kMargin = 24;
    constexpr int kPageTop = 92;
    constexpr int kMinimumWidth = 900;
    constexpr int kMinimumHeight = 600;

    // iRacing session strings are not always valid UTF-8; fall back to Windows-1252 for names with umlauts.
    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        int count = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (count <= 0)
        {
            codePage = 1252;
            flags = 0;
            count = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
        }
        if (count <= 0) return {};
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), count);
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

    // Log levels in the order the "Choose..." menu lists them.
    constexpr LogLevel kLogLevels[] = {LogLevel::Error, LogLevel::Warning, LogLevel::Info, LogLevel::Verbose};
    constexpr const wchar_t* kLogLevelNames[] = {L"Error", L"Warning", L"Info", L"Verbose"};

    const wchar_t* LogLevelLabel(LogLevel level)
    {
        for (size_t index = 0; index < std::size(kLogLevels); ++index)
            if (kLogLevels[index] == level) return kLogLevelNames[index];
        return L"Info";
    }

    // Returns an empty string when the folder does not exist.
    std::wstring PaintFolder()
    {
        wchar_t documents[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents)))
            return {};
        const std::wstring root = std::wstring(documents) + L"\\iRacing\\paint";
        std::error_code error;
        return std::filesystem::is_directory(root, error) && !error ? root : std::wstring{};
    }

    void OpenPaintFolder(HWND owner)
    {
        const auto root = PaintFolder();
        if (root.empty() || reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            MessageBoxW(owner, L"The iRacing paints folder could not be found.", L"TPMate", MB_OK | MB_ICONINFORMATION);
    }

    void CleanIRacingPaints(HWND owner, PaintDownloader& downloader)
    {
        const auto result = MessageBoxW(owner,
            L"Move all .tga and .mip files in the iRacing paints folder to the Recycle Bin?",
            L"Clean iRacing Paints Folder", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (result != IDYES) return;

        downloader.DeleteDownloadedPaints();
        const auto root = PaintFolder();
        if (root.empty())
        {
            MessageBoxW(owner, L"The iRacing paints folder could not be found.", L"TPMate", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::error_code error;
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

    // Shows a pick list at the cursor; returns the chosen index or -1 (as PageWindow::ChooseFromMenu).
    int ChooseFromMenu(HWND owner, const wchar_t* const* items, size_t count, int checked)
    {
        const HMENU menu = CreatePopupMenu();
        for (size_t index = 0; index < count; ++index)
            AppendMenuW(menu, MF_STRING | (static_cast<int>(index) == checked ? MF_CHECKED : 0), index + 1, items[index]);
        POINT cursor{};
        GetCursorPos(&cursor);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, owner, nullptr);
        DestroyMenu(menu);
        return static_cast<int>(choice) - 1;
    }
}

TrayApp::TrayApp()
    : monitor_([this](bool connected) { if (window_) PostMessageW(window_, kConnectionChanged, connected, 0); },
        [this](std::optional<std::string> yaml, PresentCars cars) { downloader_.OnSessionInfo(std::move(yaml), std::move(cars)); },
        [this]() { if (window_) PostMessageW(window_, kSessionExited, 0, 0); },
        [this](LogLevel level, const std::string& message) { PostLog(level, message); }),
      downloader_([this](LogLevel level, const std::string& message) { PostLog(level, message); },
        [this](const PaintProgress& progress)
        {
            { std::lock_guard lock(progressMutex_); progress_ = progress; }
            // Coalesce bursts of progress updates into one repaint.
            if (!progressPosted_.exchange(true) && window_) PostMessageW(window_, kProgressChanged, 0, 0);
        })
{}

int TrayApp::Run(HINSTANCE instance)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dpi_ = GetDpiForSystem();
    instance_ = instance;
    startWithWindows_ = StartupRegistration::IsEnabled();
    settings_ = AppSettings::Load();
    minimumLogLevel_ = settings_.logLevel;
    // EcoQoS ("efficiency mode") keeps TPMate on efficiency cores and low clocks where supported,
    // leaving performance cores to iRacing. Older Windows versions simply reject it.
    PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED, PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
    const bool efficiencyMode = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
        &throttling, sizeof(throttling)) != FALSE;
    if (SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS))
        PostLog(LogLevel::Info, efficiencyMode ? "TPMate CPU priority: Low (efficiency mode)." : "TPMate CPU priority: Low.");
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
    wc.hbrBackground = UiTheme::BackgroundBrush(); wc.lpszClassName = L"TPMateNativeWindow";
    if (!RegisterClassExW(&wc)) return 1;

    CreateFonts();
    const std::wstring title = L"TPMate " + UpdateChecker::CurrentVersion();
    RECT initialRect{0, 0, MulDiv(1000, dpi_, 96), MulDiv(680, dpi_, 96)};
    AdjustWindowRectExForDpi(&initialRect, kMainWindowStyle, FALSE, 0, dpi_);
    window_ = CreateWindowExW(0, wc.lpszClassName, title.c_str(), kMainWindowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, initialRect.right - initialRect.left, initialRect.bottom - initialRect.top,
        nullptr, nullptr, instance_, this);
    if (!window_) return 1;
    CreateControls();
    UiTheme::Apply(window_);
    RestoreWindowPlacement();
    RECT clientRect{};
    GetClientRect(window_, &clientRect);
    LayoutControls(clientRect.right, clientRect.bottom);
    ShowPage(page_);

    AddTrayIcon();
    UpdateStatusDisplay();
    iracingBroadcastMessage_ = RegisterWindowMessageW(L"IRSDK_BROADCASTMSG");
    downloader_.SetAutoRefreshOnReload(settings_.refreshOnTextureReload);
    downloader_.SetOnlyPresentDrivers(settings_.onlyPresentDrivers);
    downloader_.SetMaxConcurrentDownloads(settings_.maxConcurrentDownloads);
    downloader_.SetReloadExcludeWindow(window_);
    downloader_.SetPaintOptions(settings_.loadCars, settings_.loadHelmets, settings_.loadSuits,
        settings_.loadNumbers, settings_.loadSpecMaps);
    downloader_.Start(settings_.deleteAfterSession);
    monitor_.Start();
    PostMessageW(window_, kAppendLogs, 0, 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (IsDialogMessageW(window_, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (updateThread_.joinable()) updateThread_.join();
    monitor_.Stop();
    downloader_.Stop();
    RemoveTrayIcon();
    if (richEditModule_) FreeLibrary(richEditModule_);
    DeleteObject(uiFont_); DeleteObject(headingFont_); DeleteObject(logFont_);
    return static_cast<int>(message.wParam);
}

void TrayApp::CreateFonts()
{
    const HFONT previousUi = uiFont_, previousHeading = headingFont_, previousLog = logFont_;
    const auto create = [this](const wchar_t* name, int points, int weight)
    { return CreateFontW(-MulDiv(points, dpi_, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, name); };
    uiFont_ = create(L"Segoe UI", 9, FW_NORMAL);
    headingFont_ = create(L"Segoe UI", 10, FW_SEMIBOLD);
    logFont_ = create(L"Consolas", 9, FW_NORMAL);
    if (window_)
    {
        EnumChildWindows(window_, [](HWND control, LPARAM font) -> BOOL
        {
            SendMessageW(control, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(uiFont_));
        for (HWND control : {pageTitle_})
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), TRUE);
        if (logEdit_) SendMessageW(logEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(logFont_), TRUE);
        navBar_.SetFonts(headingFont_, uiFont_);
        banner_.SetFonts(headingFont_, uiFont_);
        activityRows_.SetFonts(headingFont_, uiFont_);
    }
    if (previousUi) DeleteObject(previousUi);
    if (previousHeading) DeleteObject(previousHeading);
    if (previousLog) DeleteObject(previousLog);
}

void TrayApp::CreateControls()
{
    const auto control = [this](const wchar_t* className, const wchar_t* text, DWORD style, int id = 0)
    {
        return CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, window_,
            id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, instance_, nullptr);
    };
    navBar_.Create(instance_, window_, L"TPMate", L"Version " + UpdateChecker::CurrentVersion(), {
        {L'\uE771', L"Paints", kNavPaints, true},
        {L'\uE81C', L"Activity", kNavActivity, true},
        {L'\uE713', L"Settings", kNavSettings, true}}, headingFont_, uiFont_);
    banner_.Create(instance_, window_, kBannerButton, headingFont_, uiFont_);
    pageTitle_ = control(L"STATIC", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS);
    pageHint_ = control(L"STATIC", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS);
    pageHost_.Create(instance_, window_);

    // Activity page
    activityRows_.Create(instance_, window_, kActivityRows, headingFont_, uiFont_);
    logEdit_ = control(L"RICHEDIT50W", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY);
    SendMessageW(logEdit_, EM_SETBKGNDCOLOR, 0, UiTheme::Surface);
    SendMessageW(logEdit_, EM_EXLIMITTEXT, 0, 1000000);
    RefreshActivityRows();

    CreateFonts();
}

void TrayApp::ShowPage(Page page)
{
    pageHost_.Clear();
    page_ = page;
    const bool activity = page == Page::Activity;
    for (HWND control : {activityRows_.Handle(), logEdit_})
        ShowWindow(control, activity ? SW_SHOW : SW_HIDE);
    ShowWindow(pageHost_.Handle(), activity ? SW_HIDE : SW_SHOW);
    struct Heading { int nav; const wchar_t* title; const wchar_t* hint; };
    const Heading heading =
        page == Page::Activity ? Heading{kNavActivity, L"Activity", L"What TPMate did since it started."} :
        page == Page::Settings ? Heading{kNavSettings, L"Settings", L"How TPMate starts and stays up to date."} :
        Heading{kNavPaints, L"Paints", L"What TPMate downloads, for whom and when."};
    navBar_.SetSelected(heading.nav);
    SetWindowTextW(pageTitle_, heading.title);
    SetWindowTextW(pageHint_, heading.hint);
    if (page == Page::Paints) pageHost_.SetContent(CreatePaintsPage(MakePageContext(), pageHost_.Handle()));
    if (page == Page::Settings) pageHost_.SetContent(CreateSettingsPage(MakePageContext(), pageHost_.Handle()));
    if (activity)
    {
        SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
        SendMessageW(logEdit_, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

PageContext TrayApp::MakePageContext()
{
    PageContext context;
    context.instance = instance_;
    context.headingFont = headingFont_;
    context.textFont = uiFont_;
    context.settings = &settings_;
    context.settingsChanged = [this]() { ApplySettings(); };
    context.startWithWindows = &startWithWindows_;
    context.setStartWithWindows = [this](bool enabled) { return ApplyStartWithWindows(enabled); };
    context.update = &update_;
    context.checkForUpdates = [this]() { StartUpdateCheck(); };
    context.installUpdate = [this]() { StartUpdateInstall(); };
    return context;
}

void TrayApp::RefreshPage()
{
    if (const HWND page = pageHost_.Content()) SendMessageW(page, WM_TIMER, kPageRefreshTimer, 0);
}

std::wstring TrayApp::ApplyStartWithWindows(bool enabled)
{
    if (StartupRegistration::Apply(window_, enabled))
    {
        startWithWindows_ = enabled;
        PostLog(LogLevel::Info, enabled ? "Windows startup enabled." : "Windows startup disabled.");
        return {};
    }
    return GetLastError() == ERROR_CANCELLED ? L"Not changed: Windows approval was declined." :
        L"Not changed: the logon task could not be updated.";
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
        if (LOWORD(wParam) == kReloadTexturesMessage && settings_.refreshOnTextureReload)
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
        case kNavPaints: ShowPage(Page::Paints); return 0;
        case kNavActivity: ShowPage(Page::Activity); return 0;
        case kNavSettings: ShowPage(Page::Settings); return 0;
        case kBannerButton: RequestPaintRefresh(); return 0;
        case kActivityRows:
            if (HIWORD(wParam) == RowList::kButton || HIWORD(wParam) == RowList::kActivated) ChooseLogLevel();
            return 0;
        case kMenuOpen: ShowStatusWindow(); return 0;
        case kMenuRefresh: RequestPaintRefresh(); return 0;
        case kMenuOpenFolder: OpenPaintFolder(window_); return 0;
        case kMenuUpdate:
            ShowStatusWindow();
            ShowPage(Page::Settings);
            StartUpdateCheck();
            return 0;
        case kMenuClean: CleanIRacingPaints(window_, downloader_); return 0;
        case kMenuExit:
            CaptureWindowPlacement();
            SaveSettings();
            DestroyWindow(window_);
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
    case kProgressChanged:
        progressPosted_ = false;
        UpdateStatusDisplay();
        return 0;
    case kUpdateCheckFinished: FinishUpdateCheck(); return 0;
    case kUpdateInstallFinished: FinishUpdateInstall(); return 0;
    case WM_INPUT:
        OnRawKeyboardInput(reinterpret_cast<HRAWINPUT>(lParam));
        break; // DefWindowProc releases the raw input buffer.
    case kSessionExited:
        downloader_.OnSimulatorExit();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && settings_.minimizeToTray)
        {
            CaptureWindowPlacement();
            ShowWindow(window_, SW_HIDE);
        }
        else if (wParam != SIZE_MINIMIZED) LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO:
    {
        RECT minimum{0, 0, MulDiv(kMinimumWidth, dpi_, 96), MulDiv(kMinimumHeight, dpi_, 96)};
        AdjustWindowRectExForDpi(&minimum, kMainWindowStyle, FALSE, 0, dpi_);
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        return 0;
    }
    case WM_DPICHANGED:
    {
        dpi_ = HIWORD(wParam);
        CreateFonts();
        ShowPage(page_); // Pages keep the fonts they were created with.
        const auto* rect = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        CaptureWindowPlacement();
        SaveSettings();
        if (settings_.minimizeToTray) { ShowWindow(window_, SW_HIDE); return 0; }
        DestroyWindow(window_); return 0;
    case WM_DESTROY:
        connected_ = false;
        UpdateReloadShortcutListener();
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
    AppendMenuW(menu, MF_STRING | (connected_ ? 0 : MF_GRAYED), kMenuRefresh, L"Refresh paints");
    AppendMenuW(menu, MF_STRING, kMenuOpenFolder, L"Open paint folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClean, L"Clean iRacing Paints Folder...");
    const bool updateBusy = update_.phase == UpdateState::Phase::Checking || update_.phase == UpdateState::Phase::Downloading;
    AppendMenuW(menu, MF_STRING | (updateBusy ? MF_GRAYED : 0), kMenuUpdate, L"Check for updates...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, 0, window_, nullptr));
    PostMessageW(window_, WM_NULL, 0, 0); DestroyMenu(menu);
    if (command) PostMessageW(window_, WM_COMMAND, command, 0);
}

void TrayApp::ShowStatusWindow()
{
    if (IsIconic(window_)) ShowWindow(window_, SW_RESTORE);
    else if (!IsWindowVisible(window_)) ShowWindow(window_, settings_.windowMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    SetForegroundWindow(window_);
    SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
    SendMessageW(logEdit_, WM_VSCROLL, SB_BOTTOM, 0);
}

void TrayApp::CaptureWindowPlacement()
{
    WINDOWPLACEMENT placement{sizeof(placement)};
    if (!GetWindowPlacement(window_, &placement)) return;
    const RECT& rect = placement.rcNormalPosition;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    settings_.windowLeft = rect.left;
    settings_.windowTop = rect.top;
    settings_.windowWidth = rect.right - rect.left;
    settings_.windowHeight = rect.bottom - rect.top;
    if (IsWindowVisible(window_) && !IsIconic(window_))
        settings_.windowMaximized = IsZoomed(window_) != FALSE;
}

void TrayApp::RestoreWindowPlacement()
{
    if (settings_.windowWidth <= 0 || settings_.windowHeight <= 0) return;
    // SetWindowPlacement moves a rectangle that is off every monitor back on screen.
    WINDOWPLACEMENT placement{sizeof(placement)};
    GetWindowPlacement(window_, &placement);
    placement.showCmd = SW_HIDE;
    placement.rcNormalPosition = {settings_.windowLeft, settings_.windowTop,
        settings_.windowLeft + settings_.windowWidth, settings_.windowTop + settings_.windowHeight};
    SetWindowPlacement(window_, &placement);
}

void TrayApp::StartUpdateCheck()
{
    if (update_.phase == UpdateState::Phase::Checking || update_.phase == UpdateState::Phase::Downloading) return;
    if (updateThread_.joinable()) updateThread_.join();
    update_.phase = UpdateState::Phase::Checking;
    RefreshPage();
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
    const auto& result = updateResult_;
    update_.release = result.release;
    update_.message = result.message;
    switch (result.state)
    {
    case UpdateCheckState::Failed:
        update_.phase = UpdateState::Phase::Failed;
        PostLog(LogLevel::Warning, "Update check failed.");
        break;
    case UpdateCheckState::UpToDate:
        update_.phase = UpdateState::Phase::UpToDate;
        if (!result.release.versionDisplay.empty())
            update_.message = L"Latest release on GitHub: " + result.release.versionDisplay + L".";
        PostLog(LogLevel::Info, "TPMate is up to date.");
        break;
    case UpdateCheckState::UpdateAvailable:
        update_.phase = UpdateState::Phase::Available;
        PostLog(LogLevel::Info, "A TPMate update is available.");
        break;
    }
    RefreshPage();
}

void TrayApp::StartUpdateInstall()
{
    if (update_.phase != UpdateState::Phase::Available || update_.release.assetDownloadUrl.empty()) return;
    if (updateThread_.joinable()) updateThread_.join();
    update_.phase = UpdateState::Phase::Downloading;
    RefreshPage();
    PostLog(LogLevel::Info, "Downloading TPMate update. The application will restart after installation.");
    const HWND owner = window_;
    updateThread_ = std::thread([this, owner, release = update_.release]()
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
    if (updateError_.empty() && UpdateChecker::LaunchSelfUpdater(downloadedUpdate_, GetCurrentProcessId(), updateError_))
    {
        CaptureWindowPlacement();
        SaveSettings();
        DestroyWindow(window_);
        return;
    }
    update_.phase = UpdateState::Phase::Failed;
    update_.message = updateError_;
    PostLog(LogLevel::Warning, "The update could not be installed.");
    RefreshPage();
}

void TrayApp::SaveSettings()
{
    settings_.logLevel = minimumLogLevel_.load();
    settings_.Save();
}

void TrayApp::ApplySettings()
{
    SaveSettings();
    downloader_.SetPaintOptions(settings_.loadCars, settings_.loadHelmets, settings_.loadSuits,
        settings_.loadNumbers, settings_.loadSpecMaps);
    downloader_.SetOnlyPresentDrivers(settings_.onlyPresentDrivers);
    downloader_.SetMaxConcurrentDownloads(settings_.maxConcurrentDownloads);
    downloader_.SetAutoRefreshOnReload(settings_.refreshOnTextureReload);
    // Applied to simulator exits without restarting the worker.
    downloader_.SetDeleteAfterExit(settings_.deleteAfterSession);
    UpdateReloadShortcutListener();
    UpdateStatusDisplay();
}

void TrayApp::RefreshActivityRows()
{
    RowList::Row row;
    row.title = L"Log level";
    row.detail = std::wstring(LogLevelLabel(minimumLogLevel_.load())) + L"  \u00B7  Verbose also lists every single download.";
    row.button = L"Choose...";
    activityRows_.SetRows({row});
}

void TrayApp::ChooseLogLevel()
{
    int checked = -1;
    for (size_t index = 0; index < std::size(kLogLevels); ++index)
        if (kLogLevels[index] == minimumLogLevel_.load()) checked = static_cast<int>(index);
    const int choice = ChooseFromMenu(window_, kLogLevelNames, std::size(kLogLevelNames), checked);
    if (choice < 0) return;
    minimumLogLevel_ = kLogLevels[choice];
    SaveSettings();
    RefreshActivityRows();
}

void TrayApp::PostLog(LogLevel level, const std::string& message)
{
    if (static_cast<int>(level) < static_cast<int>(minimumLogLevel_.load()))
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
    if (!logEdit_ || logs.empty()) return;
    std::wstring combined;
    for (const auto& line : logs) combined += line;
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
    if (!banner_.Handle()) return;
    const auto px = [this](int value) { return MulDiv(value, dpi_, 96); };
    const int w = MulDiv(width, 96, dpi_);
    const int h = MulDiv(height, 96, dpi_);
    const int left = kNavWidth + kMargin;
    const int content = w - left - kMargin;
    const int bottom = h - 20;
    const int top = kPageTop + 56;
    const auto place = [&](HWND control, int x, int y, int cw, int ch)
    { if (control) MoveWindow(control, px(x), px(y), px(cw), px(ch), TRUE); };

    place(navBar_.Handle(), 0, 0, kNavWidth, h);
    place(banner_.Handle(), left, 20, content, 56);
    place(pageTitle_, left, kPageTop - 1, content, 24);
    place(pageHint_, left, kPageTop + 21, content, 20);

    place(pageHost_.Handle(), left, top, content, bottom - top);

    // Activity page
    place(activityRows_.Handle(), left, top, content, 62);
    place(logEdit_, left, top + 74, content, bottom - (top + 74));
    if (logEdit_)
    {
        RECT textRect{};
        GetClientRect(logEdit_, &textRect);
        textRect.left += px(10); textRect.top += px(8);
        textRect.right -= px(10); textRect.bottom -= px(8);
        SendMessageW(logEdit_, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&textRect));
    }
}

void TrayApp::UpdateConnection(bool connected)
{
    connected_ = connected;
    UpdateReloadShortcutListener();
    UpdateStatusDisplay();
}

void TrayApp::RequestPaintRefresh()
{
    if (!connected_)
        return;
    PostLog(LogLevel::Info, "Manual paint refresh requested.");
    downloader_.RequestRefresh();
}

void TrayApp::UpdateStatusDisplay()
{
    PaintProgress progress;
    { std::lock_guard lock(progressMutex_); progress = progress_; }
    const auto count = [](size_t value) { return std::to_wstring(value); };
    constexpr wchar_t separator[] = L" \u00B7 ";
    const auto join = [&](std::initializer_list<std::wstring> parts)
    {
        std::wstring text;
        for (const auto& part : parts)
            if (!part.empty()) text += (text.empty() ? L"" : separator) + part;
        return text;
    };
    const size_t drivers = settings_.onlyPresentDrivers ? progress.selectedDrivers : progress.rosterDrivers;
    const std::wstring session = join({Utf8ToWide(progress.trackName), Utf8ToWide(progress.carName)});
    const std::wstring failed = progress.failedFiles ? count(progress.failedFiles) + L" failed" : L"";

    StatusPanel::Tone tone = StatusPanel::Tone::Neutral;
    std::wstring title = L"Waiting for iRacing";
    std::wstring detail = L"Paints are downloaded when you join a session.";
    std::wstring tip = L"TPMate - Waiting for iRacing";
    if (connected_ && progress.batchTotal > 0)
    {
        tone = StatusPanel::Tone::Busy;
        title = L"Downloading paints" + std::wstring(separator) + count(progress.batchDone) + L" of " + count(progress.batchTotal);
        detail = session;
        tip = L"TPMate - Downloading " + count(progress.batchDone) + L" of " + count(progress.batchTotal) + L" paints";
    }
    else if (connected_ && progress.rosterDrivers == 0)
    {
        title = L"Connected to iRacing";
        detail = L"Waiting for session info...";
        tip = L"TPMate - Connected to iRacing";
    }
    else if (connected_)
    {
        tone = StatusPanel::Tone::Active;
        title = L"Ready" + std::wstring(separator) + count(progress.installedFiles) + L" paint files for " +
            count(drivers) + (drivers == 1 ? L" driver" : L" drivers");
        detail = join({session, failed});
        tip = progress.failedFiles ? L"TPMate - " + failed + L" paint downloads" :
            L"TPMate - " + count(progress.installedFiles) + L" paint files for " + count(drivers) + L" drivers";
    }
    banner_.SetState(tone, title, detail, L"Refresh paints", connected_);
    if (tip != iconData_.szTip)
    {
        wcsncpy_s(iconData_.szTip, tip.c_str(), _TRUNCATE);
        iconData_.uFlags = NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &iconData_);
    }
}

void TrayApp::UpdateReloadShortcutListener()
{
    // Raw input is delivered asynchronously, so unlike a low-level keyboard hook it can never
    // delay iRacing's keyboard input while this low-priority process is waiting for CPU time.
    const bool wanted = connected_ && settings_.refreshOnTextureReload;
    if (wanted == reloadShortcutListening_)
        return;
    RAWINPUTDEVICE keyboard{0x01, 0x06, wanted ? static_cast<DWORD>(RIDEV_INPUTSINK) : static_cast<DWORD>(RIDEV_REMOVE),
        wanted ? window_ : nullptr};
    if (RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)))
    {
        reloadShortcutListening_ = wanted;
        reloadShortcut_.Reset();
    }
    else if (wanted)
        PostLog(LogLevel::Warning, "Could not enable Ctrl+R detection (Windows error " + std::to_string(GetLastError()) + ").");
}

void TrayApp::OnRawKeyboardInput(HRAWINPUT input)
{
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (!reloadShortcutListening_ ||
        GetRawInputData(input, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        raw.header.dwType != RIM_TYPEKEYBOARD)
        return;
    const auto& key = raw.data.keyboard;
    // Input injected with SendInput has no source device.
    if (!reloadShortcut_.OnKey(key.VKey, key.Message, raw.header.hDevice == nullptr,
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0,
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0))
        return;
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    HANDLE process = processId ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId) : nullptr;
    if (!process)
        return;
    wchar_t path[MAX_PATH * 4];
    DWORD length = static_cast<DWORD>(std::size(path));
    bool simulator = false;
    if (QueryFullProcessImageNameW(process, 0, path, &length))
    {
        const wchar_t* name = wcsrchr(path, L'\\');
        simulator = ReloadShortcut::IsSimulator(name ? name + 1 : path);
    }
    CloseHandle(process);
    if (simulator && connected_ && settings_.refreshOnTextureReload)
    {
        PostLog(LogLevel::Info, "Detected Ctrl+R in iRacing; requesting fresh session paints.");
        downloader_.OnIRacingTextureReload();
    }
}
