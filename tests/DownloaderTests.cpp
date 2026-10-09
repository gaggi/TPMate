// Runs the real PaintDownloader against stand-ins for Trading Paints and the paint folder,
// defined here instead of TradingPaintsClient.cpp and PaintStore.cpp: no network, no files.
#include "PaintDownloader.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace
{
    std::mutex fakeMutex;
    std::vector<std::vector<int>> lookups;       // users of each lookup
    std::map<std::string, int> downloads;        // per URL
    std::set<int> failingLookups;                // users whose lookup fails
    bool failDownloads = false;

    void ResetFakes()
    {
        std::lock_guard lock(fakeMutex);
        lookups.clear();
        downloads.clear();
        failingLookups.clear();
        failDownloads = false;
    }

    size_t LookupCount() { std::lock_guard lock(fakeMutex); return lookups.size(); }
    int Downloads(const std::string& url) { std::lock_guard lock(fakeMutex); return downloads[url]; }

    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }
}

TradingPaintsClient::TradingPaintsClient(LogCallback logCallback) : logCallback_(std::move(logCallback)) {}
TradingPaintsClient::~TradingPaintsClient() = default;

std::vector<PaintFile> TradingPaintsClient::FetchSessionPaints(const SessionInfo& session, const std::atomic_bool&,
    unsigned int, std::vector<int>* failedUsers)
{
    std::lock_guard lock(fakeMutex);
    std::vector<int> users;
    std::vector<PaintFile> paints;
    for (const auto& driver : session.drivers)
    {
        users.push_back(driver.userId);
        if (failingLookups.count(driver.userId))
        {
            if (failedUsers) failedUsers->push_back(driver.userId);
            continue;
        }
        const std::string id = std::to_string(driver.userId);
        paints.push_back({driver.userId, 0, driver.carPath, PaintType::Car, true, "car-" + id});
        paints.push_back({driver.userId, 0, "", PaintType::Helmet, true, "helmet-" + id});
    }
    lookups.push_back(users);
    return paints;
}

bool TradingPaintsClient::Download(const std::string& url, std::vector<unsigned char>& contents, std::string& error,
    const std::atomic_bool&)
{
    std::lock_guard lock(fakeMutex);
    ++downloads[url];
    if (failDownloads) { error = "simulated failure"; return false; }
    contents = {1};
    return true;
}

PaintStore::PaintStore(LogCallback logCallback) : logCallback_(std::move(logCallback)) {}
bool PaintStore::Install(const PaintFile&, const std::vector<unsigned char>&, const std::atomic_bool&) { return true; }
bool PaintStore::DeleteDownloadedPaints() { return true; }
void PaintStore::ForgetDownloadedPaints() {}

namespace
{
    // Two drivers; only the player's car (index 0) is in the world.
    const std::string kSession =
        "WeekendInfo:\n SessionID: 1\n SubSessionID: 1\nDriverInfo:\n DriverCarIdx: 0\n Drivers:\n"
        " - CarIdx: 0\n   UserID: 100\n   CarPath: car\n   CarNumber: 1\n"
        " - CarIdx: 1\n   UserID: 101\n   CarPath: car\n   CarNumber: 2\n";

    class Harness
    {
    public:
        Harness()
            : downloader_({}, [this](const PaintProgress& progress)
            {
                std::lock_guard lock(mutex_);
                progress_ = progress;
                ++reports_;
            })
        {
            downloader_.SetTextureReloadBroadcast(false);
            downloader_.Start(false);
        }
        ~Harness() { downloader_.Stop(); }

        PaintDownloader& Downloader() { return downloader_; }
        void Update() { downloader_.OnSessionInfo(kSession, std::vector<int>{0}); WaitIdle(); }

        // Waits until the downloader reported nothing new for a moment and nothing is running.
        void WaitIdle()
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            int seen = -1;
            while (std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                std::lock_guard lock(mutex_);
                const bool busy = progress_.batchTotal > 0 || std::any_of(progress_.drivers.begin(), progress_.drivers.end(),
                    [](const DriverStatus& status) { return status.state == DriverPaintState::Checking || status.state == DriverPaintState::Downloading; });
                if (!busy && reports_ == seen) return;
                seen = reports_;
            }
            throw std::runtime_error("The downloader did not become idle");
        }

