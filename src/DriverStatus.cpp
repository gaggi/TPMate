#include "DriverStatus.h"

#include "CarPresence.h"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace
{
    bool SameEntry(const SessionDriver& left, const SessionDriver& right)
    {
        return left.carIndex == right.carIndex && left.userId == right.userId;
    }

    template<class Update> void ForDrivers(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers, Update update)
    {
        for (auto& status : list)
            if (std::any_of(drivers.begin(), drivers.end(), [&](const SessionDriver& driver) { return SameEntry(status.driver, driver); }))
                update(status);
    }

    // "7" sorts before "12"; numbers that are not plain digits ("007", "1a") still sort stably.
    int NumberValue(const std::string& number)
    {
        int value = 0;
        const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
        return result.ec == std::errc{} ? value : 100000;
    }
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

void SyncDriverList(std::vector<DriverStatus>& list, const SessionInfo& roster, const SessionInfo& selected, bool reset)
{
    std::vector<DriverStatus> next;
    next.reserve(roster.drivers.size());
    for (const auto& driver : roster.drivers)
    {
        DriverStatus status;
        status.driver = driver;
        status.player = driver.carIndex >= 0 && driver.carIndex == roster.playerCarIndex;
        const bool isSelected = std::any_of(selected.drivers.begin(), selected.drivers.end(),
            [&](const SessionDriver& entry) { return SamePaintDriver(entry, driver); });
        const auto old = std::find_if(list.begin(), list.end(),
            [&](const DriverStatus& entry) { return SamePaintDriver(entry.driver, driver); });
        const bool processed = old != list.end() && old->state != DriverPaintState::NotOnTrack &&
            old->state != DriverPaintState::Waiting;
        if (!reset && processed)
        {
            status.state = old->state;
            status.installedFiles = old->installedFiles;
            status.failedFiles = old->failedFiles;
        }
        else
            status.state = isSelected ? DriverPaintState::Waiting : DriverPaintState::NotOnTrack;
        next.push_back(std::move(status));
    }
    std::stable_sort(next.begin(), next.end(), [](const DriverStatus& left, const DriverStatus& right)
    {
        if (left.player != right.player) return left.player;
        const int leftNumber = NumberValue(left.driver.carNumber), rightNumber = NumberValue(right.driver.carNumber);
        if (leftNumber != rightNumber) return leftNumber < rightNumber;
        return left.driver.userName < right.driver.userName;
    });
    list = std::move(next);
}

void MarkDriversChecking(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers)
{
    ForDrivers(list, drivers, [](DriverStatus& status)
    {
        status.state = DriverPaintState::Checking;
        status.installedFiles = 0;
        status.failedFiles = 0;
    });
}

void StartDriverDownloads(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers,
    const std::vector<PaintFile>& paints, const std::vector<int>& failedUsers)
{
    ForDrivers(list, drivers, [&](DriverStatus& status)
    {
        if (std::find(failedUsers.begin(), failedUsers.end(), status.driver.userId) != failedUsers.end())
        {
            status.state = DriverPaintState::Failed;
            return;
        }
        const bool hasPaint = std::any_of(paints.begin(), paints.end(),
            [&](const PaintFile& paint) { return PaintBelongsToDriver(paint, status.driver); });
        status.state = hasPaint ? DriverPaintState::Downloading : DriverPaintState::NoPaint;
    });
}

void ContinueDriverDownloads(std::vector<DriverStatus>& list, const std::vector<SessionDriver>& drivers,
    const std::vector<PaintFile>& paints)
{
    ForDrivers(list, drivers, [&](DriverStatus& status)
    {
        if (std::any_of(paints.begin(), paints.end(), [&](const PaintFile& paint) { return PaintBelongsToDriver(paint, status.driver); }))
            status.state = DriverPaintState::Downloading;
    });
}

void RecordPaintResult(std::vector<DriverStatus>& list, const PaintFile& paint, bool installed, bool failed)
{
    for (auto& status : list)
    {
        if (status.state != DriverPaintState::Downloading || !PaintBelongsToDriver(paint, status.driver)) continue;
        status.installedFiles += installed ? 1 : 0;
        status.failedFiles += failed ? 1 : 0;
    }
}

void FinishDriverDownloads(std::vector<DriverStatus>& list)
{
    for (auto& status : list)
    {
        if (status.state != DriverPaintState::Downloading) continue;
        status.state = status.installedFiles > 0 ? DriverPaintState::Installed :
            status.failedFiles > 0 ? DriverPaintState::Failed : DriverPaintState::NoPaint;
    }
}
