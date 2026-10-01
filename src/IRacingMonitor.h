#pragma once

#include "AppLog.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class IRacingMonitor final
{
public:
    using StatusCallback = std::function<void(bool connected)>;
    using SessionCallback = std::function<void(std::string sessionYaml)>;
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
    std::thread thread_;
};
