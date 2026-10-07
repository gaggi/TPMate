#include "PaintDownloader.h"

#include "SessionInfo.h"
#include "TradingPaintsClient.h"

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

namespace
{
    constexpr int kReloadTexturesMessage = 7;

    bool ShouldLoadPaint(PaintType type, unsigned int options)
    {
        switch (type)
        {
        case PaintType::Helmet: return (options & 2u) != 0;
        case PaintType::Suit: return (options & 4u) != 0;
        case PaintType::CarNumber: return (options & 1u) && (options & 8u);
        case PaintType::CarSpec: return (options & 1u) && (options & 16u);
        default: return (options & 1u) != 0;
        }
    }

    const char* PaintTypeName(PaintType type)
    {
        switch (type)
        {
        case PaintType::Car: return "car";
        case PaintType::CarDecal: return "car decal";
        case PaintType::CarNumber: return "car number";
        case PaintType::CarSpec: return "car specular";
        case PaintType::Helmet: return "helmet";
        case PaintType::Suit: return "suit";
        }
        return "unknown";
    }

    void ReloadIRacingTextures(HWND excludeWindow, bool allCars, int carIndex = -1)
    {
        const UINT message = RegisterWindowMessageW(L"IRSDK_BROADCASTMSG");
        if (message == 0)
            return;
        struct BroadcastContext { UINT message; HWND exclude; bool allCars; int carIndex; }
            context{message, excludeWindow, allCars, carIndex};
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL
        {
            const auto* context = reinterpret_cast<const BroadcastContext*>(parameter);
            if (window != context->exclude)
            {
                const WORD mode = context->allCars ? 0 : 1; // iRacing ReloadTexturesMode: All or CarIdx.
                SendNotifyMessageW(window, context->message,
                    MAKEWPARAM(kReloadTexturesMessage, mode),
                    MAKELPARAM(context->allCars ? 0 : context->carIndex, 0));
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
    }

    bool PaintBelongsToDriver(const PaintFile& paint, const SessionDriver& driver)
    {
        const bool wearable = paint.type == PaintType::Helmet || paint.type == PaintType::Suit;
        if (!wearable && !paint.carPath.empty() && _stricmp(paint.carPath.c_str(), driver.carPath.c_str()) != 0)
            return false;
        if (paint.userId > 0 && paint.userId == driver.userId)
            return true;
        return paint.teamId > 0 && paint.teamId == driver.teamId;
    }

    std::string MinimalDiagnosticYaml(const std::string& yaml)
    {
        std::string result = "# TPMate parse-failure snapshot; names and unrelated telemetry are omitted.\r\n";
        size_t start = 0;
        bool weekendInfo = false;
        bool driverInfo = false;
        bool driversList = false;
        bool inDriver = false;
        constexpr int driverListIndent = 1;
        const auto isField = [](std::string_view line, std::string_view key)
        {
            return line.size() > key.size() && line.substr(0, key.size()) == key && line[key.size()] == ':';
        };
        while (start < yaml.size())
        {
            const size_t end = yaml.find('\n', start);
            const size_t lineEnd = end == std::string::npos ? yaml.size() : end;
            std::string_view line(yaml.data() + start, lineEnd - start);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            int indent = 0;
            while (indent < static_cast<int>(line.size()) && line[static_cast<size_t>(indent)] == ' ') ++indent;
            const auto content = line.substr(static_cast<size_t>(indent));
            bool keep = false;
            if (indent == 0)
            {
                weekendInfo = content == "WeekendInfo:";
                driverInfo = content == "DriverInfo:";
                driversList = false;
                inDriver = false;
                keep = content == "---" || weekendInfo || driverInfo;
            }
            else if (weekendInfo && indent == 1)
            {
                keep = isField(content, "Encoding") || isField(content, "SessionID") ||
                    isField(content, "SubSessionID") || isField(content, "SeriesID") ||
                    isField(content, "LeagueID") || isField(content, "TeamRacing");
            }
            else if (driverInfo && driversList && indent == driverListIndent &&
                content.size() >= 2 && content.substr(0, 2) == "- ")
            {
                inDriver = true;
                keep = true;
            }
            else if (driverInfo && driversList && inDriver && indent > driverListIndent)
            {
                keep = isField(content, "CarIdx") || isField(content, "UserID") || isField(content, "TeamID") ||
                    isField(content, "CarPath") || isField(content, "CarNumber");
            }
            else if (driverInfo && indent == 1)
            {
                if (driversList)
                    driversList = false;
                if (isField(content, "Drivers"))
                {
                    driversList = true;
                    keep = true;
                }
                else if (isField(content, "DriverCarIdx"))
                {
                    keep = true;
                }
            }
            if (keep)
            {
                result.append(line);
                result.append("\r\n");
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return result;
    }

    std::wstring SaveSessionInfoDiagnostic(const std::string& yaml, size_t sequence)
    {
        if (yaml.empty()) return {};
        wchar_t localAppData[MAX_PATH]{};
        const DWORD chars = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (chars == 0 || chars >= MAX_PATH) return {};
        const std::wstring root = std::wstring(localAppData) + L"\\TPMate";
        if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
        const std::wstring directory = root + L"\\SessionInfoFailures";
        if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t name[96]{};
        swprintf_s(name, L"session-info-failure-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%04llu.txt",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            GetCurrentProcessId(),
            static_cast<unsigned long long>(sequence));
        const std::wstring path = directory + L"\\" + name;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        const std::string diagnostic = MinimalDiagnosticYaml(yaml);
        DWORD written = 0;
        const bool ok = WriteFile(file, diagnostic.data(), static_cast<DWORD>(diagnostic.size()), &written, nullptr) && written == diagnostic.size();
        CloseHandle(file);
        return ok ? path : std::wstring{};
    }
}

PaintDownloader::PaintDownloader(LogCallback logCallback)
    : logCallback_(std::move(logCallback)), store_(logCallback_)
{}

PaintDownloader::~PaintDownloader() { Stop(); }

void PaintDownloader::Start(bool deleteAfterExit)
{
    if (thread_.joinable())
        return;
    deleteAfterExit_ = deleteAfterExit;
    stopping_ = false;
    thread_ = std::thread(&PaintDownloader::Run, this);
}

void PaintDownloader::Stop()
{
    stopping_ = true;
    workAvailable_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void PaintDownloader::SetDeleteAfterExit(bool enabled)
{
    deleteAfterExit_ = enabled;
}

void PaintDownloader::SetAutoRefreshOnReload(bool enabled)
{
    autoRefreshOnReload_ = enabled;
}

void PaintDownloader::SetOnlyPresentDrivers(bool enabled)
{
    if (onlyPresentDrivers_.exchange(enabled) == enabled) return;
    std::lock_guard lock(mutex_);
    optionsChanged_ = true;
    workAvailable_.notify_one();
}

void PaintDownloader::SetMaxConcurrentDownloads(unsigned int count)
{
    maxConcurrentDownloads_ = (std::clamp)(count, 1u, 10u);
}

void PaintDownloader::SetReloadExcludeWindow(HWND window)
{
    reloadExcludeWindow_ = window;
}

void PaintDownloader::SetPaintOptions(bool cars, bool helmets, bool suits, bool numbers, bool specMaps)
{
    const unsigned int options = (cars ? 1u : 0u) | (helmets ? 2u : 0u) |
        (suits ? 4u : 0u) | (numbers ? 8u : 0u) | (specMaps ? 16u : 0u);
    if (paintOptions_.exchange(options) != options)
    {
        std::lock_guard lock(mutex_);
        optionsChanged_ = true;
        workAvailable_.notify_one();
    }
}

void PaintDownloader::OnSessionInfo(std::optional<std::string> yaml, PresentCars presentCars)
{
    std::lock_guard lock(mutex_);
    if (yaml)
        nextSession_ = std::move(yaml);
    presentCars_ = std::move(presentCars);
    presenceChanged_ = true;
    workAvailable_.notify_one();
}

void PaintDownloader::OnSimulatorExit()
{
    {
        std::lock_guard lock(mutex_);
        ++sessionGeneration_;
        invalidateSession_ = true;
        workAvailable_.notify_one();
        nextSession_.reset();
        presentCars_.reset();
        presenceChanged_ = false;
    }
}

void PaintDownloader::OnIRacingTextureReload()
{
    if (!autoRefreshOnReload_.load())
        return;
    std::lock_guard lock(mutex_);
    forceRefresh_ = true;
    workAvailable_.notify_one();
}

bool PaintDownloader::DeleteDownloadedPaints()
{
    return store_.DeleteDownloadedPaints();
}

void PaintDownloader::Run()
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    TradingPaintsClient client(logCallback_);
    std::wstring lastSessionKey;
    std::optional<SessionInfo> currentSession;
    std::optional<SessionInfo> lastProcessedSession;
    bool parseWarningLogged = false;
    bool presenceWarningLogged = false;
    size_t diagnosticCount = 0;
    while (!stopping_.load())
    {
        std::optional<std::string> yaml;
        bool forceRefresh = false;
        bool optionsChanged = false;
        bool invalidateSession = false;
        bool presenceChanged = false;
        PresentCars presentCars;
        {
            std::unique_lock lock(mutex_);
            workAvailable_.wait(lock, [&]()
            {
                return stopping_.load() || nextSession_.has_value() || presenceChanged_ || forceRefresh_ ||
                    optionsChanged_ || invalidateSession_;
            });
            if (stopping_.load())
                break;
            yaml.swap(nextSession_);
            presentCars = presentCars_;
            presenceChanged = presenceChanged_;
            presenceChanged_ = false;
            forceRefresh = forceRefresh_;
            forceRefresh_ = false;
            optionsChanged = optionsChanged_;
            optionsChanged_ = false;
            invalidateSession = invalidateSession_;
            invalidateSession_ = false;
        }
        if (invalidateSession)
        {
            if (deleteAfterExit_.load())
                store_.DeleteDownloadedPaints();
            currentSession.reset();
            lastProcessedSession.reset();
            lastSessionKey.clear();
            presenceWarningLogged = false;
        }

        bool parsedSessionInfo = false;
        if (yaml)
        {
            std::string parseError;
            const auto session = ParseSessionInfo(*yaml, &parseError);
            if (!session)
            {
                if (!yaml->empty())
                {
                    const auto path = SaveSessionInfoDiagnostic(*yaml, ++diagnosticCount);
                    if (!path.empty())
                    {
                        if (logCallback_)
                            logCallback_(LogLevel::Info, "Saved minimized session-info failure #" + std::to_string(diagnosticCount) +
                                " under %LOCALAPPDATA%\\TPMate\\SessionInfoFailures.");
                    }
                }
                if (!parseWarningLogged && logCallback_)
                    logCallback_(LogLevel::Warning, "Could not parse iRacing session info (" + parseError +").");
                parseWarningLogged = true;
            }
            else
            {
                parseWarningLogged = false;
                currentSession = *session;
                parsedSessionInfo = true;
            }
        }

        if (forceRefresh && yaml && !parsedSessionInfo)
        {
            std::lock_guard lock(mutex_);
            forceRefresh_ = true;
            forceRefresh = false;
        }

        const bool onlyPresent = onlyPresentDrivers_.load();
        // Presence changes only affect the selection when the present-drivers filter is enabled.
        if (currentSession && (parsedSessionInfo || forceRefresh || optionsChanged || (presenceChanged && onlyPresent)))
        {
            if (onlyPresent && !presentCars && !presenceWarningLogged && logCallback_)
                logCallback_(LogLevel::Info, "Waiting for valid vehicle-presence telemetry; other drivers' paints are deferred.");
            presenceWarningLogged = onlyPresent && !presentCars;
            const SessionInfo paintSession = SelectPaintDrivers(*currentSession, presentCars, onlyPresent);
            std::wstring sessionKey = paintSession.Key();
            for (const auto& driver : paintSession.drivers)
                sessionKey += L"|" + std::to_wstring(driver.carIndex) + L":" +
                    std::to_wstring(driver.userId) + L":" + std::to_wstring(driver.teamId) +
                    L":" + std::wstring(driver.carPath.begin(), driver.carPath.end()) + L":" +
                    std::wstring(driver.carNumber.begin(), driver.carNumber.end());

            if (forceRefresh || optionsChanged || sessionKey != lastSessionKey)
            {
                const auto batchStarted = std::chrono::steady_clock::now();
                const unsigned int batchGeneration = sessionGeneration_.load();
                const bool isNewSession = !lastProcessedSession ||
                    paintSession.Key() != lastProcessedSession->Key();
                if (forceRefresh)
                {
                    if (logCallback_)
                        logCallback_(LogLevel::Info, "iRacing requested a texture reload; refreshing session paints.");
                    if (!store_.DeleteDownloadedPaints() && logCallback_)
                        logCallback_(LogLevel::Warning, "Some downloaded paints could not be deleted before refresh.");
                }
                const unsigned int concurrency = maxConcurrentDownloads_.load();
                if (onlyPresent && logCallback_)
                    logCallback_(LogLevel::Verbose, "Paint filter: " + std::to_string(paintSession.drivers.size()) +
                        " present/player drivers out of " + std::to_string(currentSession->drivers.size()) + " roster entries.");
                SessionInfo affectedSession = paintSession;
                bool shouldQuery = !affectedSession.drivers.empty();
                const bool canUseRosterDelta = !forceRefresh && !optionsChanged && lastProcessedSession &&
                    paintSession.Key() == lastProcessedSession->Key() &&
                    !isNewSession;
                if (canUseRosterDelta)
                {
                    affectedSession.drivers.clear();
                    for (const auto& driver : paintSession.drivers)
                    {
                        const auto existing = std::find_if(lastProcessedSession->drivers.begin(), lastProcessedSession->drivers.end(),
                            [&](const SessionDriver& oldDriver)
                            {
                                return SamePaintDriver(oldDriver, driver);
                            });
                        if (existing == lastProcessedSession->drivers.end())
                            affectedSession.drivers.push_back(driver);
                    }
                    shouldQuery = !affectedSession.drivers.empty();
                    if (!shouldQuery && logCallback_)
                        logCallback_(LogLevel::Info, "Session roster changed; no new or changed drivers need paint lookups.");
                }
                lastSessionKey = sessionKey;
                if (!lastProcessedSession || isNewSession || forceRefresh || optionsChanged)
                    lastProcessedSession = paintSession;
                else
                    RememberPaintDrivers(*lastProcessedSession, paintSession);
                // Team lookups are an aggregate API request, so query the complete roster,
                // then keep only paints belonging to newly added or changed entries.
                const SessionInfo& lookupSession = paintSession.teamRacing ? paintSession : affectedSession;
                auto paints = shouldQuery ? client.FetchSessionPaints(lookupSession, stopping_, concurrency) : std::vector<PaintFile>{};
                const unsigned int options = paintOptions_.load();
                paints.erase(std::remove_if(paints.begin(), paints.end(), [options](const PaintFile& paint)
                {
                    return !ShouldLoadPaint(paint.type, options);
                }), paints.end());
                if (shouldQuery && (paintSession.teamRacing || onlyPresent))
                {
                    paints.erase(std::remove_if(paints.begin(), paints.end(), [&](const PaintFile& paint)
                    {
                        return std::find_if(affectedSession.drivers.begin(), affectedSession.drivers.end(),
                            [&](const SessionDriver& driver) { return PaintBelongsToDriver(paint, driver); }) == affectedSession.drivers.end();
                    }), paints.end());
                }
                if (logCallback_ && !paints.empty())
                    logCallback_(LogLevel::Info, "Downloading " + std::to_string(paints.size()) + " paints with " +
                        std::to_string((std::min)(concurrency, static_cast<unsigned int>(paints.size()))) + " simultaneous downloads.");

                struct DownloadResult { bool installed = false; };
                std::vector<DownloadResult> results(paints.size());
                std::atomic_size_t nextPaint{0};
                const auto downloadWorker = [&]()
                {
                    // Each worker owns only one compressed paint at a time. Decompression
                    // can overlap other downloads; PaintStore serializes file replacement.
                    std::vector<unsigned char> contents;
                    while (!stopping_.load() && sessionGeneration_.load() == batchGeneration)
                    {
                        const size_t index = nextPaint.fetch_add(1);
                        if (index >= paints.size())
                            break;
                        const auto& paint = paints[index];
                        if (!ShouldLoadPaint(paint.type, paintOptions_.load()))
                            continue;
                        if (logCallback_)
                            logCallback_(LogLevel::Verbose, "Downloading a " + std::string(PaintTypeName(paint.type)) +
                                " paint for user " + std::to_string(paint.userId) +
                                (paint.carPath.empty() ? std::string(".") : " in " + paint.carPath + "."));
                        std::string error;
                        if (client.Download(paint.url, contents, error, stopping_))
                        {
                            if (logCallback_)
                                logCallback_(LogLevel::Verbose, "Downloaded " + std::to_string(contents.size()) + " bytes.");
                            if (sessionGeneration_.load() == batchGeneration &&
                                ShouldLoadPaint(paint.type, paintOptions_.load()))
                                results[index].installed = store_.Install(paint, contents, stopping_);
                        }
                        else if (!stopping_.load() && logCallback_)
                            logCallback_(LogLevel::Warning, "Paint download failed: " + error);
                    }
                };
                std::vector<std::thread> workers;
                const size_t workerCount = (std::min)(static_cast<size_t>(concurrency), paints.size());
                bool workerCreationFailed = false;
                for (size_t i = 0; i < workerCount; ++i)
                {
                    try { workers.emplace_back(downloadWorker); }
                    catch (...) { workerCreationFailed = true; break; }
                }
                for (auto& worker : workers)
                    if (worker.joinable()) worker.join();
                if (workerCreationFailed)
                {
                    if (logCallback_)
                        logCallback_(LogLevel::Warning, "Could not start all download workers; continuing with fewer workers.");
                    downloadWorker();
                }

                size_t installed = 0;
                std::set<std::string> installedCars;
                std::set<std::string> installedSuits;
                std::set<std::string> installedHelmets;
                std::set<int> changedCarIndexes;
                bool unknownCarIndex = false;
                for (size_t index = 0; index < paints.size(); ++index)
                {
                    if (stopping_.load())
                        break;
                    if (results[index].installed)
                    {
                        ++installed;
                        const std::string owner = paints[index].teamId > 0 ?
                            "team:" + std::to_string(paints[index].teamId) :
                            "user:" + std::to_string(paints[index].userId);
                        switch (paints[index].type)
                        {
                        case PaintType::Helmet:
                            installedHelmets.insert(std::to_string(paints[index].userId));
                            break;
                        case PaintType::Suit:
                            installedSuits.insert(owner);
                            break;
                        default:
                            installedCars.insert(owner + "|" + paints[index].carPath);
                            break;
                        }
                        bool mappedToCar = false;
                        for (const auto& driver : affectedSession.drivers)
                        {
                            if (!PaintBelongsToDriver(paints[index], driver))
                                continue;
                            mappedToCar = true;
                            if (driver.carIndex >= 0)
                                changedCarIndexes.insert(driver.carIndex);
                            else
                                unknownCarIndex = true;
                        }
                        if (!mappedToCar)
                            unknownCarIndex = true;
                    }
                }
                if (installed && !stopping_.load() && sessionGeneration_.load() == batchGeneration)
                {
                    const bool reloadAll = forceRefresh || isNewSession || unknownCarIndex || changedCarIndexes.empty();
                    if (reloadAll)
                    {
                        ReloadIRacingTextures(reloadExcludeWindow_, true);
                    }
                    else
                    {
                        for (const int carIndex : changedCarIndexes)
                            ReloadIRacingTextures(reloadExcludeWindow_, false, carIndex);
                    }
                    if (logCallback_)
                        logCallback_(LogLevel::Info, "Reloaded iRacing textures for " +
                            std::to_string(installedCars.size()) + " Cars, " +
                            std::to_string(installedSuits.size()) + " Suits, " +
                            std::to_string(installedHelmets.size()) + " Helmets.");
                }
                if (logCallback_)
                {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - batchStarted).count();
                    logCallback_(LogLevel::Verbose, "Paint batch completed in " + std::to_string(elapsed) +
                        " ms; installed " + std::to_string(installed) + " paint files.");
                }
            }
        }
    }
    if (uninitializeCom)
        CoUninitialize();
}
