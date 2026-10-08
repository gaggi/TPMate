#include "DriverStatus.h"

#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }

    SessionDriver Driver(int carIndex, int userId, const char* number, const char* name)
    {
        SessionDriver driver;
        driver.carIndex = carIndex;
        driver.userId = userId;
        driver.carPath = "porsche992cup";
        driver.carNumber = number;
        driver.userName = name;
        return driver;
    }

    PaintFile Paint(int userId, PaintType type, const char* carPath = "porsche992cup")
    {
        PaintFile paint;
        paint.userId = userId;
        paint.type = type;
        paint.carPath = carPath;
        return paint;
    }

    const DriverStatus& Find(const std::vector<DriverStatus>& list, int userId)
    {
        for (const auto& status : list)
            if (status.driver.userId == userId) return status;
        throw std::runtime_error("Driver missing from the list");
    }
}

int main()
{
    try
    {
        SessionInfo roster;
        roster.sessionId = 1;
        roster.playerCarIndex = 2;
        roster.drivers = {Driver(0, 100, "12", "Bea"), Driver(1, 101, "7", "Al"), Driver(2, 102, "33", "Me"),
            Driver(3, 103, "abc", "Zed")};
        SessionInfo selected = roster;
        selected.drivers = {roster.drivers[1], roster.drivers[2]};

        std::vector<DriverStatus> list;
        SyncDriverList(list, roster, selected, false);
        Require(list.size() == 4, "Every roster entry is listed");
        Require(list[0].player && list[0].driver.userId == 102, "Own car first");
        Require(list[1].driver.carNumber == "7" && list[2].driver.carNumber == "12" && list[3].driver.carNumber == "abc",
            "Car numbers sort numerically, others last");
        Require(Find(list, 101).state == DriverPaintState::Waiting, "Selected driver waits for the lookup");
        Require(Find(list, 100).state == DriverPaintState::NotOnTrack, "Unselected driver is not on track");

        MarkDriversChecking(list, selected.drivers);
        Require(Find(list, 101).state == DriverPaintState::Checking, "Lookup running");
        const std::vector<PaintFile> paints = {Paint(101, PaintType::Car), Paint(101, PaintType::Helmet, "")};
        StartDriverDownloads(list, selected.drivers, paints);
        Require(Find(list, 101).state == DriverPaintState::Downloading, "Paints found");
        Require(Find(list, 102).state == DriverPaintState::NoPaint, "No paints for the player");

        RecordPaintResult(list, paints[0], true, false);
        RecordPaintResult(list, paints[1], false, true);
        Require(Find(list, 101).installedFiles == 1 && Find(list, 101).failedFiles == 1, "Results counted per driver");
        FinishDriverDownloads(list);
        Require(Find(list, 101).state == DriverPaintState::Installed, "One installed file is enough");

        SessionInfo later = roster;
        later.drivers = {roster.drivers[0], roster.drivers[2]};
        SyncDriverList(list, roster, later, false);
        Require(Find(list, 101).state == DriverPaintState::Installed, "Leaving the track keeps installed paints");
        Require(Find(list, 100).state == DriverPaintState::Waiting, "Arriving driver waits for the lookup");

        MarkDriversChecking(list, {roster.drivers[0]});
        StartDriverDownloads(list, {roster.drivers[0]}, {Paint(100, PaintType::Car)});
        RecordPaintResult(list, Paint(100, PaintType::Car), false, true);
        FinishDriverDownloads(list);
        Require(Find(list, 100).state == DriverPaintState::Failed, "Nothing installed, a download failed");

        MarkDriversChecking(list, {roster.drivers[3]});
        StartDriverDownloads(list, {roster.drivers[3]}, {}, {103});
        Require(Find(list, 103).state == DriverPaintState::Failed, "A failed lookup is not \"No paint\"");

        SyncDriverList(list, roster, later, true);
        Require(Find(list, 101).state == DriverPaintState::NotOnTrack && Find(list, 100).state == DriverPaintState::Waiting,
            "A refresh starts every driver over");

        const SessionDriver driver = Driver(0, 100, "1", "A");
        Require(PaintBelongsToDriver(Paint(100, PaintType::Helmet, "othercar"), driver), "Helmets follow the user in any car");
        Require(!PaintBelongsToDriver(Paint(100, PaintType::Car, "othercar"), driver), "Car paints must match the car");
        PaintFile team = Paint(0, PaintType::Car);
        team.teamId = 55;
        SessionDriver teamDriver = driver;
        teamDriver.teamId = 55;
        Require(PaintBelongsToDriver(team, teamDriver) && !PaintBelongsToDriver(team, driver), "Team paints follow the team");
        std::cout << "Driver status checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
