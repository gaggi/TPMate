#include "CarPresence.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace
{
    struct VarBuffer { std::int32_t tick, offset, padding[2]; };
    struct Header
    {
        std::int32_t version, status, tickRate, sessionUpdate, sessionLength, sessionOffset;
        std::int32_t numVars, varOffset, numBuffers, bufferLength, padding[2];
        VarBuffer buffers[4];
    };
    struct Variable
    {
        std::int32_t type, offset, count;
        std::uint8_t countAsTime, padding[3];
        char name[32], description[64], unit[32];
    };
    static_assert(sizeof(Header) == 112 && sizeof(Variable) == 144);
    bool Fits(std::span<const std::byte> memory, std::int32_t offset, size_t bytes)
    { return offset >= 0 && static_cast<size_t>(offset) <= memory.size() && bytes <= memory.size() - offset; }
}

PresentCars ReadPresentCars(std::span<const std::byte> memory)
{
    if (memory.size() < sizeof(Header)) return std::nullopt;
    Header header{};
    std::memcpy(&header, memory.data(), sizeof(header));
    if (!(header.status & 1) || header.numVars < 1 || header.numVars > 4096 ||
        header.numBuffers < 1 || header.numBuffers > 4 || header.bufferLength <= 0 ||
        !Fits(memory, header.varOffset, static_cast<size_t>(header.numVars) * sizeof(Variable)))
        return std::nullopt;
    Variable surfaces{};
    bool found = false;
    for (int index = 0; index < header.numVars; ++index)
    {
        Variable variable{};
        std::memcpy(&variable, memory.data() + header.varOffset + index * sizeof(Variable), sizeof(variable));
        constexpr char name[] = "CarIdxTrackSurface";
        if (std::memcmp(variable.name, name, sizeof(name)) == 0)
        { surfaces = variable; found = true; break; }
    }
    if (!found || surfaces.type != 2 || surfaces.count < 1 || surfaces.count > 256 || surfaces.offset < 0)
        return std::nullopt;
    const size_t bytes = static_cast<size_t>(surfaces.count) * sizeof(std::int32_t);
    if (surfaces.offset > header.bufferLength || bytes > static_cast<size_t>(header.bufferLength - surfaces.offset))
        return std::nullopt;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        int newest = -1;
        std::int32_t newestTick = 0;
        for (int index = 0; index < header.numBuffers; ++index)
        {
            VarBuffer buffer{};
            std::memcpy(&buffer, memory.data() + offsetof(Header, buffers) + index * sizeof(VarBuffer), sizeof(buffer));
            if (buffer.tick > newestTick && Fits(memory, buffer.offset, header.bufferLength))
            { newest = index; newestTick = buffer.tick; }
        }
        if (newest < 0) return std::nullopt;
        const auto* bufferAddress = memory.data() + offsetof(Header, buffers) + newest * sizeof(VarBuffer);
        VarBuffer before{}, after{};
        std::memcpy(&before, bufferAddress, sizeof(before));
        if (!Fits(memory, before.offset, header.bufferLength)) continue;
        std::vector<std::int32_t> values(surfaces.count);
        MemoryBarrier();
        std::memcpy(values.data(), memory.data() + before.offset + surfaces.offset, bytes);
        MemoryBarrier();
        std::memcpy(&after, bufferAddress, sizeof(after));
        std::int32_t sessionUpdate = 0;
        std::memcpy(&sessionUpdate, memory.data() + offsetof(Header, sessionUpdate), sizeof(sessionUpdate));
        if (before.tick != after.tick || before.offset != after.offset || sessionUpdate != header.sessionUpdate) continue;
        std::vector<int> present;
        for (int index = 0; index < surfaces.count; ++index)
        {
            // -1: not in world; 0..3: off track, pit stall, approaching pits, on track.
            if (values[index] < -1 || values[index] > 3) return std::nullopt;
            if (values[index] >= 0) present.push_back(index);
        }
        return present;
    }
    return std::nullopt;
}

SessionInfo SelectPaintDrivers(const SessionInfo& session, const PresentCars& presentCars, bool onlyPresent)
{
    SessionInfo selected = session;
    if (!onlyPresent) return selected;
    std::erase_if(selected.drivers, [&](const SessionDriver& driver)
    {
        // Preload our own car while in the garage. Other cars are deferred until they appear.
        if (driver.carIndex >= 0 && driver.carIndex == session.playerCarIndex) return false;
        return !presentCars || std::find(presentCars->begin(), presentCars->end(), driver.carIndex) == presentCars->end();
    });
    return selected;
}

bool SamePaintDriver(const SessionDriver& left, const SessionDriver& right)
{
    return left.carIndex == right.carIndex && left.userId == right.userId && left.teamId == right.teamId &&
        _stricmp(left.carPath.c_str(), right.carPath.c_str()) == 0 && left.carNumber == right.carNumber;
}

void RememberPaintDrivers(SessionInfo& remembered, const SessionInfo& processed)
{
    if (remembered.Key() != processed.Key()) { remembered = processed; return; }
    for (const auto& driver : processed.drivers)
        if (std::none_of(remembered.drivers.begin(), remembered.drivers.end(),
            [&](const SessionDriver& old) { return SamePaintDriver(old, driver); }))
            remembered.drivers.push_back(driver);
}
