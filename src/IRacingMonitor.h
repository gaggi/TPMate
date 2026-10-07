#pragma once

#include "AppLog.h"
#include "CarPresence.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class IRacingMonitor final
{
public:
    using StatusCallback = std::function<void(bool connected)>;
    // sessionYaml is nullopt when only vehicle presence changed since the previous callback.
    using SessionCallback = std::function<void(std::optional<std::string> sessionYaml, PresentCars presentCars)>;
    using SimulatorExitCallback = std::function<void()>;

    IRacingMonitor(StatusCallback statusCallback, SessionCallback sessionCallback,
        SimulatorExitCallback simulatorExitCallback, LogCallback logCallback);
    ~IRacingMonitor();
    void Start();
    void Stop();

private:
    void Run();
    StatusCallback statusCallback_;
    SessionCallback sessionCallback_;
    SimulatorExitCallback simulatorExitCallback_;
    LogCallback logCallback_;
    std::atomic_bool stopping_{false};
    std::mutex stopMutex_;
    std::condition_variable stopRequested_;
    std::thread thread_;
};
