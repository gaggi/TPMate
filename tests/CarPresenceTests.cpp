#include "CarPresence.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

void Put(std::vector<std::byte>& memory, size_t offset, std::int32_t value)
{ std::memcpy(memory.data() + offset, &value, sizeof(value)); }

std::vector<std::byte> Fixture()
{
    std::vector<std::byte> memory(640);
    Put(memory, 4, 1); // Connected.
    Put(memory, 24, 1); Put(memory, 28, 112); // One variable header.
    Put(memory, 32, 2); Put(memory, 36, 32); // Two telemetry buffers.
    Put(memory, 48, 10); Put(memory, 52, 400);
    Put(memory, 64, 11); Put(memory, 68, 500); // Latest frame.
    Put(memory, 112, 2); Put(memory, 116, 0); Put(memory, 120, 5);
    constexpr char name[] = "CarIdxTrackSurface";
    std::memcpy(memory.data() + 128, name, sizeof(name));
    for (int index = 0; index < 5; ++index) Put(memory, 400 + index * 4, -1);
    Put(memory, 500, -1); Put(memory, 504, 1); Put(memory, 508, 3);
    Put(memory, 512, 0); Put(memory, 516, 2);
    return memory;
}

int main()
{
    try
    {
        auto memory = Fixture();
        Require(ReadPresentCars(memory) == PresentCars(std::vector<int>{1, 2, 3, 4}), "Latest buffer, including pits and off-track");
        auto empty = memory;
        for (int index = 0; index < 5; ++index) Put(empty, 500 + index * 4, -1);
        Require(ReadPresentCars(empty) == PresentCars(std::vector<int>{}), "Valid empty world differs from unavailable data");
        Require(!ReadPresentCars(std::span(memory).first(20)), "Truncated shared memory");
        auto missing = memory; missing[128] = std::byte{'X'};
        Require(!ReadPresentCars(missing), "Missing telemetry variable");
        auto bad = memory; Put(bad, 116, 30);
        Require(!ReadPresentCars(bad), "Variable extends past frame");
        bad = memory; Put(bad, 112, 4);
        Require(!ReadPresentCars(bad), "Reject wrong variable type");
        bad = memory; Put(bad, 120, 1000000);
        Require(!ReadPresentCars(bad), "Reject unbounded array");
        bad = memory; Put(bad, 28, 600);
        Require(!ReadPresentCars(bad), "Reject out-of-bounds variable table");
        bad = memory; Put(bad, 4, 0);
        Require(!ReadPresentCars(bad), "Disconnected snapshot");
        bad = memory; Put(bad, 508, 100);
        Require(!ReadPresentCars(bad), "Invalid surface value");
        bad = memory; Put(bad, 48, 0); Put(bad, 64, 0);
        Require(!ReadPresentCars(bad), "Wait for first telemetry frame");

        SessionInfo roster;
        roster.sessionId = 1; roster.playerCarIndex = 0;
        roster.drivers = {{0, 100, 0, "car", "1"}, {1, 101, 0, "car", "2"}, {2, 102, 0, "car", "3"}};
        const auto first = SelectPaintDrivers(roster, std::vector<int>{1}, true);
        Require(first.drivers.size() == 2 && first.drivers[1].userId == 101, "Exclude historical driver, keep own garage car");
        Require(SelectPaintDrivers(roster, std::nullopt, true).drivers.size() == 1, "Unknown presence defers remote downloads");
        Require(SelectPaintDrivers(roster, std::vector<int>{}, true).drivers.size() == 1, "No remote cars present");
        Require(SelectPaintDrivers(roster, std::nullopt, false).drivers.size() == 3, "Checkbox disabled restores full roster");
        auto remembered = first;
        const auto left = SelectPaintDrivers(roster, std::vector<int>{}, true);
        RememberPaintDrivers(remembered, left);
        Require(remembered.drivers.size() == 2, "Leaving the world preserves already processed driver history");
        const auto joined = SelectPaintDrivers(roster, std::vector<int>{1, 2}, true);
        RememberPaintDrivers(remembered, joined);
        RememberPaintDrivers(remembered, joined);
        Require(remembered.drivers.size() == 3, "Later arrival added once");
        auto swapped = roster.drivers[1]; swapped.userId = 201;
        Require(!SamePaintDriver(roster.drivers[1], swapped), "Team driver swap needs new wearables");
        auto next = joined; next.sessionId = 2;
        RememberPaintDrivers(remembered, next);
        Require(remembered.sessionId == 2, "New session resets history");
        std::cout << "Vehicle presence checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
