#pragma once

#include "SessionInfo.h"
#include "TradingPaintsClient.h"

#include <vector>

// Where each roster entry stands, for the driver list on the Session page.
enum class DriverPaintState
{
    NotOnTrack,  // filtered out by "Only drivers on track" and not processed yet
    Waiting,     // selected, lookup not started
    Checking,    // Trading Paints lookup running
    Downloading, // paints found, files still downloading
    Installed,   // at least one paint file installed
    NoPaint,     // Trading Paints has no paint for the enabled paint types
    Failed       // the lookup failed, or paints were found but none could be installed
};

struct DriverStatus
{
    SessionDriver driver;
    bool player{};
    DriverPaintState state{DriverPaintState::NotOnTrack};
    size_t installedFiles{};
    size_t failedFiles{};
};

bool PaintBelongsToDriver(const PaintFile& paint, const SessionDriver& driver);

// Rebuilds the list from the roster. Drivers that were already processed keep their
// state (their paints stay installed); `reset` forgets everything, e.g. for a refresh.
void SyncDriverList(std::vector<DriverStatus>& list, const SessionInfo& roster, const SessionInfo& selected, bool reset);
// The lookup for these drivers started.
void MarkDriversChecking(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers);
// The lookup finished: drivers with paints start downloading, the others have none,
// except drivers whose lookup failed.
void StartDriverDownloads(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers,
    const std::vector<PaintFile>& paints, const std::vector<int>& failedUsers = {});
// Already processed drivers that get more paints (a newly enabled paint type) download
// them without losing their counts.
void ContinueDriverDownloads(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers,
    const std::vector<PaintFile>& paints);
void RecordPaintResult(std::vector<DriverStatus>& list, const PaintFile& paint, bool installed, bool failed);
// The batch ended; downloading drivers become Installed, Failed or NoPaint.
void FinishDriverDownloads(std::vector<DriverStatus>& list);
