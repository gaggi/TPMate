#pragma once

#include "AppLog.h"
#include "PaintStore.h"
#include "CarPresence.h"
#include "DriverStatus.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <windows.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct PaintProgress
{
    std::string trackName;
    std::string carName;
    size_t selectedDrivers = 0;
    size_t rosterDrivers = 0;
    size_t batchDone = 0;
    size_t batchTotal = 0; // 0 while no download batch is running.
    size_t installedFiles = 0;
    size_t failedFiles = 0;
    std::vector<DriverStatus> drivers;
};

class PaintDownloader final
{
public:
    using ProgressCallback = std::function<void(const PaintProgress& progress)>;

    PaintDownloader(LogCallback logCallback, ProgressCallback progressCallback);
    ~PaintDownloader();
    void Start(bool deleteAfterExit);
    void SetDeleteAfterExit(bool enabled);
    void SetAutoRefreshOnReload(bool enabled);
    void SetOnlyPresentDrivers(bool enabled);
    void SetMaxConcurrentDownloads(unsigned int count);
    void SetPaintOptions(bool cars, bool helmets, bool suits, bool numbers, bool specMaps);
    void SetReloadExcludeWindow(HWND window);
    void Stop();
    void OnSessionInfo(std::optional<std::string> yaml, PresentCars presentCars);
    void OnSimulatorExit();
    void OnIRacingTextureReload();
    void RequestRefresh();
    bool DeleteDownloadedPaints();

private:
    void Run();
    template<class Update> void ReportProgress(Update update);
    LogCallback logCallback_;
    ProgressCallback progressCallback_;
    std::mutex progressMutex_;
    PaintProgress progress_;
    PaintStore store_;
    std::atomic_bool stopping_{false};
    std::atomic_bool deleteAfterExit_{true};
    std::atomic_bool autoRefreshOnReload_{true};
    std::atomic_uint maxConcurrentDownloads_{5};
    std::atomic_uint paintOptions_{31};
    std::atomic_bool onlyPresentDrivers_{true};
    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::atomic_uint sessionGeneration_{0};
    std::optional<std::string> nextSession_;
    PresentCars presentCars_;
    bool presenceChanged_ = false;
    bool forceRefresh_ = false;
    bool optionsChanged_ = false;
    bool invalidateSession_ = false;
    HWND reloadExcludeWindow_ = nullptr;
    std::thread thread_;
};
