#pragma once

#include "AppLog.h"
#include "PaintStore.h"

#include <atomic>
#include <condition_variable>
#include <windows.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class PaintDownloader final
{
public:
    explicit PaintDownloader(LogCallback logCallback);
    ~PaintDownloader();
    void Start(bool deleteAfterExit);
    void SetDeleteAfterExit(bool enabled);
    void SetAutoRefreshOnReload(bool enabled);
    void SetMaxConcurrentDownloads(unsigned int count);
    void SetPaintOptions(bool cars, bool helmets, bool suits, bool numbers, bool specMaps);
    void SetReloadExcludeWindow(HWND window);
    void Stop();
    void OnSessionInfo(std::string yaml);
    void OnSimulatorExit();
    void OnIRacingTextureReload();
    bool DeleteDownloadedPaints();

private:
    void Run();
    LogCallback logCallback_;
    PaintStore store_;
    std::atomic_bool stopping_{false};
    std::atomic_bool deleteAfterExit_{true};
    std::atomic_bool autoRefreshOnReload_{true};
    std::atomic_uint maxConcurrentDownloads_{5};
    std::atomic_uint paintOptions_{31};
    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::atomic_uint sessionGeneration_{0};
    std::optional<std::string> nextSession_;
    bool forceRefresh_ = false;
    bool optionsChanged_ = false;
    bool invalidateSession_ = false;
    HWND reloadExcludeWindow_ = nullptr;
    std::thread thread_;
};
