#include "IRacingMonitor.h"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
    constexpr wchar_t kMapName[] = L"Local\\IRSDKMemMapFileName";
    constexpr std::int32_t kConnected = 1;
    constexpr size_t kMaximumSessionInfoBytes = 512 * 1024;

    struct IracingHeader
    {
        std::int32_t version;
        std::int32_t status;
        std::int32_t tickRate;
        std::int32_t sessionInfoUpdate;
        std::int32_t sessionInfoLength;
        std::int32_t sessionInfoOffset;
    };

    struct SessionSnapshot
    {
        bool connected = false;
        int update = -1;
        std::string yaml;
        PresentCars presentCars;
    };

    bool IsSimulatorProcess(const wchar_t* name)
    {
        return _wcsicmp(name, L"iRacingSim64DX11.exe") == 0 || _wcsicmp(name, L"iRacingSim64.exe") == 0;
    }

    bool IsSimulatorRunning()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (IsSimulatorProcess(entry.szExeFile))
                {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    SessionSnapshot ReadSessionSnapshot(int lastUpdate)
    {
        SessionSnapshot result;
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kMapName);
        if (!mapping)
            return result;

        const auto* view = static_cast<const std::byte*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (view)
        {
            const auto* header = reinterpret_cast<const IracingHeader*>(view);
            result.connected = (header->status & kConnected) != 0;
            MEMORY_BASIC_INFORMATION memoryInfo{};
            if (result.connected && VirtualQuery(view, &memoryInfo, sizeof(memoryInfo)) == sizeof(memoryInfo))
            {
                const size_t viewBytes = memoryInfo.RegionSize;
                for (int attempt = 0; attempt < 3; ++attempt)
                {
                    MemoryBarrier();
                    const auto updateBefore = header->sessionInfoUpdate;
                    if (updateBefore == lastUpdate)
                    {
                        result.update = updateBefore;
                        break;
                    }
                    const auto offset = header->sessionInfoOffset;
                    const auto length = header->sessionInfoLength;
                    std::string candidate;
                    if (offset >= 0 && length > 0 && static_cast<size_t>(offset) < viewBytes)
                    {
                        const size_t available = viewBytes - static_cast<size_t>(offset);
                        const size_t capacity = (std::min)(available, (std::min)(static_cast<size_t>(length), kMaximumSessionInfoBytes));
                        const char* text = reinterpret_cast<const char*>(view + offset);
                        const void* terminator = std::memchr(text, '\0', capacity);
                        const size_t textLength = terminator ? static_cast<const char*>(terminator) - text : capacity;
                        candidate.assign(text, textLength);
                    }
                    MemoryBarrier();
                    const auto updateAfter = header->sessionInfoUpdate;
                    if (updateBefore == updateAfter)
                    {
                        result.update = updateAfter;
                        result.yaml = std::move(candidate);
                        break;
                    }
                }
                if (result.update == header->sessionInfoUpdate)
                    result.presentCars = ReadPresentCars({view, viewBytes});
            }
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
        return result;
    }
}

IRacingMonitor::IRacingMonitor(StatusCallback statusCallback, SessionCallback sessionCallback,
    SimulatorExitCallback simulatorExitCallback, LogCallback logCallback)
    : statusCallback_(std::move(statusCallback)), sessionCallback_(std::move(sessionCallback)),
      simulatorExitCallback_(std::move(simulatorExitCallback)), logCallback_(std::move(logCallback))
{}
IRacingMonitor::~IRacingMonitor() { Stop(); }

void IRacingMonitor::Start()
{
    if (thread_.joinable())
        return;
    stopping_ = false;
    thread_ = std::thread(&IRacingMonitor::Run, this);
}

void IRacingMonitor::Stop()
{
    stopping_ = true;
    if (thread_.joinable())
        thread_.join();
}

void IRacingMonitor::Run()
{
    bool previous = false;
    bool simulatorWasRunning = IsSimulatorRunning();
    int lastSessionUpdate = (std::numeric_limits<int>::min)();
    std::string currentYaml;
    PresentCars lastPresentCars;
    if (logCallback_)
        logCallback_(LogLevel::Info, "Waiting for the iRacing simulator shared memory.");

    while (!stopping_)
    {
        SessionSnapshot snapshot = ReadSessionSnapshot(lastSessionUpdate);
        const bool simulatorRunning = snapshot.connected || IsSimulatorRunning();
        if (simulatorWasRunning && !simulatorRunning)
        {
            if (logCallback_)
                logCallback_(LogLevel::Info, "iRacing simulator exited.");
            if (simulatorExitCallback_)
                simulatorExitCallback_();
            lastSessionUpdate = (std::numeric_limits<int>::min)();
            currentYaml.clear();
            lastPresentCars.reset();
        }
        simulatorWasRunning = simulatorRunning;

        const bool connected = snapshot.connected;
        if (connected != previous)
        {
            previous = connected;
            if (statusCallback_)
                statusCallback_(connected);
            if (logCallback_)
                logCallback_(connected ? LogLevel::Info : LogLevel::Verbose,
                    connected ? "Connected to iRacing shared memory." : "iRacing shared memory disconnected.");
            lastSessionUpdate = (std::numeric_limits<int>::min)();
        }
        if (connected && !snapshot.yaml.empty() && snapshot.update != lastSessionUpdate)
        {
            lastSessionUpdate = snapshot.update;
            if (logCallback_)
                logCallback_(LogLevel::Verbose, "Received a new iRacing session-info update.");
            currentYaml = std::move(snapshot.yaml);
            if (sessionCallback_)
                sessionCallback_(currentYaml, snapshot.presentCars);
            lastPresentCars = std::move(snapshot.presentCars);
        }
        else if (connected && snapshot.update == lastSessionUpdate && !currentYaml.empty() && snapshot.presentCars != lastPresentCars)
        {
            if (sessionCallback_) sessionCallback_(currentYaml, snapshot.presentCars);
            lastPresentCars = std::move(snapshot.presentCars);
        }
        if (!connected) { currentYaml.clear(); lastPresentCars.reset(); }
        Sleep(1000);
    }
}