        PaintProgress Progress() { std::lock_guard lock(mutex_); return progress_; }
        DriverPaintState State(int userId)
        {
            const auto progress = Progress();
            for (const auto& status : progress.drivers)
                if (status.driver.userId == userId) return status.state;
            throw std::runtime_error("Driver missing from the list");
        }

    private:
        std::mutex mutex_;
        PaintProgress progress_;
        int reports_{};
        PaintDownloader downloader_;
    };

    void FailedDownloadsAreRetried()
    {
        ResetFakes();
        Harness harness;
        { std::lock_guard lock(fakeMutex); failDownloads = true; }
        harness.Update();
        Require(LookupCount() == 1 && Downloads("car-100") == 1 && Downloads("helmet-100") == 1, "First attempt");
        Require(harness.State(100) == DriverPaintState::Failed && harness.Progress().failedFiles == 2, "Failed downloads are shown");

        { std::lock_guard lock(fakeMutex); failDownloads = false; }
        harness.Update();
        Require(LookupCount() == 2 && Downloads("car-100") == 2, "The next session update retries the driver");
        Require(harness.State(100) == DriverPaintState::Installed, "Retry installs the paints");
        const auto progress = harness.Progress();
        Require(progress.installedFiles == 2 && progress.failedFiles == 0, "Counters after a successful retry");
    }

    void RetriesStopAfterThreeAttempts()
    {
        ResetFakes();
        Harness harness;
        { std::lock_guard lock(fakeMutex); failDownloads = true; }
        for (int update = 0; update < 6; ++update) harness.Update();
        Require(LookupCount() == 3, "At most three attempts per driver and session");
        harness.Downloader().RequestRefresh();
        harness.WaitIdle();
        Require(LookupCount() == 4, "Refresh tries again");
    }

    void FailedLookupsAreRetried()
    {
        ResetFakes();
        Harness harness;
        { std::lock_guard lock(fakeMutex); failingLookups.insert(100); }
        harness.Update();
        Require(harness.State(100) == DriverPaintState::Failed && harness.Progress().failedLookups == 1, "Failed lookup is shown");
        Require(Downloads("car-100") == 0, "Nothing to download yet");

        { std::lock_guard lock(fakeMutex); failingLookups.clear(); }
        harness.Update();
        Require(LookupCount() == 2 && Downloads("car-100") == 1, "The next session update looks the driver up again");
        Require(harness.State(100) == DriverPaintState::Installed && harness.Progress().failedLookups == 0, "Lookup retry succeeds");
    }

    void PaintTypesOnlyLoadWhatChanged()
    {
        ResetFakes();
        Harness harness;
        harness.Update();
        Require(LookupCount() == 1 && Downloads("car-100") == 1 && Downloads("helmet-100") == 1, "Initial download");
        Require(harness.Progress().installedFiles == 2, "Two files installed");

        harness.Downloader().SetPaintOptions(true, false, true, true, true);
        harness.WaitIdle();
        Require(LookupCount() == 1 && Downloads("car-100") == 1, "Turning helmets off downloads nothing");

        harness.Downloader().SetPaintOptions(true, true, true, true, true);
        harness.WaitIdle();
        Require(LookupCount() == 2 && Downloads("car-100") == 1 && Downloads("helmet-100") == 2,
            "Turning helmets on downloads only helmets");
        Require(harness.Progress().installedFiles == 2 && harness.State(100) == DriverPaintState::Installed,
            "The counter counts files, not installs");
    }

    void TrackFilterKeepsInstalledPaints()
    {
        ResetFakes();
        Harness harness;
        harness.Update();
        Require(harness.State(101) == DriverPaintState::NotOnTrack, "Driver off track waits");

        harness.Downloader().SetOnlyPresentDrivers(false);
        harness.WaitIdle();
        Require(LookupCount() == 2 && lookups[1] == std::vector<int>{101}, "Turning the filter off looks up only the new driver");
        Require(Downloads("car-100") == 1 && Downloads("car-101") == 1, "Paints already installed are not downloaded again");

        harness.Downloader().SetOnlyPresentDrivers(true);
        harness.WaitIdle();
        Require(LookupCount() == 2, "Turning the filter on downloads nothing");
    }
}

int main()
{
    try
    {
        FailedDownloadsAreRetried();
        RetriesStopAfterThreeAttempts();
        FailedLookupsAreRetried();
        PaintTypesOnlyLoadWhatChanged();
        TrackFilterKeepsInstalledPaints();
        std::cout << "Downloader checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
